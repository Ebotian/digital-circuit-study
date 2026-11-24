#include "cli/cli_common.hpp"
#include "cli/cli_includes.hpp"

namespace fs = std::filesystem;

using namespace dcstudy;

namespace dcstudy {

StaticCheck check_static_cmos(const CmosDesign &d, const FuncSpec &spec) {
  const Subckt cell = build_static_cmos_subckt(d, spec.vars);
  const SwitchAnalysis a =
      analyze_switch(cell, spec.vars, {"Y"}, true, "VDD", "VSS");
  StaticCheck c;
  for (const auto &row : a.rows)
    c.truth.push_back(logic_symbol(row.values[0]));
  c.target = spec.func.truth_string();
  c.ok = c.truth == c.target;
  return c;
}

int static_gate_fanin(const CmosDesign &d) {
  std::uint64_t used = 0;
  for (const auto &c : d.pull_down.terms)
    used |= c.care;
  return static_cast<int>(__builtin_popcountll(used));
}

int cmd_design_static(const Args &a, const FuncSpec &spec,
                      const SpaceOptions &opt, VerifyMode vmode, bool json) {
  const CmosDesign d = synthesize_cmos(spec.func, spec.vars);
  const int max_fanin = std::stoi(a.get("max-fanin", "0"));
  if (max_fanin != 0 && (max_fanin < 2 || max_fanin > 4))
    throw CliError("--max-fanin must be 0 or 2..4");
  const int single_fanin = static_gate_fanin(d);
  const bool fanin_ok = max_fanin == 0 || single_fanin <= max_fanin;
  const bool single_ok =
      d.realizable && d.total <= opt.max_cost && !d.pull_down.terms.empty() &&
      std::none_of(d.pull_down.terms.begin(), d.pull_down.terms.end(),
                   [](const Cube &c) { return c.literals() == 0; }) &&
      fanin_ok;
  if (!single_ok) {
    if (vmode != VerifyMode::None && vmode != VerifyMode::Static)
      throw CliError("--static-cmos supports --verify static|none");
    const MultiStageDesign m =
        synthesize_multistage_static(spec.func, spec.vars, max_fanin);
    if (!m.ok || !m.verified) {
      if (json) {
        Json j;
        j["ok"] = false;
        j["command"] = "design.min";
        j["space"] = "static-cmos";
        j["feasible"] = false;
        j["message"] =
            m.reason.empty() ? "multi-stage synthesis failed" : m.reason;
        j["truth"] = spec.func.truth_string();
        j["single_stage"] = false;
        j["anf_affine"] = spec.func.is_anf_affine();
        std::cout << j.dump() << "\n";
      } else {
        std::cout << "static complementary CMOS: multi-stage failed: "
                  << m.reason << "\n";
      }
      return 1;
    }
    if (m.total > opt.max_cost) {
      if (json) {
        Json j;
        j["ok"] = false;
        j["command"] = "design.min";
        j["space"] = "static-cmos";
        j["feasible"] = false;
        j["message"] = "multi-stage static CMOS needs " +
                       std::to_string(m.total) + "T, over --max-cost";
        j["optimum"] = m.total;
        std::cout << j.dump() << "\n";
      } else {
        std::cout << "multi-stage static CMOS needs " << m.total
                  << "T, over --max-cost\n";
      }
      return 1;
    }

    Json design = multi_design_json(m);
    Json j;
    j["ok"] = true;
    j["command"] = "design.min";
    j["space"] = "static-cmos";
    j["feasible"] = true;
    j["single_stage"] = false;
    j["multi_stage"] = true;
    j["optimum"] = m.total;
    j["truth"] = spec.func.truth_string();
    j["kappa_linear"] = true;
    j["pun_dual"] = true;
    j["monotone_complement"] = false;
    j["anf_affine"] = spec.func.is_anf_affine();
    j["designs"] = [&] {
      Json arr = Json::Array{};
      arr.push_back(design);
      return arr;
    }();
    j["verification"] = [&] {
      Json v;
      v["static"] = [&] {
        Json s;
        s["ok"] = m.verified;
        s["truth"] = m.truth;
        s["target"] = spec.func.truth_string();
        s["mode"] = "z3 switch-level (flattened standard cells)";
        return s;
      }();
      return v;
    }();

    if (a.has("emit")) {
      const fs::path dir = fs::path(a.get("emit")) /
                           ("static_multi_" + std::to_string(m.total) + "T");
      fs::create_directories(dir);
      std::ofstream(dir / "static-cmos.json") << design.dump() << "\n";
      std::ofstream(dir / "gate-level.v") << m.verilog;
      std::ofstream(dir / "netlist.json") << m.json;
      std::ofstream(dir / "flattened.cdl") << m.cdl;
      j["files"] = [&] {
        Json v = Json::Array{};
        v.push_back((dir / "static-cmos.json").string());
        v.push_back((dir / "gate-level.v").string());
        v.push_back((dir / "netlist.json").string());
        v.push_back((dir / "flattened.cdl").string());
        return v;
      }();
    }

    if (json) {
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "multi-stage static CMOS: " << m.total << "T (" << m.nfet
                << " NMOS + " << m.pfet << " PMOS)\n";
      std::cout << "  cells:";
      for (const auto &c : m.cells)
        std::cout << " " << c.type;
      std::cout << "\n  switch-level: " << (m.verified ? "PASS" : "FAIL")
                << "  truth=" << m.truth << "\n";
      if (a.has("emit"))
        std::cout << "  emitted: " << a.get("emit") << "\n";
    }
    return 0;
  }
  if (!d.realizable) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.min";
      j["space"] = "static-cmos";
      j["feasible"] = false;
      j["message"] = d.reason;
      j["truth"] = spec.func.truth_string();
      j["monotone_complement"] = false;
      j["kappa_linear"] = false;
      j["anf_affine"] = spec.func.is_anf_affine();
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "static complementary CMOS: no (" << d.reason << ")\n";
    }
    return 1;
  }
  if (d.pull_down.terms.empty() ||
      std::any_of(d.pull_down.terms.begin(), d.pull_down.terms.end(),
                  [](const Cube &c) { return c.literals() == 0; })) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.min";
      j["space"] = "static-cmos";
      j["feasible"] = false;
      j["message"] =
          "constant functions are not supported by the static CMOS emitter";
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "static complementary CMOS: constant functions are not "
                   "supported\n";
    }
    return 1;
  }
  if (d.total > opt.max_cost) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.min";
      j["space"] = "static-cmos";
      j["feasible"] = false;
      j["message"] =
          "static CMOS needs " + std::to_string(d.total) + "T, over --max-cost";
      j["optimum"] = d.total;
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "static CMOS needs " << d.total << "T, over --max-cost\n";
    }
    return 1;
  }
  if (vmode != VerifyMode::None && vmode != VerifyMode::Static)
    throw CliError("--static-cmos supports --verify static|none");

  Json verification;
  bool check_ok = true;
  if (vmode == VerifyMode::Static) {
    const StaticCheck c = check_static_cmos(d, spec);
    check_ok = c.ok;
    verification["static"] = [&] {
      Json v;
      v["ok"] = c.ok;
      v["truth"] = c.truth;
      v["target"] = c.target;
      v["mode"] = "z3 switch-level";
      return v;
    }();
  }

  Json design = static_design_json(d, spec.vars);
  Json j;
  j["ok"] = check_ok;
  j["command"] = "design.min";
  j["space"] = "static-cmos";
  j["feasible"] = true;
  j["single_stage"] = true;
  j["multi_stage"] = false;
  j["optimum"] = d.total;
  j["truth"] = spec.func.truth_string();
  j["monotone_complement"] = true;
  j["kappa_linear"] = true;
  j["pun_dual"] = true;
  j["anf_affine"] = spec.func.is_anf_affine();
  j["designs"] = [&] {
    Json arr = Json::Array{};
    arr.push_back(design);
    return arr;
  }();
  if (vmode != VerifyMode::None)
    j["verification"] = verification;

  if (a.has("emit")) {
    const fs::path dir =
        fs::path(a.get("emit")) / ("static_" + std::to_string(d.total) + "T");
    fs::create_directories(dir);
    std::ofstream(dir / "static-cmos.json") << design.dump() << "\n";
    std::ofstream(dir / "static-cmos.cdl")
        << subckt_to_cdl(build_static_cmos_subckt(d, spec.vars));
    j["files"] = [&] {
      Json v = Json::Array{};
      v.push_back((dir / "static-cmos.json").string());
      v.push_back((dir / "static-cmos.cdl").string());
      return v;
    }();
  }

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "static complementary CMOS: " << d.total << "T (" << d.nfet
              << " NMOS + " << d.pfet << " PMOS)\n";
    std::cout << "  PDN = " << d.pull_down.expression(spec.vars) << "\n";
    std::cout << "  PUN = " << pull_up_expression(d, spec.vars) << "\n";
    if (vmode == VerifyMode::Static)
      std::cout << "  switch-level: " << (check_ok ? "PASS" : "FAIL")
                << "  truth=" << verification["static"]["truth"].as_string()
                << "\n";
    if (a.has("emit"))
      std::cout << "  emitted: " << a.get("emit") << "\n";
  }
  return check_ok ? 0 : 1;
}

