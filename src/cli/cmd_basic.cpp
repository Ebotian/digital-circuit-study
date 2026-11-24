#include "cli/cli_common.hpp"
#include "cli/cli_includes.hpp"

namespace fs = std::filesystem;

using namespace dcstudy;

namespace dcstudy {

int cmd_bool(const std::string &sub, int argc, char **argv) {
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
      for (const auto &v : spec.vars)
        vars.push_back(v);
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
      std::cout << "monotone: " << (spec.func.is_monotone() ? "yes" : "no")
                << "\n";
      std::cout << "self-dual: " << (spec.func.is_self_dual() ? "yes" : "no")
                << "\n";
      std::cout << "single-stage CMOS: " << (realizable ? "yes" : "no") << "\n";
      std::cout << "ANF: " << spec.func.anf_string(spec.vars) << "\n";
    }
    return 0;
  }
  if (sub == "count") {
    const int n = std::stoi(a.need("n"));
    if (n < 0 || n > 6)
      throw CliError("--n must be in 0..6");
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

int cmd_cmos(const std::string &sub, int argc, char **argv) {
  const Args a = parse_args(argc, argv, 3);
  if (sub != "synth")
    throw CliError("unknown cmos subcommand: " + sub);
  const bool json = a.is("json");
  const FuncSpec spec = get_spec(a);
  const CmosDesign d = synthesize_cmos(spec.func, spec.vars);
  if (json) {
    Json j;
    j["ok"] = true;
    j["command"] = "cmos.synth";
    j["realizable"] = d.realizable;
    j["kappa_linear"] = d.realizable;
    j["anf_affine"] = spec.func.is_anf_affine();
    j["vars"] = [&] {
      Json v = Json::Array{};
      for (const auto &s : spec.vars)
        v.push_back(s);
      return v;
    }();
    j["truth"] = spec.func.truth_string();
    if (d.realizable) {
      j["pull_down"] = d.pull_down.expression(spec.vars);
      j["pull_up"] = pull_up_expression(d, spec.vars);
      j["monotone_complement"] = true;
      j["terms"] = static_cast<std::int64_t>(d.pull_down.terms.size());
      j["literals"] = d.pull_down.literals;
      j["nfet"] = d.nfet;
      j["pfet"] = d.pfet;
      j["total"] = d.total;
    } else {
      j["reason"] = d.reason;
      j["monotone_complement"] = false;
    }
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "spec: " << spec.description << "\n";
    if (!d.realizable) {
      std::cout << "single-stage complementary CMOS: no (" << d.reason << ")\n";
    } else {
      std::cout << "single-stage complementary CMOS: yes\n";
      std::cout << "pull-down = " << d.pull_down.expression(spec.vars) << "\n";
      std::cout << "pull-up   = " << pull_up_expression(d, spec.vars) << "\n";
      std::cout << "NMOS=" << d.nfet << "  PMOS=" << d.pfet
                << "  total=" << d.total << "T\n";
    }
  }
  return 0;
}

std::vector<std::pair<int, int>> parse_edges(const std::string &s) {
  std::vector<std::pair<int, int>> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    const auto comma = s.find(',', start);
    std::string item = s.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    auto dash = item.find('-');
    if (dash == std::string::npos)
      throw CliError("bad edge: " + item);
    out.emplace_back(std::stoi(item.substr(0, dash)),
                     std::stoi(item.substr(dash + 1)));
    if (comma == std::string::npos)
      break;
    start = comma + 1;
  }
  return out;
}

std::vector<std::vector<int>> parse_rows(const std::string &s) {
  std::vector<std::vector<int>> rows;
  std::size_t start = 0;
  while (start <= s.size()) {
    const auto semi = s.find(';', start);
    std::string row = s.substr(
        start, semi == std::string::npos ? std::string::npos : semi - start);
    std::vector<int> vals;
    std::size_t p = 0;
    while (p <= row.size()) {
      const auto comma = row.find(',', p);
      std::string cell = row.substr(
          p, comma == std::string::npos ? std::string::npos : comma - p);
      vals.push_back(std::stoi(cell));
      if (comma == std::string::npos)
        break;
      p = comma + 1;
    }
    rows.push_back(std::move(vals));
    if (semi == std::string::npos)
      break;
    start = semi + 1;
  }
  return rows;
}

