#include "cli/cli_common.hpp"
#include "cli/cli_includes.hpp"

namespace fs = std::filesystem;
using namespace dcstudy;

namespace fs = std::filesystem;
using namespace dcstudy;

namespace dcstudy {

constexpr std::string_view kBoolFlags[] = {
    "json",     "all",         "canonical", "exact",    "keep",
    "help",     "verbose",     "no-tg",     "heavy",    "png",
    "no-logic", "static-cmos", "graphblas", "tri-state"};

Args parse_args(int argc, char **argv, int start) {
  Args a;
  for (int i = start; i < argc; ++i) {
    std::string s = argv[i];
    if (s.rfind("--", 0) != 0) {
      throw CliError("unexpected argument: " + s);
    }
    const std::string body = s.substr(2);
    const auto eq = body.find('=');
    const std::string key = body.substr(0, eq);
    bool is_bool = false;
    for (std::string_view f : kBoolFlags)
      is_bool = is_bool || key == f;
    if (is_bool) {
      if (eq != std::string::npos)
        throw CliError("flag --" + key + " does not take a value");
      a.flag.insert(key);
    } else {
      if (eq != std::string::npos) {
        a.opt[key] = body.substr(eq + 1);
      } else {
        if (i + 1 >= argc || std::string(argv[i + 1]).rfind("--", 0) == 0)
          throw CliError("missing value for --" + key);
        a.opt[key] = argv[++i];
      }
    }
  }
  return a;
}

std::vector<std::string> split_csv(const std::string &s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    const auto p = s.find(',', start);
    std::string item =
        s.substr(start, p == std::string::npos ? std::string::npos : p - start);
    auto b = item.find_first_not_of(" \t");
    auto e = item.find_last_not_of(" \t");
    if (b != std::string::npos)
      out.push_back(item.substr(b, e - b + 1));
    if (p == std::string::npos)
      break;
    start = p + 1;
  }
  return out;
}

FuncSpec get_spec(const Args &a) {
  const bool has_expr = a.has("expr");
  const bool has_tt = a.has("tt");
  if (has_expr == has_tt)
    throw CliError("give exactly one of --expr or --tt");
  const auto vars = split_csv(a.get("vars"));
  if (has_expr)
    return spec_from_expr(a.get("expr"), vars);
  return spec_from_truth(a.get("tt"), vars);
}

std::vector<int> target_of(const FuncSpec &s) {
  std::vector<int> t;
  t.reserve(s.func.num_minterms());
  for (std::uint32_t m = 0; m < s.func.num_minterms(); ++m)
    t.push_back(s.func.value(m) ? 1 : 0);
  return t;
}

std::string join(const std::vector<std::string> &v, const std::string &sep) {
  std::string out;
  for (const auto &s : v) {
    if (!out.empty())
      out += sep;
    out += s;
  }
  return out;
}

Json driver_profile_json(const DriverProfile &p) {
  Json j;
  j["all_single"] = p.all_single;
  j["valid"] = p.valid;
  j["simple"] = p.simple;
  j["max_drivers"] = p.max_drivers;
  j["power_steps"] = p.power_steps;
  Json rows = Json::Array{};
  for (std::size_t m = 0; m < p.rows.size(); ++m) {
    const auto &r = p.rows[m];
    Json row;
    row["count"] = static_cast<int>(r.sources.size());
    Json sources = Json::Array{};
    Json values = Json::Array{};
    Json strong = Json::Array{};
    for (std::size_t i = 0; i < r.sources.size(); ++i) {
      sources.push_back(r.sources[i]);
      values.push_back(r.values[i]);
      strong.push_back(r.strong[i]);
    }
    row["sources"] = std::move(sources);
    row["values"] = std::move(values);
    row["strong"] = std::move(strong);
    rows.push_back(std::move(row));
  }
  j["rows"] = std::move(rows);
  return j;
}

