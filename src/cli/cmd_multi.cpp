#include "cli/cli_common.hpp"
#include "cli/cli_includes.hpp"
#include "cli/cmd_render.hpp"

namespace fs = std::filesystem;
using namespace dcstudy;

namespace fs = std::filesystem;
using namespace dcstudy;

namespace dcstudy {

MultiProblem load_multi_spec(const std::string &path,
                             std::size_t max_vars = 12) {
  std::ifstream in(path);
  if (!in)
    throw CliError("cannot read spec: " + path);
  std::ostringstream os;
  os << in.rdbuf();
  const Json root = Json::parse(os.str());
  MultiProblem s;
  s.title = root.contains("name") ? root.at("name").as_string()
                                  : fs::path(path).stem().string();
  const Json &vars = root.at("inputs");
  if (!vars.is_array() || vars.array().empty())
    throw CliError("spec.inputs must be a non-empty array");
  for (const auto &v : vars.array())
    s.vars.push_back(v.as_string());
  if (s.vars.size() > max_vars)
    throw CliError("multi spec supports at most " + std::to_string(max_vars) +
                   " inputs");
  const Json &outs = root.at("outputs");
  if (!outs.is_array() || outs.array().empty())
    throw CliError("spec.outputs must be a non-empty array");
  for (const auto &o : outs.array()) {
    std::string name, expr;
    if (o.is_object()) {
      name = o.at("name").as_string();
      expr = o.at("expr").as_string();
    } else if (o.is_string()) {
      const std::string &t = o.as_string();
      const auto eq = t.find('=');
      if (eq == std::string::npos)
        throw CliError("output string must be name=expr");
      name = t.substr(0, eq);
      expr = t.substr(eq + 1);
    } else {
      throw CliError("each output must be {name, expr} or name=expr");
    }
    if (name.empty() || name == "VDD" || name == "VSS")
      throw CliError("bad output name: " + name);
    Bits any_mask = Bits::zeros(std::size_t{1} << s.vars.size());
    if (o.is_object() && o.contains("any")) {
      const std::string t = o.at("any").as_string();
      try {
        any_mask = spec_from_truth(t, s.vars).func.bits();
      } catch (const std::exception &e) {
        throw CliError("bad any mask for " + name + ": " + e.what());
      }
      if (any_mask.count() == any_mask.size())
        throw CliError("output " + name + " has no care rows (any is all 1s)");
    }
    if (std::find(s.names.begin(), s.names.end(), name) != s.names.end())
      throw CliError("duplicate output name: " + name);
    if (std::find(s.vars.begin(), s.vars.end(), name) != s.vars.end())
      throw CliError("output collides with input: " + name);
    s.names.push_back(name);
    s.outputs.push_back(
        {spec_from_expr(expr, s.vars).func, std::move(any_mask)});
  }
  return s;
}

// Echo the don't-care rows of a spec into a result object.
void add_any_json(Json &j, const MultiProblem &spec) {
  Json any = Json::Object{};
  bool found = false;
  for (std::size_t i = 0; i < spec.size(); ++i) {
    if (spec.outputs[i].any.none())
      continue;
    any[spec.names[i]] = spec.outputs[i].any.to_binary_string();
    found = true;
  }
  if (found)
    j["any"] = std::move(any);
}

int cmd_multi_template(const Args &a, const MultiProblem &spec) {
  SpaceOptions opt;
  opt.max_cost = 32;
  opt.max_chain = 4;
  opt.allow_tg = !a.is("no-tg");
  const MultiPathSynthesisResult seed =
      synthesize_path_variables_multi(spec, opt, false);
  if (!seed.ok || !seed.verified) {
    if (a.is("json")) {
      Json j;
      j["ok"] = false;
      j["command"] = "multi.template";
      j["reason"] = seed.reason;
      std::cout << j.dump() << "\n";
    }
    return 1;
  }
  std::vector<std::vector<int>> targets;
  for (std::size_t k = 0; k < spec.size(); ++k) {
    const BoolFunc f =
        spec.any(k).none() ? spec.outputs[k].func : spec.completed(k);
    std::vector<int> t;
    for (std::uint32_t m = 0; m < f.num_minterms(); ++m)
      t.push_back(f.value(m) ? 1 : 0);
    targets.push_back(std::move(t));
  }
  const NetworkTemplate tmpl =
      template_from_graph(seed.graph, seed.inverter_cost);
  const TemplateResult r = optimize_template(tmpl, spec.vars, targets);
  const bool json = a.is("json");
  if (!r.ok || !r.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "multi.template";
      j["reason"] = r.reason;
      std::cout << j.dump() << "\n";
    }
    return 1;
  }
  Json j;
  j["ok"] = true;
  j["command"] = "multi.template";
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
  if (json)
    std::cout << j.dump() << "\n";
  else
    std::cout << "multi-output template: " << r.total << "T (" << r.edge_cost
              << "T edges + " << r.inverter_cost << "T inverters)\n";
  return 0;
}