int cmd_design_template(const Args &a, const FuncSpec &spec) {
  const BddSwitchDesign b = synthesize_bdd_switch_best(spec.func, spec.vars);
  if (!b.ok || !b.verified) {
    if (a.is("json")) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.template";
      j["reason"] = b.reason;
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "template seed failed: " << b.reason << "\n";
    }
    return 1;
  }
  std::vector<int> target;
  target.reserve(spec.func.num_minterms());
  for (std::uint32_t m = 0; m < spec.func.num_minterms(); ++m)
    target.push_back(spec.func.value(m) ? 1 : 0);
  const NetworkTemplate t = template_from_graph(b.graph, b.inverters);
  const TemplateResult r = optimize_template(t, spec.vars, {target});
  const bool json = a.is("json");
  if (!r.ok || !r.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.template";
      j["reason"] = r.reason;
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "template optimization failed: " << r.reason << "\n";
    }
    return 1;
  }
  Json j;
  j["ok"] = true;
  j["command"] = "design.template";
  j["model"] = "fixed candidate graph, optional edge variables";
  j["total"] = r.total;
  j["edge_cost"] = r.edge_cost;
  j["inverter_cost"] = r.inverter_cost;
  j["proof"] = r.proof;
  j["verified"] = r.verified;
  j["selected_edges"] = [&] {
    Json v = Json::Array{};
    for (int e : r.selected_edges)
      v.push_back(e);
    return v;
  }();
  j["graph"] = [&] {
    Json graph;
    Json nets = Json::Array{};
    for (const auto &n : r.graph.nets)
      nets.push_back(n);
    graph["nets"] = std::move(nets);
    Json edges = Json::Array{};
    for (const auto &e : r.graph.edges) {
      Json ej;
      ej["from"] = e.from;
      ej["to"] = e.to;
      ej["kind"] = switch_name(e.kind);
      ej["lit"] = e.lit;
      ej["cost"] = e.cost;
      ej["strong"] = e.strong;
      edges.push_back(std::move(ej));
    }
    graph["edges"] = std::move(edges);
    Json outs = Json::Array{};
    for (int o : r.graph.outputs)
      outs.push_back(o);
    graph["outputs"] = std::move(outs);
    return graph;
  }();
  if (json)
    std::cout << j.dump() << "\n";
  else
    std::cout << "template-optimized switch graph: " << r.total << "T\n"
              << "  edges=" << r.edge_cost << "T  inverters=" << r.inverter_cost
              << "T  proof=" << (r.proof ? "exhaustive" : "greedy") << "\n";
  return 0;
}

