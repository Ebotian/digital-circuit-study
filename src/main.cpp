#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "dcstudy/boolfunc.hpp"
#include "dcstudy/cdl.hpp"
#include "dcstudy/cmos.hpp"
#include "dcstudy/designspace.hpp"
#include "dcstudy/json.hpp"
#include "dcstudy/switchlevel.hpp"
#include "dcstudy/synthesis.hpp"
#include "dcstudy/verilog.hpp"
#include "dcstudy/verify.hpp"

namespace fs = std::filesystem;
using namespace dcstudy;

namespace {

struct CliError : std::runtime_error {
  int code;
  explicit CliError(const std::string& msg, int c = 2)
      : std::runtime_error(msg), code(c) {}
};

struct Args {
  std::map<std::string, std::string> opt;
  std::set<std::string> flag;

  bool has(const std::string& k) const { return opt.count(k) || flag.count(k); }
  bool is(const std::string& k) const { return flag.count(k) != 0; }
  std::string get(const std::string& k, const std::string& def = {}) const {
    auto it = opt.find(k);
    return it == opt.end() ? def : it->second;
  }
  std::string need(const std::string& k) const {
    auto it = opt.find(k);
    if (it == opt.end()) throw CliError("missing --" + k);
    return it->second;
  }
};

const std::set<std::string> kBoolFlags = {
    "json", "all", "canonical", "exact", "keep", "help", "verbose", "no-tg",
    "heavy"};

Args parse_args(int argc, char** argv, int start) {
  Args a;
  for (int i = start; i < argc; ++i) {
    std::string s = argv[i];
    if (s.rfind("--", 0) != 0) {
      throw CliError("unexpected argument: " + s);
    }
    const std::string body = s.substr(2);
    const auto eq = body.find('=');
    const std::string key = body.substr(0, eq);
    if (kBoolFlags.count(key)) {
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

std::vector<std::string> split_csv(const std::string& s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    const auto p = s.find(',', start);
    std::string item = s.substr(start, p == std::string::npos ? std::string::npos
                                                              : p - start);
    auto b = item.find_first_not_of(" \t");
    auto e = item.find_last_not_of(" \t");
    if (b != std::string::npos) out.push_back(item.substr(b, e - b + 1));
    if (p == std::string::npos) break;
    start = p + 1;
  }
  return out;
}

FuncSpec get_spec(const Args& a) {
  const bool has_expr = a.has("expr");
  const bool has_tt = a.has("tt");
  if (has_expr == has_tt) throw CliError("give exactly one of --expr or --tt");
  const auto vars = split_csv(a.get("vars"));
  if (has_expr) return spec_from_expr(a.get("expr"), vars);
  return spec_from_truth(a.get("tt"), vars);
}

std::vector<int> target_of(const FuncSpec& s) {
  std::vector<int> t;
  t.reserve(s.func.num_minterms());
  for (std::uint32_t m = 0; m < s.func.num_minterms(); ++m)
    t.push_back(s.func.value(m) ? 1 : 0);
  return t;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
  std::string out;
  for (const auto& s : v) {
    if (!out.empty()) out += sep;
    out += s;
  }
  return out;
}

Json design_json(const Design& d, const std::vector<std::string>& vars) {
  Json j;
  j["cost"] = d.cost;
  Json invs = Json::Array{};
  for (auto i : d.inv_vars) invs.push_back(vars.at(i));
  j["inverters"] = invs;
  Json branches = Json::Array{};
  for (const auto& b : d.branches) {
    Json bj;
    bj["source"] = b.source;
    bj["cost"] = b.cost;
    Json chain = Json::Array{};
    for (const auto& el : b.chain) {
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

void write_files(const fs::path& dir, const Design& d, const FuncSpec& spec) {
  fs::create_directories(dir);
  const std::string dut = emit_dut(d, spec.vars);
  std::ofstream(dir / "dut.v") << dut;
  std::ofstream(dir / "ref.v") << emit_ref(spec.func, spec.vars);
  std::ofstream(dir / "miter.v") << emit_miter(spec.vars);
  std::ofstream(dir / "tb.v") << emit_tb(spec.func, spec.vars);
}

enum class VerifyMode { None, Yosys, Sim, Both };

VerifyMode parse_verify(const Args& a, VerifyMode def) {
  const std::string v = a.get("verify");
  if (v.empty()) return def;
  if (v == "none") return VerifyMode::None;
  if (v == "yosys") return VerifyMode::Yosys;
  if (v == "sim") return VerifyMode::Sim;
  if (v == "both") return VerifyMode::Both;
  throw CliError("--verify must be none|yosys|sim|both");
}

Json verify_json(const ToolResult& r) {
  Json j;
  j["ok"] = r.ok;
  j["exit_code"] = r.exit_code;
  j["output"] = r.output;
  if (!r.workdir.empty()) j["workdir"] = r.workdir;
  return j;
}

struct Verified {
  bool yosys = false;
  bool sim = false;
  Json json;
};

Verified verify_design(const Design& d, const FuncSpec& spec, VerifyMode mode,
                       bool keep, const fs::path* emit_dir) {
  Verified v;
  const std::string dut = emit_dut(d, spec.vars);
  if (emit_dir) write_files(*emit_dir, d, spec);
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

void emit_error(bool want_json, const std::string& message, int code) {
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

void help() {
  std::cout << R"(dcstudy - Boolean circuits, exact synthesis, formal checks

Usage:
  dcstudy [--json] <command> [options]

Commands:
  bool info      --expr EXPR | --tt BITS [--vars A,B]
  bool count     --n N
  cmos synth     --expr EXPR | --tt BITS [--vars A,B]
  cdl list       [--cdl FILE]
  cdl solve      --cell NAME [--cdl FILE] [--inputs A,B] [--exact] [--expect BITS]
  design min     --expr EXPR | --tt BITS [--max-cost N] [--max-chain N]
                 [--all] [--limit N] [--canonical] [--emit DIR]
                 [--verify none|yosys|sim|both] [--keep]
  verify equiv   --dut FILE --expr EXPR | --tt BITS [--vars A,B] [--keep]
  verify sim     --dut FILE --expr EXPR | --tt BITS [--vars A,B] [--keep]
  pipeline       --funcs 'name=EXPR,...' [--max-cost N] [--out DIR]
                 [--verify none|yosys|sim|both] [--canonical]

Options:
  --json         machine-readable output (also on errors)
  --vars         comma-separated variable order, e.g. A,B
  --max-cost     total transistor budget for a design (default 8)
  --max-chain    maximum switches in a series chain (default 4)
  --all          enumerate all optimal designs (up to --limit)
  --limit        maximum enumerated designs (default 8)
  --canonical    quotient designs by input permutations that preserve f
  --heavy        allow the n=6 Dedekind count (about 40 s and 10 GB)

Exit codes: 0 success, 1 verification/feasibility failure, 2 usage/runtime error.
)";
}

int cmd_bool(const std::string& sub, int argc, char** argv) {
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  if (sub == "info") {
    const FuncSpec spec = get_spec(a);
    const bool realizable = (~spec.func).is_monotone();
    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "bool.info";
      Json vars = Json::Array{};
      for (const auto& v : spec.vars) vars.push_back(v);
      j["vars"] = vars;
      j["truth"] = spec.func.truth_string();
      j["monotone"] = spec.func.is_monotone();
      j["self_dual"] = spec.func.is_self_dual();
      j["realizable"] = realizable;
      j["anf"] = spec.func.anf_string(spec.vars);
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "spec: " << spec.description << "\n";
      std::cout << "vars: " << join(spec.vars, ",") << "\n";
      std::cout << "truth: " << spec.func.truth_string() << "\n";
      std::cout << "monotone: " << (spec.func.is_monotone() ? "yes" : "no") << "\n";
      std::cout << "self-dual: " << (spec.func.is_self_dual() ? "yes" : "no") << "\n";
      std::cout << "single-stage CMOS: " << (realizable ? "yes" : "no") << "\n";
      std::cout << "ANF: " << spec.func.anf_string(spec.vars) << "\n";
    }
    return 0;
  }
  if (sub == "count") {
    const int n = std::stoi(a.need("n"));
    if (n < 0 || n > 6) throw CliError("--n must be in 0..6");
    if (n == 6 && !a.is("heavy"))
      throw CliError(
          "n=6 needs --heavy (about 40 s and 10 GB); n<=5 is instant");
    const auto t0 = std::chrono::steady_clock::now();
    const auto c = count_monotone(static_cast<std::size_t>(n));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "bool.count";
      j["n"] = n;
      j["count"] = c;
      j["ms"] = static_cast<std::int64_t>(ms);
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "n=" << n << "  monotone functions=" << c << "  (" << ms
                << " ms)\n";
    }
    return 0;
  }
  throw CliError("unknown bool subcommand: " + sub);
}

int cmd_cmos(const std::string& sub, int argc, char** argv) {
  const Args a = parse_args(argc, argv, 3);
  if (sub != "synth") throw CliError("unknown cmos subcommand: " + sub);
  const bool json = a.is("json");
  const FuncSpec spec = get_spec(a);
  const CmosDesign d = synthesize_cmos(spec.func, spec.vars);
  if (json) {
    Json j;
    j["ok"] = true;
    j["command"] = "cmos.synth";
    j["realizable"] = d.realizable;
    j["vars"] = [&] {
      Json v = Json::Array{};
      for (const auto& s : spec.vars) v.push_back(s);
      return v;
    }();
    j["truth"] = spec.func.truth_string();
    if (d.realizable) {
      j["pull_down"] = d.pull_down.expression(spec.vars);
      j["terms"] = static_cast<std::int64_t>(d.pull_down.terms.size());
      j["literals"] = d.pull_down.literals;
      j["nfet"] = d.nfet;
      j["pfet"] = d.pfet;
      j["total"] = d.total;
    } else {
      j["reason"] = d.reason;
    }
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "spec: " << spec.description << "\n";
    if (!d.realizable) {
      std::cout << "single-stage complementary CMOS: no (" << d.reason << ")\n";
    } else {
      std::cout << "single-stage complementary CMOS: yes\n";
      std::cout << "pull-down = " << d.pull_down.expression(spec.vars) << "\n";
      std::cout << "NMOS=" << d.nfet << "  PMOS=" << d.pfet
                << "  total=" << d.total << "T\n";
    }
  }
  return 0;
}

int cmd_cdl(const std::string& sub, int argc, char** argv) {
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  const std::string path = a.get("cdl", default_cdl_path());
  const CdlLibrary lib = CdlLibrary::load(path);
  if (sub == "list") {
    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "cdl.list";
      j["cdl"] = path;
      Json cells = Json::Array{};
      for (const auto& c : lib.cells()) {
        Json cj;
        cj["name"] = c.name;
        cj["devices"] = c.transistor_count();
        Json in = Json::Array{}, out = Json::Array{};
        for (const auto& p : c.inputs()) in.push_back(p);
        for (const auto& p : c.outputs()) out.push_back(p);
        cj["inputs"] = std::move(in);
        cj["outputs"] = std::move(out);
        if (!c.eqn.empty()) cj["eqn"] = c.eqn;
        cells.push_back(std::move(cj));
      }
      j["cells"] = std::move(cells);
      std::cout << j.dump() << "\n";
    } else {
      for (const auto& c : lib.cells()) {
        std::cout << c.name << "  " << c.transistor_count() << "T  in:";
        for (const auto& p : c.inputs()) std::cout << " " << p;
        std::cout << "  out:";
        for (const auto& p : c.outputs()) std::cout << " " << p;
        std::cout << "\n";
      }
    }
    return 0;
  }
  if (sub == "solve") {
    const Subckt& cell = lib.cell(a.need("cell"));
    std::vector<std::string> inputs = split_csv(a.get("inputs"));
    if (inputs.empty()) inputs = cell.inputs();
    const auto outputs = cell.outputs();
    const bool exact = a.is("exact");
    const SwitchAnalysis r = analyze_switch(cell, inputs, outputs, exact,
                                           a.get("vdd", "VDD"),
                                           a.get("vss", "VSS"));
    std::vector<std::string> truth(outputs.size());
    for (const auto& row : r.rows)
      for (std::size_t oi = 0; oi < outputs.size(); ++oi)
        truth[oi].push_back(logic_symbol(row.values[oi]));

    bool expect_ok = true;
    if (a.has("expect")) {
      const std::string exp = a.get("expect");
      expect_ok = exp.size() == truth[0].size() && exp == truth[0];
    }

    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "cdl.solve";
      j["cell"] = cell.name;
      j["cdl"] = path;
      j["devices"] = cell.transistor_count();
      j["mode"] = r.mode;
      j["exact"] = r.exact;
      j["assumptions_ok"] = r.assumptions_ok;
      Json in = Json::Array{}, out = Json::Array{};
      for (const auto& s : inputs) in.push_back(s);
      for (const auto& s : outputs) out.push_back(s);
      j["inputs"] = std::move(in);
      j["outputs"] = std::move(out);
      Json rows = Json::Array{};
      for (const auto& row : r.rows) {
        Json rj;
        std::string sig;
        for (int b : row.inputs) sig.push_back(char('0' + b));
        rj["in"] = sig;
        Json vals = Json::Array{};
        for (auto v : row.values) vals.push_back(std::string(1, logic_symbol(v)));
        rj["values"] = std::move(vals);
        if (!row.floating_gates.empty()) {
          Json fg = Json::Array{};
          for (const auto& g : row.floating_gates) fg.push_back(g);
          rj["floating_gates"] = std::move(fg);
        }
        if (row.conflict) rj["conflict"] = true;
        rows.push_back(std::move(rj));
      }
      j["rows"] = std::move(rows);
      Json tv;
      for (std::size_t oi = 0; oi < outputs.size(); ++oi) tv[outputs[oi]] = truth[oi];
      j["truth"] = std::move(tv);
      if (a.has("expect")) j["expect_ok"] = expect_ok;
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "cell " << cell.name << ": " << cell.transistor_count()
                << " devices, mode=" << r.mode << "\n";
      std::cout << "  " << join(inputs, " ");
      for (const auto& o : outputs) std::cout << " | " << o;
      std::cout << "\n";
      for (const auto& row : r.rows) {
        for (int b : row.inputs) std::cout << b << " ";
        std::cout << "|";
        for (auto v : row.values) std::cout << " " << logic_symbol(v);
        if (!row.floating_gates.empty()) {
          std::cout << "   floating gate: " << join(row.floating_gates, ",");
        }
        if (row.conflict) std::cout << "   conflict";
        std::cout << "\n";
      }
      for (std::size_t oi = 0; oi < outputs.size(); ++oi)
        std::cout << "  " << outputs[oi] << " = " << truth[oi] << "\n";
      if (a.has("expect")) std::cout << "  expected: " << (expect_ok ? "match" : "MISMATCH") << "\n";
    }
    return expect_ok ? 0 : 1;
  }
  throw CliError("unknown cdl subcommand: " + sub);
}

int cmd_design(const std::string& sub, int argc, char** argv) {
  if (sub != "min") throw CliError("unknown design subcommand: " + sub);
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  const FuncSpec spec = get_spec(a);
  if (spec.vars.size() > 4) throw CliError("design min supports at most 4 variables");
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", "8"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  if (opt.max_cost < 1 || opt.max_cost > 32) throw CliError("--max-cost out of range");
  if (opt.max_chain < 1 || opt.max_chain > 8) throw CliError("--max-chain out of range");
  const unsigned long limit_raw =
      std::stoul(a.get("limit", a.is("all") ? "64" : "8"));
  if (limit_raw == 0 || limit_raw > 1024)
    throw CliError("--limit must be in 1..1024");
  const std::size_t limit = static_cast<std::size_t>(limit_raw);
  const VerifyMode vmode = parse_verify(a, VerifyMode::Yosys);
  const auto result = synthesize_min(spec.vars, target_of(spec), opt, limit,
                                     a.is("canonical"));

  Json j;
  j["ok"] = result.feasible;
  j["command"] = "design.min";
  j["vars"] = [&] {
    Json v = Json::Array{};
    for (const auto& s : spec.vars) v.push_back(s);
    return v;
  }();
  j["truth"] = spec.func.truth_string();
  j["branch_count"] = result.branch_count;
  j["optimum"] = result.optimum;
  j["feasible"] = result.feasible;
  if (!result.message.empty()) j["message"] = result.message;

  Json designs = Json::Array{};
  bool all_verified = true;
  for (std::size_t i = 0; i < result.designs.size(); ++i) {
    const auto& d = result.designs[i];
    fs::path emit_dir;
    if (a.has("emit"))
      emit_dir = fs::path(a.get("emit")) /
                 ("design_" + std::to_string(i) + "_" + std::to_string(d.cost) + "T");
    const auto v = verify_design(d, spec, vmode, a.is("keep"),
                                 a.has("emit") ? &emit_dir : nullptr);
    if ((vmode == VerifyMode::Yosys || vmode == VerifyMode::Both) && !v.yosys)
      all_verified = false;
    if ((vmode == VerifyMode::Sim || vmode == VerifyMode::Both) && !v.sim)
      all_verified = false;
    Json dj = design_json(d, spec.vars);
    if (vmode != VerifyMode::None) dj["verification"] = v.json;
    if (a.has("emit")) dj["files"] = emit_dir.string();
    designs.push_back(std::move(dj));
  }
  j["designs"] = std::move(designs);
  j["ok"] = result.feasible && all_verified;

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "issue: " << spec.description << "  truth=" << spec.func.truth_string()
              << "\n";
    std::cout << "space: " << result.branch_count << " usable branches\n";
    if (!result.feasible) {
      std::cout << "no design: " << result.message << "\n";
    } else {
      std::cout << "optimum: " << result.optimum << "T  designs="
                << result.designs.size() << "\n";
      for (std::size_t i = 0; i < result.designs.size(); ++i) {
        const auto& d = result.designs[i];
        std::cout << "  [" << i << "] " << d.cost << "T  inverters="
                  << d.inverter_names(spec.vars) << "  branches="
                  << d.branches.size() << "\n";
        for (const auto& b : d.branches) std::cout << "       " << b.topo(spec.vars) << "\n";
        if (vmode == VerifyMode::Yosys || vmode == VerifyMode::Both)
          std::cout << "       yosys: "
                    << (j["designs"].array().at(i).at("verification").at("yosys").at("ok")
                                .dump(0) == "true"
                            ? "PASS"
                            : "FAIL")
                    << "\n";
        if (vmode == VerifyMode::Sim || vmode == VerifyMode::Both)
          std::cout << "       verilator: "
                    << (j["designs"].array().at(i).at("verification").at("verilator").at("ok")
                                .dump(0) == "true"
                            ? "PASS"
                            : "FAIL")
                    << "\n";
      }
    }
  }
  if (!result.feasible) return 1;
  return all_verified ? 0 : 1;
}

int cmd_verify(const std::string& sub, int argc, char** argv) {
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  const FuncSpec spec = get_spec(a);
  std::ifstream in(a.need("dut"));
  if (!in) throw CliError("cannot read DUT file: " + a.get("dut"));
  std::ostringstream os;
  os << in.rdbuf();
  const std::string dut = os.str();
  ToolResult r;
  if (sub == "equiv")
    r = prove_yosys(dut, spec.func, spec.vars, a.is("keep"));
  else if (sub == "sim")
    r = simulate_verilator(dut, spec.func, spec.vars, a.is("keep"));
  else
    throw CliError("unknown verify subcommand: " + sub);
  if (json) {
    Json j;
    j["ok"] = r.ok;
    j["command"] = "verify." + sub;
    j["result"] = verify_json(r);
    std::cout << j.dump() << "\n";
  } else {
    std::cout << (r.ok ? "PASS" : "FAIL") << "  (exit " << r.exit_code << ")\n";
    std::cout << r.output;
  }
  return r.ok ? 0 : 1;
}

int cmd_pipeline(int argc, char** argv) {
  const Args a = parse_args(argc, argv, 2);
  const bool json = a.is("json");
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", "8"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  if (opt.max_cost < 1 || opt.max_cost > 32) throw CliError("--max-cost out of range");
  if (opt.max_chain < 1 || opt.max_chain > 8) throw CliError("--max-chain out of range");
  const VerifyMode vmode = parse_verify(a, VerifyMode::Both);
  const auto funcs = split_csv(a.need("funcs"));
  if (funcs.empty()) throw CliError("empty --funcs");

  Json runs = Json::Array{};
  bool all_ok = true;
  for (const auto& item : funcs) {
    std::string name, expr;
    const auto eq = item.find('=');
    if (eq == std::string::npos) {
      expr = item;
      name = item;
    } else {
      name = item.substr(0, eq);
      expr = item.substr(eq + 1);
    }
    FuncSpec spec;
    try {
      spec = spec_from_expr(expr, {});
    } catch (const std::exception& e) {
      throw CliError("bad function '" + item + "': " + e.what());
    }
    const auto result = synthesize_min(spec.vars, target_of(spec), opt,
                                       a.is("all") ? 64 : 1, a.is("canonical"));
    Json run;
    run["name"] = name;
    run["expr"] = expr;
    run["stats"] = spec.func.truth_string();
    run["feasible"] = result.feasible;
    run["optimum"] = result.optimum;
    run["designs"] = Json::Array{};
    bool ok = result.feasible;
    for (std::size_t i = 0; i < result.designs.size(); ++i) {
      const auto& d = result.designs[i];
      fs::path emit_dir;
      if (a.has("out"))
        emit_dir = fs::path(a.get("out")) / (name + "_" + std::to_string(i));
      const auto v = verify_design(d, spec, vmode, a.is("keep"),
                                   a.has("out") ? &emit_dir : nullptr);
      if (vmode != VerifyMode::None && !v.yosys && !v.sim) ok = false;
      if ((vmode == VerifyMode::Yosys || vmode == VerifyMode::Both) && !v.yosys) ok = false;
      if ((vmode == VerifyMode::Sim || vmode == VerifyMode::Both) && !v.sim) ok = false;
      Json dj = design_json(d, spec.vars);
      if (vmode != VerifyMode::None) dj["verification"] = v.json;
      if (a.has("out")) dj["files"] = emit_dir.string();
      run["designs"].push_back(std::move(dj));
    }
    run["ok"] = ok;
    runs.push_back(std::move(run));
    all_ok = all_ok && ok;
  }

  Json j;
  j["ok"] = all_ok;
  j["command"] = "pipeline";
  j["runs"] = std::move(runs);
  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    for (const auto& r : j["runs"].array()) {
      std::cout << r.at("name").as_string() << ": " << r.at("expr").as_string() << "  ";
      if (r.at("feasible").dump(0) != "true") {
        std::cout << "no design\n";
        continue;
      }
      std::cout << r.at("optimum").dump(0) << "T  designs="
                << r.at("designs").size() << "\n";
      for (const auto& d : r.at("designs").array()) {
        std::cout << "  " << d.at("cost").dump(0) << "T  "
                  << d.at("topology").as_string() << "\n";
        if (d.at("verification").is_object()) {
          if (d.at("verification").at("yosys").is_object())
            std::cout << "    yosys: "
                      << (d.at("verification").at("yosys").at("ok").as_bool() ? "PASS"
                                                                              : "FAIL")
                      << "\n";
          if (d.at("verification").at("verilator").is_object())
            std::cout << "    verilator: "
                      << (d.at("verification").at("verilator").at("ok").as_bool()
                              ? "PASS"
                              : "FAIL")
                      << "\n";
        }
      }
    }
  }
  return all_ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  bool want_json = false;
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--json") want_json = true;
  try {
    if (argc < 2) {
      help();
      return 2;
    }
    // Allow the global --json before the command: move it to the end so the
    // two-word command dispatch (and parse_args) still works.
    if (argc > 2 && std::string(argv[1]) == "--json") {
      char* json_arg = argv[1];
      for (int i = 1; i + 1 < argc; ++i) argv[i] = argv[i + 1];
      argv[argc - 1] = json_arg;
    }
    const std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
      help();
      return 0;
    }
    if (argc < 3) throw CliError("missing subcommand or arguments for " + cmd);
    const std::string sub = argv[2];
    if (cmd == "bool") return cmd_bool(sub, argc, argv);
    if (cmd == "cmos") return cmd_cmos(sub, argc, argv);
    if (cmd == "cdl") return cmd_cdl(sub, argc, argv);
    if (cmd == "design") return cmd_design(sub, argc, argv);
    if (cmd == "verify") return cmd_verify(sub, argc, argv);
    if (cmd == "pipeline") return cmd_pipeline(argc, argv);
    throw CliError("unknown command: " + cmd);
  } catch (const CliError& e) {
    emit_error(want_json, e.what(), e.code);
    return e.code;
  } catch (const std::exception& e) {
    emit_error(want_json, e.what(), 2);
    return 2;
  }
}