int cmd_multi_path(const Args &a, const MultiProblem &spec) {
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", "64"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  opt.max_ops = std::stoull(a.get("max-ops", std::to_string(kDefaultMaxOps)));
  const MultiPathSynthesisResult r =
      synthesize_path_variables_multi(spec, opt, a.is("tri-state"));
  const bool json = a.is("json");
  std::string fallback_note;
  if ((!r.ok || !r.verified) && !a.is("tri-state")) {
    const StaticExactResult s = synthesize_static_exact_multi(spec);
    if (s.ok && s.verified && s.total > opt.max_cost)
      fallback_note = " (static fallback needs " + std::to_string(s.total) +
                      "T, over --max-cost " + std::to_string(opt.max_cost) +
                      ")";
    if (s.ok && s.verified && s.total <= opt.max_cost) {
      Json j;
      j["ok"] = true;
      j["command"] = "multi.path";
      j["mode"] = "static-exact-fallback";
      j["path_reason"] = r.reason;
      add_any_json(j, spec);
      j["outputs"] = [&] {
        Json v = Json::Array{};
        for (const auto &n : spec.names)
          v.push_back(n);
        return v;
      }();
      j["total"] = s.total;
      j["gate_cost"] = s.gate_cost;
      j["inverter_cost"] = s.inverter_cost;
      j["proof"] = s.proof;
      j["proof_method"] = s.proof_method;
      j["proof_scope"] = "single-stage static CMOS model";
      j["verified"] = s.verified;
      std::cout << j.dump() << "\n";
      return 0;
    }
  }
  if (!r.ok || !r.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "multi.path";
      j["reason"] = r.reason + fallback_note;
      Json edges = Json::Array{};
      for (const auto &e : r.graph.edges) {
        Json ej;
        ej["from"] = e.from;
        ej["to"] = e.to;
        ej["kind"] = switch_name(e.kind);
        ej["lit"] = e.lit;
        edges.push_back(std::move(ej));
      }
      Json nets = Json::Array{};
      for (const auto &n : r.graph.nets)
        nets.push_back(n);
      Json sources = Json::Array{};
      for (int s : r.graph.source_ids)
        sources.push_back(s);
      Json outs = Json::Array{};
      for (int o : r.graph.outputs)
        outs.push_back(o);
      j["graph"] = Json::Object{{"edges", edges},
                                {"nets", nets},
                                {"sources", sources},
                                {"outputs", outs}};
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "multi path failed: " << r.reason << fallback_note << "\n";
    }
    return 1;
  }
  Json j;
  j["ok"] = true;
  j["command"] = "multi.path";
  j["mode"] = a.is("tri-state") ? "tri-state" : "dual-rail";
  add_any_json(j, spec);
  j["model"] =
      a.is("tri-state")
          ? "multi-output path variables with shared switch edges"
          : "PUN/PDN dual-rail path variables with shared switch edges";
  j["total"] = r.total;
  j["edge_cost"] = r.edge_cost;
  j["inverter_cost"] = r.inverter_cost;
  j["proof"] = r.proof;
  j["proof_method"] = r.proof_method;
  j["proof_scope"] = r.proof_scope;
  j["verified"] = r.verified;
  Json outs = Json::Array{};
  for (const auto &o : r.outputs)
    outs.push_back(o);
  j["outputs"] = std::move(outs);
  j["selected"] = [&] {
    Json v = Json::Array{};
    for (int s : r.selected)
      v.push_back(s);
    return v;
  }();
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
    ej["strong"] = e.strong;
    edges.push_back(std::move(ej));
  }
  graph["edges"] = std::move(edges);
  Json sources = Json::Array{};
  for (int s : r.graph.source_ids)
    sources.push_back(s);
  graph["sources"] = std::move(sources);
  Json gouts = Json::Array{};
  for (int o : r.graph.outputs)
    gouts.push_back(o);
  graph["outputs"] = std::move(gouts);
  j["graph"] = std::move(graph);
  if (json)
    std::cout << j.dump() << "\n";
  else
    std::cout << "multi-output path design: " << r.total << "T\n"
              << "  edges=" << r.edge_cost
              << "T  shared inverters=" << r.inverter_cost << "T\n";
  return 0;
}