int cmd_design_path(const Args &a, const FuncSpec &spec) {
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", "8"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  const PathSynthesisResult d =
      synthesize_path_variables(spec.func, spec.vars, opt, a.is("tri-state"));
  const bool json = a.is("json");
  if (!d.ok || !d.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.path";
      j["reason"] = d.reason;
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "design path failed: " << d.reason << "\n";
    }
    return 1;
  }
  Json j;
  j["ok"] = true;
  j["command"] = "design.path";
  j["model"] = "path-variable cover with shared switch prefixes";
  j["total"] = d.total;
  j["edge_cost"] = d.edge_cost;
  j["inverter_cost"] = d.inverter_cost;
  j["proof"] = d.proof;
  j["verified"] = d.verified;
  j["selected"] = [&] {
    Json v = Json::Array{};
    for (int s : d.selected)
      v.push_back(s);
    return v;
  }();
  Json graph;
  Json nets = Json::Array{};
  for (const auto &n : d.graph.nets)
    nets.push_back(n);
  graph["nets"] = std::move(nets);
  Json edges = Json::Array{};
  for (const auto &e : d.graph.edges) {
    Json ej;
    ej["from"] = e.from;
    ej["to"] = e.to;
    ej["kind"] = switch_name(e.kind);
    ej["lit"] = e.lit;
    ej["strong"] = e.strong;
    edges.push_back(std::move(ej));
  }
  graph["edges"] = std::move(edges);
  Json sources = Json::Array{};
  for (int s : d.graph.source_ids)
    sources.push_back(s);
  graph["sources"] = std::move(sources);
  graph["output"] = d.graph.output;
  j["graph"] = std::move(graph);
  if (json)
    std::cout << j.dump() << "\n";
  else
    std::cout << "path-variable switch design: " << d.total << "T\n"
              << "  edges=" << d.edge_cost << "T  inverters=" << d.inverter_cost
              << "T\n"
              << "  proof: exhaustive within candidate path set\n";
  return 0;
}