int cmd_math(const std::string &sub, int argc, char **argv) {
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  if (sub == "closure") {
    const auto edges = parse_edges(a.need("edges"));
    int n = 0;
    for (auto [u, v] : edges)
      n = std::max(n, std::max(u, v) + 1);
    if (a.has("n"))
      n = std::stoi(a.get("n"));
    const auto rows = graph_closure(n, edges);
    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "math.closure";
      j["n"] = n;
      Json r = Json::Array{};
      for (const auto &row : rows) {
        Json one = Json::Array{};
        for (int v : row)
          one.push_back(v);
        r.push_back(std::move(one));
      }
      j["rows"] = std::move(r);
      std::cout << j.dump() << "\n";
    } else {
      for (int i = 0; i < n; ++i) {
        std::cout << i << ":";
        for (int v : rows[static_cast<std::size_t>(i)])
          std::cout << " " << v;
        std::cout << "\n";
      }
    }
    return 0;
  }
  if (sub == "gf2") {
    const auto rows = parse_rows(a.need("rows"));
    int ncols = rows.empty() ? 0 : static_cast<int>(rows[0].size());
    if (a.has("ncols"))
      ncols = std::stoi(a.get("ncols"));
    const MathGf2 g = gf2_analysis(rows, ncols);
    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "math.gf2";
      j["rank"] = g.rank;
      j["nullity"] = g.nullity;
      Json ns = Json::Array{};
      for (const auto &v : g.nullspace) {
        Json one = Json::Array{};
        for (int x : v)
          one.push_back(x);
        ns.push_back(std::move(one));
      }
      j["nullspace"] = std::move(ns);
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "rank=" << g.rank << " nullity=" << g.nullity << "\n";
      for (const auto &v : g.nullspace) {
        for (int x : v)
          std::cout << x << " ";
        std::cout << "\n";
      }
    }
    return 0;
  }
  throw CliError("unknown math subcommand: " + sub);
}

int cmd_cdl(const std::string &sub, int argc, char **argv) {
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
      for (const auto &c : lib.cells()) {
        Json cj;
        cj["name"] = c.name;
        cj["devices"] = c.transistor_count();
        Json in = Json::Array{}, out = Json::Array{};
        for (const auto &p : c.inputs())
          in.push_back(p);
        for (const auto &p : c.outputs())
          out.push_back(p);
        cj["inputs"] = std::move(in);
        cj["outputs"] = std::move(out);
        if (!c.eqn.empty())
          cj["eqn"] = c.eqn;
        cells.push_back(std::move(cj));
      }
      j["cells"] = std::move(cells);
      std::cout << j.dump() << "\n";
    } else {
      for (const auto &c : lib.cells()) {
        std::cout << c.name << "  " << c.transistor_count() << "T  in:";
        for (const auto &p : c.inputs())
          std::cout << " " << p;
        std::cout << "  out:";
        for (const auto &p : c.outputs())
          std::cout << " " << p;
        std::cout << "\n";
      }
    }
    return 0;
  }
  if (sub == "solve") {
    const Subckt &cell = lib.cell(a.need("cell"));
    std::vector<std::string> inputs = split_csv(a.get("inputs"));
    if (inputs.empty())
      inputs = cell.inputs();
    const auto outputs = cell.outputs();
    const bool exact = a.is("exact");
    const SwitchAnalysis r =
        analyze_switch(cell, inputs, outputs, exact, a.get("vdd", "VDD"),
                       a.get("vss", "VSS"), a.is("graphblas"));
    std::vector<std::string> truth(outputs.size());
    for (const auto &row : r.rows)
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
      for (const auto &s : inputs)
        in.push_back(s);
      for (const auto &s : outputs)
        out.push_back(s);
      j["inputs"] = std::move(in);
      j["outputs"] = std::move(out);
      Json rows = Json::Array{};
      for (const auto &row : r.rows) {
        Json rj;
        std::string sig;
        for (int b : row.inputs)
          sig.push_back(char('0' + b));
        rj["in"] = sig;
        Json vals = Json::Array{};
        for (auto v : row.values)
          vals.push_back(std::string(1, logic_symbol(v)));
        rj["values"] = std::move(vals);
        if (!row.floating_gates.empty()) {
          Json fg = Json::Array{};
          for (const auto &g : row.floating_gates)
            fg.push_back(g);
          rj["floating_gates"] = std::move(fg);
        }
        if (row.conflict)
          rj["conflict"] = true;
        rows.push_back(std::move(rj));
      }
      j["rows"] = std::move(rows);
      Json tv;
      for (std::size_t oi = 0; oi < outputs.size(); ++oi)
        tv[outputs[oi]] = truth[oi];
      j["truth"] = std::move(tv);
      if (a.has("expect"))
        j["expect_ok"] = expect_ok;
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "cell " << cell.name << ": " << cell.transistor_count()
                << " devices, mode=" << r.mode << "\n";
      std::cout << "  " << join(inputs, " ");
      for (const auto &o : outputs)
        std::cout << " | " << o;
      std::cout << "\n";
      for (const auto &row : r.rows) {
        for (int b : row.inputs)
          std::cout << b << " ";
        std::cout << "|";
        for (auto v : row.values)
          std::cout << " " << logic_symbol(v);
        if (!row.floating_gates.empty()) {
          std::cout << "   floating gate: " << join(row.floating_gates, ",");
        }
        if (row.conflict)
          std::cout << "   conflict";
        std::cout << "\n";
      }
      for (std::size_t oi = 0; oi < outputs.size(); ++oi)
        std::cout << "  " << outputs[oi] << " = " << truth[oi] << "\n";
      if (a.has("expect"))
        std::cout << "  expected: " << (expect_ok ? "match" : "MISMATCH")
                  << "\n";
    }
    return expect_ok ? 0 : 1;
  }
  throw CliError("unknown cdl subcommand: " + sub);
}

int cmd_verify(const std::string &sub, int argc, char **argv) {
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  const FuncSpec spec = get_spec(a);
  std::ifstream in(a.need("dut"));
  if (!in)
    throw CliError("cannot read DUT file: " + a.get("dut"));
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

} // namespace dcstudy