int cmd_multi_compose(const Args &a, const MultiProblem &spec,
                      const std::vector<MultiProblem> &library) {
  const ComposeResult r = compose_mux2(spec, library);
  const bool json = a.is("json");
  const std::string out_dir = a.get("out");
  if (r.ok && !out_dir.empty()) {
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    std::ofstream(out_dir + "/compose.v") << r.rtl;
    std::ofstream(out_dir + "/mux2.v") << r.atom_rtl;
    std::ofstream(out_dir + "/netlist.v") << r.synth.verilog;
    std::ofstream(out_dir + "/netlist.json") << r.synth.json;
    if (a.is("png")) {
      std::string err;
      if (!render_netlist_files({out_dir + "/compose.v", out_dir + "/mux2.v"},
                                "top", out_dir, true, err))
        throw CliError(err);
    }
  }
  if (json) {
    Json j;
    j["ok"] = r.ok;
    j["command"] = "multi.compose";
    j["mode"] = "bdd-mux2";
    j["title"] = spec.title;
    j["nodes"] = r.nodes;
    j["total"] = r.synth.total;
    j["atom_cost"] = r.synth.atom_cost;
    j["cell_cost"] = r.synth.cell_cost;
    Json atoms = Json::Array{};
    for (const auto &u : r.synth.atoms) {
      Json aj;
      aj["module"] = u.module;
      aj["instances"] = u.instances;
      aj["transistors"] = u.transistors;
      if (!u.parameters.empty())
        aj["parameters"] = u.parameters;
      atoms.push_back(std::move(aj));
    }
    j["atoms"] = std::move(atoms);
    j["verified"] = r.verified;
    if (!r.ok)
      j["reason"] = r.reason;
    std::cout << j.dump() << "\n";
  } else if (!r.ok) {
    std::cerr << "multi compose failed: " << r.reason << "\n";
  } else {
    std::cout << "multi compose (BDD -> mux2): " << r.nodes << " mux2, "
              << r.synth.total << "T (" << r.synth.atom_cost << "T atoms + "
              << r.synth.cell_cost << "T glue)\n  miter: PASS\n";
  }
  return (r.ok && r.verified) ? 0 : 1;
}