int cmd_design_bdd(const Args &a, const FuncSpec &spec) {
  const BddSwitchDesign d = synthesize_bdd_switch_best(spec.func, spec.vars);
  const bool json = a.is("json");
  if (!d.ok || !d.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.bdd";
      j["reason"] = d.reason;
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "design bdd failed: " << d.reason << "\n";
    }
    return 1;
  }
  Json j;
  j["ok"] = true;
  j["command"] = "design.bdd";
  j["model"] = "BDD-derived switch DAG";
  j["total"] = d.total;
  j["nodes"] = d.nodes;
  j["inverters"] = d.inverters;
  j["order"] = [&] {
    Json v = Json::Array{};
    for (int i : d.order)
      v.push_back(spec.vars.at(static_cast<std::size_t>(i)));
    return v;
  }();
  j["verified"] = d.verified;
  j["truth"] = d.truths.empty() ? "" : d.truths[0];
  Json graph;
  Json nets = Json::Array{};
  for (const auto &n : d.graph.nets)
    nets.push_back(n);
  graph["nets"] = std::move(nets);
  Json edges = Json::Array{};
  for (const auto &e : d.graph.edges) {
    Json ej;
    ej["from"] = e.from;
    ej["to"] = e.to;
    ej["kind"] = switch_name(e.kind);
    ej["lit"] = e.lit;
    ej["strong"] = e.strong;
    edges.push_back(std::move(ej));
  }
  graph["edges"] = std::move(edges);
  Json sources = Json::Array{};
  for (int s : d.graph.source_ids)
    sources.push_back(s);
  graph["sources"] = std::move(sources);
  graph["output"] = d.graph.output;
  j["graph"] = std::move(graph);
  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "BDD switch DAG: " << d.total << "T\n";
    std::cout << "  nodes=" << d.nodes << "  input inverters=" << d.inverters
              << "\n";
    std::cout << "  switch-level drivers: PASS\n";
  }
  return 0;
}