Json design_json(const Design &d, const std::vector<std::string> &vars) {
  Json j;
  j["cost"] = d.cost;
  Json invs = Json::Array{};
  for (auto i : d.inv_vars)
    invs.push_back(vars.at(i));
  j["inverters"] = invs;
  Json branches = Json::Array{};
  for (const auto &b : d.branches) {
    Json bj;
    bj["source"] = b.source;
    bj["cost"] = b.cost;
    Json chain = Json::Array{};
    for (const auto &el : b.chain) {
      Json ej;
      ej["kind"] = switch_name(el.kind);
      ej["lit"] = el.lit;
      ej["cost"] = el.cost;
      chain.push_back(std::move(ej));
    }
    bj["chain"] = std::move(chain);
    bj["topology"] = b.topo(vars);
    branches.push_back(std::move(bj));
  }
  j["branches"] = std::move(branches);
  j["topology"] = d.topologies(vars);
  return j;
}

std::vector<std::string> cube_names(const Cube &c,
                                    const std::vector<std::string> &vars) {
  std::vector<std::string> out;
  const std::size_t n = vars.size();
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint64_t bit = std::uint64_t{1} << (n - 1 - i);
    if (!(c.care & bit))
      continue;
    if (!(c.value & bit))
      throw std::runtime_error("PD cube contains a negated literal");
    out.push_back(vars[i]);
  }
  return out;
}

Json static_design_json(const CmosDesign &d,
                        const std::vector<std::string> &vars) {
  Json j;
  j["kind"] = "static-cmos";
  j["cost"] = d.total;
  j["nfet"] = d.nfet;
  j["pfet"] = d.pfet;
  j["kappa_linear"] = true;
  j["pun_dual"] = true;
  j["inverters"] = Json::Array{};
  j["branches"] = Json::Array{};
  Json pd = Json::Array{};
  Json pu = Json::Array{};
  for (const auto &c : d.pull_down.terms) {
    const auto names = cube_names(c, vars);
    Json term = Json::Array{};
    for (const auto &s : names)
      term.push_back(s);
    pd.push_back(term);
    // PUN is the dual: each PDN term becomes a parallel PMOS group, groups
    // are in series.
    pu.push_back(term);
  }
  j["pd_terms"] = std::move(pd);
  j["pu_groups"] = std::move(pu);
  j["pull_down"] = d.pull_down.expression(vars);
  j["pull_up"] = pull_up_expression(d, vars);
  j["topology"] = "PDN: " + d.pull_down.expression(vars) +
                  "; PUN: " + pull_up_expression(d, vars);
  return j;
}

std::string subckt_to_cdl(const Subckt &c) {
  std::string out = ".SUBCKT " + c.name;
  for (const auto &p : c.ports)
    out += " " + p;
  out += "\n*.PININFO";
  for (const auto &p : c.ports) {
    const char dir = [&] {
      switch (c.direction(p)) {
      case PinDir::Input:
        return 'I';
      case PinDir::Output:
        return 'O';
      case PinDir::Power:
        return 'P';
      case PinDir::Ground:
        return 'G';
      default:
        return 'B';
      }
    }();
    out += " " + p + ":" + dir;
  }
  out += '\n';
  for (const auto &d : c.devices) {
    const std::string bulk = d.pmos ? "VDD" : "VSS";
    out += d.name + " " + d.drain + " " + d.gate + " " + d.source + " " + bulk +
           " " + d.model + "\n";
  }
  out += ".ENDS\n";
  return out;
}

void write_files(const fs::path &dir, const Design &d, const FuncSpec &spec) {
  fs::create_directories(dir);
  const std::string dut = emit_dut(d, spec.vars);
  std::ofstream(dir / "dut.v") << dut;
  std::ofstream(dir / "ref.v") << emit_ref(spec.func, spec.vars);
  std::ofstream(dir / "miter.v") << emit_miter(spec.vars);
  std::ofstream(dir / "tb.v") << emit_tb(spec.func, spec.vars);
}