int cmd_multi_exact(const Args &a, const MultiProblem &spec) {
  const StaticExactResult r = synthesize_static_exact_multi(spec);
  const bool json = a.is("json");
  if (!r.ok || !r.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "multi.exact";
      j["reason"] = r.reason;
      j["verified"] = r.verified;
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "multi exact failed: " << r.reason << "\n";
    }
    return 1;
  }

  Json j;
  j["ok"] = true;
  j["command"] = "multi.exact";
  j["model"] = "single-stage static complex CMOS with shared input inverters";
  j["title"] = spec.title;
  add_any_json(j, spec);
  j["optimal"] = r.total;
  j["total"] = r.total;
  j["gate_cost"] = r.gate_cost;
  j["inverter_cost"] = r.inverter_cost;
  j["nfet"] = r.nfet;
  j["pfet"] = r.pfet;
  j["proof"] = r.proof;
  j["proof_method"] = r.proof_method;
  j["proof_scope"] = "single-stage static CMOS model";
  j["verified"] = r.verified;
  Json outs = Json::Array{};
  Json truths;
  Json inverted = Json::Array{};
  Json gates = Json::Array{};
  for (std::size_t i = 0; i < spec.vars.size(); ++i)
    if ((r.inverted_mask >> i) & 1U)
      inverted.push_back(spec.vars[i]);
  for (std::size_t k = 0; k < r.outputs.size(); ++k) {
    outs.push_back(r.outputs[k]);
    truths[r.outputs[k]] = r.truths.at(k);
    Json g;
    g["output"] = r.outputs[k];
    g["pdn"] = r.pdn.at(k).expression(spec.vars);
    g["literals"] = r.pdn.at(k).literals;
    g["transistors"] = 2 * r.pdn.at(k).literals;
    gates.push_back(std::move(g));
  }
  j["outputs"] = std::move(outs);
  j["truths"] = std::move(truths);
  j["inverted"] = std::move(inverted);
  j["gates"] = std::move(gates);

  const std::string out_dir = a.get("out");
  if (!out_dir.empty()) {
    const fs::path dir(out_dir);
    fs::create_directories(dir);
    std::ofstream(dir / "flattened.cdl") << r.cdl;
    std::ofstream(dir / "exact.json") << j.dump() << "\n";
    Json files = Json::Array{};
    for (const char *f : {"flattened.cdl", "exact.json"})
      files.push_back((dir / f).string());
    j["files"] = std::move(files);
  }

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "multi-output exact static CMOS: " << spec.title << "\n";
    std::cout << "  " << r.total << "T  (" << r.gate_cost << "T gates + "
              << r.inverter_cost << "T shared inverters)\n";
    std::cout << "  " << r.nfet << " NMOS + " << r.pfet << " PMOS\n";
    std::cout << "  inverted inputs:";
    bool any = false;
    for (std::size_t i = 0; i < spec.vars.size(); ++i)
      if ((r.inverted_mask >> i) & 1U) {
        std::cout << " " << spec.vars[i];
        any = true;
      }
    if (!any)
      std::cout << " none";
    std::cout << "\n";
    if (r.proof_method == "analytic_shortcut")
      std::cout
          << "  proof: analytic -- every output is a single cube, so the\n"
             "         minimum PDN literals and inverter set are fixed\n";
    else
      std::cout << "  proof: exhaustive over all 2^" << spec.vars.size()
                << " inverter sets + exact minimum DNF per output\n";
    std::cout << "  switch-level: PASS\n";
  }
  return 0;
}