int cmd_design(const std::string &sub, int argc, char **argv) {
  if (sub == "bdd") {
    const Args a = parse_args(argc, argv, 3);
    return cmd_design_bdd(a, get_spec(a));
  }
  if (sub == "path") {
    const Args a = parse_args(argc, argv, 3);
    return cmd_design_path(a, get_spec(a));
  }
  if (sub == "template") {
    const Args a = parse_args(argc, argv, 3);
    return cmd_design_template(a, get_spec(a));
  }
  if (sub != "min")
    throw CliError("unknown design subcommand: " + sub);
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  const FuncSpec spec = get_spec(a);
  if (spec.vars.size() > 4)
    throw CliError("design min supports at most 4 variables");
  const bool static_mode = a.is("static-cmos");
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", static_mode ? "32" : "8"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  if (opt.max_cost < 1 || opt.max_cost > 32)
    throw CliError("--max-cost out of range");
  if (opt.max_chain < 1 || opt.max_chain > 8)
    throw CliError("--max-chain out of range");
  const unsigned long limit_raw =
      std::stoul(a.get("limit", a.is("all") ? "64" : "8"));
  if (limit_raw == 0 || limit_raw > 1024)
    throw CliError("--limit must be in 1..1024");
  const std::size_t limit = static_cast<std::size_t>(limit_raw);
  const VerifyMode vmode =
      parse_verify(a, static_mode ? VerifyMode::Static : VerifyMode::Yosys);
  // --verify static is a switch-level proof of the single-stage static gate;
  // without --static-cmos there is no such gate, and silently returning an
  // unverified design would be a false green.
  if (!static_mode && vmode == VerifyMode::Static)
    throw CliError("--verify static requires --static-cmos");
  if (static_mode)
    return cmd_design_static(a, spec, opt, vmode, json);

  const auto result =
      synthesize_min(spec.vars, target_of(spec), opt, limit, a.is("canonical"));

  Json j;
  j["ok"] = result.feasible;
  j["command"] = "design.min";
  j["vars"] = [&] {
    Json v = Json::Array{};
    for (const auto &s : spec.vars)
      v.push_back(s);
    return v;
  }();
  j["truth"] = spec.func.truth_string();
  j["branch_count"] = result.branch_count;
  j["optimum"] = result.optimum;
  j["feasible"] = result.feasible;
  if (!result.message.empty())
    j["message"] = result.message;

  Json designs = Json::Array{};
  bool all_verified = true;
  for (std::size_t i = 0; i < result.designs.size(); ++i) {
    const auto &d = result.designs[i];
    fs::path emit_dir;
    if (a.has("emit"))
      emit_dir = fs::path(a.get("emit")) / ("design_" + std::to_string(i) +
                                            "_" + std::to_string(d.cost) + "T");
    const auto v = verify_design(d, spec, vmode, a.is("keep"),
                                 a.has("emit") ? &emit_dir : nullptr);
    if ((vmode == VerifyMode::Yosys || vmode == VerifyMode::Both) && !v.yosys)
      all_verified = false;
    if ((vmode == VerifyMode::Sim || vmode == VerifyMode::Both) && !v.sim)
      all_verified = false;
    Json dj = design_json(d, spec.vars);
    if (!d.branches.empty()) {
      const DriverProfile dp =
          analyze_design_drivers(d, spec.vars, target_of(spec));
      dj["drivers"] = driver_profile_json(dp);
      if (!dp.valid)
        all_verified = false;
    }
    if (vmode != VerifyMode::None)
      dj["verification"] = v.json;
    if (a.has("emit"))
      dj["files"] = emit_dir.string();
    designs.push_back(std::move(dj));
  }
  j["designs"] = std::move(designs);
  j["ok"] = result.feasible && all_verified;

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "issue: " << spec.description
              << "  truth=" << spec.func.truth_string() << "\n";
    std::cout << "space: " << result.branch_count << " usable branches\n";
    if (!result.feasible) {
      std::cout << "no design: " << result.message << "\n";
    } else {
      std::cout << "optimum: " << result.optimum
                << "T  designs=" << result.designs.size() << "\n";
      for (std::size_t i = 0; i < result.designs.size(); ++i) {
        const auto &d = result.designs[i];
        std::cout << "  [" << i << "] " << d.cost
                  << "T  inverters=" << d.inverter_names(spec.vars)
                  << "  branches=" << d.branches.size() << "\n";
        for (const auto &b : d.branches)
          std::cout << "       " << b.topo(spec.vars) << "\n";
        if (vmode == VerifyMode::Yosys || vmode == VerifyMode::Both)
          std::cout << "       yosys: "
                    << (j["designs"].array()
                                    .at(i)
                                    .at("verification")
                                    .at("yosys")
                                    .at("ok")
                                    .dump(0) == "true"
                            ? "PASS"
                            : "FAIL")
                    << "\n";
        if (vmode == VerifyMode::Sim || vmode == VerifyMode::Both)
          std::cout << "       verilator: "
                    << (j["designs"].array()
                                    .at(i)
                                    .at("verification")
                                    .at("verilator")
                                    .at("ok")
                                    .dump(0) == "true"
                            ? "PASS"
                            : "FAIL")
                    << "\n";
      }
    }
  }
  if (!result.feasible)
    return 1;
  return all_verified ? 0 : 1;
}

} // namespace dcstudy