VerifyMode parse_verify(const Args &a, VerifyMode def) {
  const std::string v = a.get("verify");
  if (v.empty())
    return def;
  if (v == "none")
    return VerifyMode::None;
  if (v == "yosys")
    return VerifyMode::Yosys;
  if (v == "sim")
    return VerifyMode::Sim;
  if (v == "both")
    return VerifyMode::Both;
  if (v == "static")
    return VerifyMode::Static;
  throw CliError("--verify must be none|yosys|sim|both|static");
}

Json verify_json(const ToolResult &r) {
  Json j;
  j["ok"] = r.ok;
  j["exit_code"] = r.exit_code;
  j["output"] = r.output;
  if (!r.workdir.empty())
    j["workdir"] = r.workdir;
  return j;
}

Verified verify_design(const Design &d, const FuncSpec &spec, VerifyMode mode,
                       bool keep, const fs::path *emit_dir) {
  Verified v;
  const std::string dut = emit_dut(d, spec.vars);
  if (emit_dir)
    write_files(*emit_dir, d, spec);
  Json j;
  if (mode == VerifyMode::Yosys || mode == VerifyMode::Both) {
    const auto r = prove_yosys(dut, spec.func, spec.vars, keep);
    v.yosys = r.ok;
    j["yosys"] = verify_json(r);
  }
  if (mode == VerifyMode::Sim || mode == VerifyMode::Both) {
    const auto r = simulate_verilator(dut, spec.func, spec.vars, keep);
    v.sim = r.ok;
    j["verilator"] = verify_json(r);
  }
  v.json = std::move(j);
  return v;
}

void emit_error(bool want_json, const std::string &message, int code) {
  if (want_json) {
    Json j;
    j["ok"] = false;
    j["error"] = message;
    j["exit_code"] = code;
    std::cout << j.dump() << "\n";
  } else {
    std::cerr << "error: " << message << "\n";
  }
}

Json cells_json(const std::vector<GateInstance> &instances) {
  Json cells = Json::Array{};
  std::string topology;
  for (const auto &c : instances) {
    Json cj;
    cj["type"] = c.type;
    Json ports;
    for (const auto &[k, v] : c.ports)
      ports[k] = v;
    cj["ports"] = std::move(ports);
    cells.push_back(std::move(cj));
    if (!topology.empty())
      topology += " → ";
    topology += c.type;
  }
  return cells;
}

Json multi_design_json(const MultiStageDesign &m) {
  Json j;
  j["kind"] = "static-cmos-multi";
  j["cost"] = m.total;
  j["nfet"] = m.nfet;
  j["pfet"] = m.pfet;
  j["inverters"] = Json::Array{};
  j["branches"] = Json::Array{};
  j["kappa_linear"] = true;
  j["single_stage"] = false;
  j["cells"] = cells_json(m.cells);
  std::string topology;
  for (const auto &c : m.cells) {
    if (!topology.empty())
      topology += " → ";
    topology += c.type;
  }
  j["topology"] = topology;
  return j;
}

Json multi_design_json(const MultiStageMultiDesign &m) {
  Json j;
  j["kind"] = "static-cmos-multi";
  j["cost"] = m.total;
  j["nfet"] = m.nfet;
  j["pfet"] = m.pfet;
  j["inverters"] = Json::Array{};
  j["branches"] = Json::Array{};
  j["kappa_linear"] = true;
  j["single_stage"] = false;
  Json outs = Json::Array{};
  for (const auto &o : m.outputs)
    outs.push_back(o);
  j["outputs"] = std::move(outs);
  j["cells"] = cells_json(m.cells);
  Json truths;
  for (std::size_t i = 0; i < m.outputs.size(); ++i)
    truths[m.outputs[i]] = m.truths.at(i);
  j["truths"] = std::move(truths);
  std::string topology;
  for (const auto &c : m.cells) {
    if (!topology.empty())
      topology += " → ";
    topology += c.type;
  }
  j["topology"] = topology;
  return j;
}

// Fan-in of a single-stage static gate is the number of distinct input
// signals its PDN uses -- not the longest product term.  Three parallel
// single-literal terms is still a 3-input NOR.

} // namespace dcstudy