int cmd_multi(const std::string &sub, int argc, char **argv) {
  const Args a = parse_args(argc, argv, 3);
  if (sub == "compose") {
    const MultiProblem spec = load_multi_spec(a.need("spec"), 16);
    std::vector<MultiProblem> library;
    if (a.has("library")) {
      std::ifstream in(a.get("library"));
      if (!in)
        throw CliError("cannot read library: " + a.get("library"));
      std::ostringstream os;
      os << in.rdbuf();
      const Json arr = Json::parse(os.str());
      if (!arr.is_array())
        throw CliError("--library must be a JSON array of specs");
      int idx = 0;
      static std::atomic<int> counter{0};
      for (const auto &e : arr.array()) {
        const std::string p = (fs::temp_directory_path() /
                               ("dcstudy-lib-" + std::to_string(counter++) +
                                "-" + std::to_string(idx++) + ".json"))
                                  .string();
        {
          std::ofstream(p) << e.dump();
        }
        library.push_back(load_multi_spec(p, 16));
        fs::remove(p);
      }
    }
    return cmd_multi_compose(a, spec, library);
  }
  if (sub == "exact") {
    const MultiProblem spec = load_multi_spec(a.need("spec"));
    return cmd_multi_exact(a, spec);
  }
  if (sub == "path") {
    const MultiProblem spec = load_multi_spec(a.need("spec"));
    return cmd_multi_path(a, spec);
  }
  if (sub == "template") {
    const MultiProblem spec = load_multi_spec(a.need("spec"));
    return cmd_multi_template(a, spec);
  }
  if (sub != "synth")
    throw CliError("unknown multi subcommand: " + sub);
  if (!a.has("spec") && !(a.has("rtl") && a.has("ref")))
    throw CliError("multi synth needs --spec (or --rtl with --ref)");
  const MultiProblem spec =
      a.has("spec") ? load_multi_spec(a.get("spec")) : MultiProblem{};
  const int max_fanin = std::stoi(a.get("max-fanin", "0"));
  if (max_fanin != 0 && (max_fanin < 2 || max_fanin > 4))
    throw CliError("--max-fanin must be 0 or 2..4");
  if (a.has("rtl")) {
    if (!a.has("atoms"))
      throw CliError("--rtl needs --atoms FILE (module library)");
    // --atoms - reads the module emitted by a previous stage:
    //   dcstudy ... --emit-atom pe42 --json | dcstudy ... --atoms -
    std::string atoms_path = a.get("atoms");
    std::string temp_atoms;
    if (atoms_path == "-") {
      std::ostringstream in;
      in << std::cin.rdbuf();
      const Json prev = Json::parse(in.str());
      const std::string lib = prev.contains("atom_library")
                                  ? prev.at("atom_library").as_string()
                                  : (prev.contains("atom_verilog")
                                         ? prev.at("atom_verilog").as_string()
                                         : std::string());
      if (lib.empty())
        throw CliError(
            "--atoms - expects a piped 'multi synth --emit-atom' result");
      static std::atomic<int> counter{0};
      temp_atoms =
          (fs::temp_directory_path() /
           ("dcstudy-atom-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(counter++) + ".v"))
              .string();
      std::ofstream(temp_atoms) << lib;
      atoms_path = temp_atoms;
    }
    const RtlSynthResult r = synthesize_rtl_atoms(
        a.get("rtl"), atoms_path, spec, a.get("ref"), max_fanin);
    std::string atoms_src;
    {
      std::ifstream in(atoms_path);
      std::ostringstream os;
      os << in.rdbuf();
      atoms_src = os.str();
    }
    if (!temp_atoms.empty())
      fs::remove(temp_atoms);
    const bool ok = r.ok && r.verified;
    const std::string out_dir = a.get("out");
    if (!out_dir.empty() && r.ok) {
      std::error_code ec;
      fs::create_directories(out_dir, ec);
      std::ofstream(out_dir + "/netlist.v") << r.verilog;
      std::ofstream(out_dir + "/netlist.json") << r.json;
    }
    const std::string atom_name = a.get("emit-atom");
    std::string atom_src;
    if (!atom_name.empty()) {
      atom_src = a.get("rtl");
      std::ifstream in(atom_src);
      std::ostringstream os;
      os << in.rdbuf();
      atom_src = os.str();
      const auto pos = atom_src.find("module top");
      if (pos != std::string::npos)
        atom_src.replace(pos, std::string("module top").size(),
                         "module " + atom_name);
    }
    if (a.is("json")) {
      Json j;
      j["ok"] = ok;
      j["command"] = "multi.synth";
      j["mode"] = "rtl-atoms";
      j["title"] = spec.title;
      j["total"] = r.total;
      j["atom_cost"] = r.atom_cost;
      j["cell_cost"] = r.cell_cost;
      Json atoms = Json::Array{};
      for (const auto &u : r.atoms) {
        Json aj;
        aj["module"] = u.module;
        aj["instances"] = u.instances;
        aj["transistors"] = u.transistors;
        atoms.push_back(std::move(aj));
      }
      j["atoms"] = std::move(atoms);
      j["verified"] = r.verified;
      if (!atom_src.empty()) {
        j["atom_name"] = atom_name;
        j["atom_verilog"] = atom_src;
        j["atom_library"] =
            atoms_src.empty() ? atom_src : atoms_src + "\n" + atom_src;
        j["atom_transistors"] = r.total;
      }
      if (!ok)
        j["reason"] = r.reason;
      std::cout << j.dump() << "\n";
    } else if (!ok) {
      std::cerr << "multi synth (rtl) failed: " << r.reason << "\n";
    } else {
      std::cout << "multi synth (RTL + atoms): " << r.total << "T ("
                << r.atom_cost << "T atoms + " << r.cell_cost << "T glue)\n";
      for (const auto &u : r.atoms)
        std::cout << "  " << u.module
                  << (u.parameters.empty() ? "" : "(" + u.parameters + ")")
                  << " x" << u.instances << "  " << u.transistors << "T each\n";
      std::cout << "  miter: PASS\n";
    }
    return ok ? 0 : 1;
  }
  const MultiStageMultiDesign m =
      synthesize_multistage_static_multi(spec, max_fanin);
  const bool json = a.is("json");
  if (!m.ok || !m.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "multi.synth";
      j["reason"] = m.reason;
      j["verified"] = m.verified;
      Json truths;
      for (std::size_t i = 0; i < m.outputs.size() && i < m.truths.size(); ++i)
        truths[m.outputs[i]] = m.truths[i];
      j["truths"] = std::move(truths);
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "multi synth failed: " << m.reason << "\n";
      for (std::size_t i = 0; i < m.outputs.size() && i < m.truths.size(); ++i)
        std::cerr << "  " << m.outputs[i] << " = " << m.truths[i] << "\n";
    }
    return 1;
  }

  Json design = multi_design_json(m);
  const std::string out_dir = a.get("out");
  Json j;
  j["ok"] = true;
  j["command"] = "multi.synth";
  j["title"] = spec.title;
  add_any_json(j, spec);
  const std::string atom_name = a.get("emit-atom");
  if (!atom_name.empty() && !m.rtl.empty()) {
    // Emit the behavioural module (not the mapped netlist) so the next stage
    // can both black-box it for mapping and use it in the miter.
    std::string src = m.rtl;
    const auto pos = src.find("module top");
    if (pos != std::string::npos)
      src.replace(pos, std::string("module top").size(), "module " + atom_name);
    j["atom_name"] = atom_name;
    j["atom_verilog"] = src;
    j["atom_library"] = src;
    j["atom_transistors"] = m.total;
  }
  j["total"] = m.total;
  j["nfet"] = m.nfet;
  j["pfet"] = m.pfet;
  j["cells"] = static_cast<int>(m.cells.size());
  Json outs = Json::Array{};
  for (const auto &o : m.outputs)
    outs.push_back(o);
  j["outputs"] = std::move(outs);
  Json truths;
  for (std::size_t i = 0; i < m.outputs.size(); ++i)
    truths[m.outputs[i]] = m.truths[i];
  j["truths"] = truths;
  j["verified"] = m.verified;
  j["design"] = design;

  if (!out_dir.empty()) {
    const fs::path dir(out_dir);
    fs::create_directories(dir);
    std::ofstream(dir / "gate-level.v") << m.verilog;
    std::ofstream(dir / "netlist.json") << m.json;
    std::ofstream(dir / "flattened.cdl") << m.cdl;
    std::ofstream(dir / "design.json") << design.dump() << "\n";
    Json files = Json::Array{};
    for (const char *f :
         {"gate-level.v", "netlist.json", "flattened.cdl", "design.json"})
      files.push_back((dir / f).string());
    j["files"] = std::move(files);

    Json req;
    req["op"] = "render";
    req["out"] = dir.string();
    req["vars"] = [&] {
      Json v = Json::Array{};
      for (const auto &s : spec.vars)
        v.push_back(s);
      return v;
    }();
    req["title"] = spec.title;
    req["truth"] = m.truths.empty() ? "" : m.truths.front();
    req["truths"] = truths;
    req["outputs"] = [&] {
      Json v = Json::Array{};
      for (const auto &o : m.outputs)
        v.push_back(o);
      return v;
    }();
    req["png"] = a.is("png");
    req["logic"] = false;
    req["designs"] = Json::Array{design};
    const Json resp = render_call(req);
    j["render"] = resp;
  } else if (a.is("png")) {
    throw CliError("--png requires --out DIR");
  }

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "multi-output static CMOS: " << spec.title << "\n";
    std::cout << "  " << m.total << "T  (" << m.nfet << " NMOS + " << m.pfet
              << " PMOS), " << m.cells.size() << " cells\n";
    std::cout << "  outputs: " << join(m.outputs, ", ") << "\n";
    for (std::size_t i = 0; i < m.outputs.size(); ++i)
      std::cout << "  " << m.outputs[i] << " = " << m.truths[i] << "\n";
    std::cout << "  switch-level: PASS\n";
    if (j.contains("render"))
      std::cout << "  rendered: " << j.at("render").at("index").as_string()
                << "\n";
  }
  return 0;
}

} // namespace dcstudy
