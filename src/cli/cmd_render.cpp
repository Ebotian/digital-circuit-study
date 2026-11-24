#include "cli/cmd_render.hpp"
#include "cli/cli_common.hpp"
#include "cli/cli_includes.hpp"
#include "cli/cmd_design.hpp"
#include "cli/render_client.hpp"
#include <unistd.h>

namespace fs = std::filesystem;

using namespace dcstudy;

namespace dcstudy {

static bool have_program(const std::string &name) {
  const char *path = std::getenv("PATH");
  if (path == nullptr)
    return false;
  std::string rest(path);
  std::size_t begin = 0;
  while (begin <= rest.size()) {
    const std::size_t end = rest.find(':', begin);
    const std::string dir = rest.substr(begin, end - begin);
    const fs::path cand = fs::path(dir.empty() ? "." : dir) / name;
    if (::access(cand.c_str(), X_OK) == 0)
      return true;
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }
  return false;
}

static std::string shq(const std::string &s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += '\'';
  return out;
}

bool render_netlist_files(const std::vector<std::string> &files,
                          const std::string &top, const std::string &out,
                          bool png, std::string &err) {
  if (files.empty()) {
    err = "netlist render: no input files";
    return false;
  }
  for (const char *tool : {"yosys", "dot"}) {
    if (!have_program(tool)) {
      err = std::string("netlist render needs ") + tool + " in PATH";
      return false;
    }
  }
  if (png && !have_program("rsvg-convert")) {
    err = "netlist render --png needs rsvg-convert in PATH";
    return false;
  }
  std::error_code ec;
  fs::create_directories(out, ec);
  // Yosys' -p parser does not strip shell quotes, and paths with spaces
  // would be split. Stage the inputs under simple names in the output dir.
  std::string script;
  for (std::size_t i = 0; i < files.size(); ++i) {
    const std::string staged = "netlist_in_" + std::to_string(i) + ".v";
    std::error_code cec;
    fs::copy_file(files[i], fs::path(out) / staged,
                  fs::copy_options::overwrite_existing, cec);
    if (cec) {
      err = "cannot stage " + files[i] + ": " + cec.message();
      return false;
    }
    script += "read_verilog -sv " + staged + "; ";
  }
  script += "prep -top " + top + "; show -format dot -prefix " + top + " " +
            top + ";";
  const std::string log = (fs::path(out) / "render.log").string();
  const std::string ycmd = "cd " + shq(out) + " && timeout 180 yosys -Q -p " +
                           shq(script) + " > " + shq(log) + " 2>&1";
  if (std::system(ycmd.c_str()) != 0) { // NOLINT(bugprone-command-processor)
    err = "yosys show failed; see " + log;
    return false;
  }
  const std::string dot = (fs::path(out) / (top + ".dot")).string();
  const std::string svg = (fs::path(out) / (top + ".svg")).string();
  const std::string dcmd = "timeout 120 dot -Tsvg " + shq(dot) + " -o " +
                           shq(svg) + " >> " + shq(log) + " 2>&1";
  if (std::system(dcmd.c_str()) != 0) { // NOLINT(bugprone-command-processor)
    err = "graphviz failed; see " + log;
    return false;
  }
  std::string img = top + ".svg";
  if (png) {
    const std::string pcmd = "timeout 120 rsvg-convert -w 2400 " + shq(svg) +
                             " -o " +
                             shq((fs::path(out) / (top + ".png")).string()) +
                             " >> " + shq(log) + " 2>&1";
    if (std::system(pcmd.c_str()) != 0) { // NOLINT(bugprone-command-processor)
      err = "rsvg-convert failed; see " + log;
      return false;
    }
    img = top + ".png";
  }
  std::ofstream html(fs::path(out) / "index.html");
  html << "<!doctype html><meta charset=\"utf-8\"><title>" << top
       << "</title>\n<h1>" << top << "</h1>\n<img src=\"" << img
       << "\" style=\"max-width:100%\">\n";
  return true;
}

bool render_netlist_native(const std::vector<std::string> &files,
                           const std::string &top, const std::string &out,
                           bool png, std::string &err, int cost,
                           const std::string &title) {
  if (files.empty()) {
    err = "native render: no input files";
    return false;
  }
  const fs::path dir = fs::temp_directory_path() /
                       ("dcstudy-native-" + std::to_string(::getpid()));
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::string script;
  for (std::size_t i = 0; i < files.size(); ++i) {
    const std::string name = "in" + std::to_string(i) + ".v";
    fs::copy_file(files[i], dir / name, fs::copy_options::overwrite_existing,
                  ec);
    if (ec) {
      err = "native render: cannot stage " + files[i];
      fs::remove_all(dir);
      return false;
    }
    script += "read_verilog -sv " + name + "; ";
  }
  script += "prep -top " + top + "; write_json net.json;";
  const std::string log = (dir / "yosys.log").string();
  const std::string cmd = "cd " + shq(dir.string()) +
                          " && timeout 180 yosys -Q -p " + shq(script) + " > " +
                          shq(log) + " 2>&1";
  if (std::system(cmd.c_str()) != 0) { // NOLINT(bugprone-command-processor)
    err = "native render: yosys write_json failed; see " + log;
    fs::remove_all(dir);
    return false;
  }
  Json root;
  try {
    std::ifstream in(dir / "net.json");
    std::ostringstream os;
    os << in.rdbuf();
    root = Json::parse(os.str());
  } catch (const std::exception &e) {
    err = std::string("native render: bad netlist json: ") + e.what();
    fs::remove_all(dir);
    return false;
  }
  if (!root.contains("modules") || !root.at("modules").contains(top)) {
    err = "native render: module not found: " + top;
    fs::remove_all(dir);
    return false;
  }
  const Json &mod = root.at("modules").at(top);
  std::map<std::string, std::string> bit_name;
  auto key_of = [](const Json &b) -> std::string {
    if (b.is_number())
      return std::to_string(b.as_int());
    if (b.is_string())
      return b.as_string();
    return "";
  };
  std::vector<std::string> vars, outs;
  auto bind_port = [&](const Json &pj, const std::string &name, bool input) {
    if (!pj.contains("bits"))
      return;
    const Json &bits = pj.at("bits");
    for (std::size_t i = 0; i < bits.size(); ++i) {
      const std::string k = key_of(bits.array().at(i));
      if (k.empty())
        continue;
      bit_name[k] =
          bits.size() == 1 ? name : name + "[" + std::to_string(i) + "]";
    }
    (input ? vars : outs).push_back(name);
  };
  if (mod.contains("ports")) {
    for (const auto &[name, pj] : mod.at("ports").object()) {
      const bool input =
          pj.contains("direction") && pj.at("direction").as_string() == "input";
      bind_port(pj, name, input);
    }
  }
  auto name_of = [&](const Json &b) -> std::string {
    const std::string k = key_of(b);
    if (k.empty())
      return "0";
    if (k == "0" || k == "1" || k == "x")
      return k;
    auto it = bit_name.find(k);
    if (it != bit_name.end())
      return it->second;
    const std::string name = "n" + k;
    bit_name[k] = name;
    return name;
  };
  Json cells = Json::Array{};
  if (mod.contains("cells")) {
    for (const auto &[inst, cj] : mod.at("cells").object()) {
      if (!cj.contains("type") || !cj.contains("connections"))
        continue;
      Json ports;
      for (const auto &[port, bits] : cj.at("connections").object()) {
        if (!bits.is_array() || bits.size() == 0)
          continue;
        ports[port] = name_of(bits.array().at(0));
      }
      Json cell;
      cell["type"] = cj.at("type").as_string();
      if (cj.contains("port_directions")) {
        Json dirs;
        for (const auto &[p, d] : cj.at("port_directions").object())
          dirs[p] = d.as_string();
        cell["directions"] = std::move(dirs);
      }
      cell["ports"] = std::move(ports);
      cells.push_back(std::move(cell));
    }
  }
  Json design;
  design["kind"] = "gate-boxes";
  design["cost"] = cost;
  design["branches"] = Json::Array{};
  design["inverters"] = Json::Array{};
  design["cells"] = std::move(cells);
  Json out_names = Json::Array{};
  for (const auto &o : outs)
    out_names.push_back(o);
  design["outputs"] = std::move(out_names);
  Json req;
  req["op"] = "render";
  req["out"] = out;
  req["vars"] = [&] {
    Json v = Json::Array{};
    for (const auto &x : vars)
      v.push_back(x);
    return v;
  }();
  req["title"] = title.empty() ? top : title;
  req["truth"] = "";
  req["png"] = png;
  req["designs"] = Json::Array{design};
  try {
    render_call(req);
  } catch (const std::exception &e) {
    err = std::string("native render: worker failed: ") + e.what();
    fs::remove_all(dir);
    return false;
  }
  fs::remove_all(dir);
  return true;
}

int cmd_render_static(const Args &a, const FuncSpec &spec,
                      const SpaceOptions &opt, bool json) {
  const CmosDesign d = synthesize_cmos(spec.func, spec.vars);
  const std::string out = a.get("out", "out/render");
  auto fail = [&](const std::string &message) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "render";
      j["space"] = "static-cmos";
      j["message"] = message;
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "static complementary CMOS: " << message << "\n";
    }
    return 1;
  };
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

  Json design;
  int total = 0;
  std::string topo;
  if (single_ok) {
    design = static_design_json(d, spec.vars);
    total = d.total;
    topo = design.at("topology").as_string();
  } else {
    const MultiStageDesign m =
        synthesize_multistage_static(spec.func, spec.vars, max_fanin);
    if (!m.ok || !m.verified)
      return fail(m.reason.empty() ? "multi-stage failed" : m.reason);
    if (m.total > opt.max_cost)
      return fail("multi-stage needs " + std::to_string(m.total) +
                  "T, over --max-cost");
    design = multi_design_json(m);
    total = m.total;
    topo = design.at("topology").as_string();
  }

  Json req;
  req["op"] = "render";
  req["out"] = out;
  req["title"] = spec.description;
  req["vars"] = [&] {
    Json v = Json::Array{};
    for (const auto &s : spec.vars)
      v.push_back(s);
    return v;
  }();
  req["truth"] = spec.func.truth_string();
  req["png"] = a.is("png");
  req["canonical"] = false;
  req["logic"] = !a.is("no-logic");
  Json designs = Json::Array{};
  designs.push_back(design);
  req["designs"] = std::move(designs);

  const Json resp = render_call(req);
  Json j;
  j["ok"] = true;
  j["command"] = "render";
  j["space"] = "static-cmos";
  j["out"] = out;
  j["optimum"] = total;
  j["single_stage"] = single_ok;
  j["topology"] = topo;
  j["designs"] = [&] {
    Json v = Json::Array{};
    v.push_back(design);
    return v;
  }();
  if (resp.contains("index"))
    j["index"] = resp.at("index");
  if (resp.contains("files"))
    j["files"] = resp.at("files");
  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "rendered static CMOS " << total << "T to " << out << "\n";
    std::cout << "  " << topo << "\n";
    if (resp.contains("index"))
      std::cout << "  index: " << resp.at("index").as_string() << "\n";
  }
  return 0;
}

int cmd_render(int argc, char **argv) {
  const Args a = parse_args(argc, argv, 2);
  const bool json = a.is("json");
  if (a.has("netlist")) {
    std::vector<std::string> files{a.need("netlist")};
    if (a.has("atoms"))
      files.push_back(a.need("atoms"));
    const std::string out = a.get("out", "out/render");
    const std::string top = a.get("top", "top");
    std::string err;
    if (!render_netlist_files(files, top, out, a.is("png"), err))
      throw CliError(err);
    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "render.netlist";
      j["out"] = out;
      j["svg"] = out + "/" + top + ".svg";
      if (a.is("png"))
        j["png"] = out + "/" + top + ".png";
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "rendered netlist to " << out << "/" << top << ".svg\n";
    }
    return 0;
  }
  const FuncSpec spec = get_spec(a);
  if (spec.vars.size() > 4)
    throw CliError("render supports at most 4 variables");
  const bool static_mode = a.is("static-cmos");
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", static_mode ? "32" : "8"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  if (opt.max_cost < 1 || opt.max_cost > kMaxSearchCost)
    throw CliError("--max-cost out of range");
  if (opt.max_chain < 1 || opt.max_chain > kMaxSearchChain)
    throw CliError("--max-chain out of range");
  const unsigned long limit_raw =
      std::stoul(a.get("limit", a.is("all") ? "64" : "8"));
  if (limit_raw == 0 || limit_raw > kMaxSearchSolutions)
    throw CliError("--limit must be in 1..1024");
  if (static_mode)
    return cmd_render_static(a, spec, opt, json);

  const auto result =
      synthesize_min(spec.vars, target_of(spec), opt,
                     static_cast<std::size_t>(limit_raw), a.is("canonical"));
  if (!result.feasible) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "render";
      j["optimum"] = result.optimum;
      j["message"] = result.message;
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "no design: " << result.message << "\n";
    }
    return 1;
  }

  const std::string out = a.get("out", "out/render");
  Json req;
  req["op"] = "render";
  req["out"] = out;
  req["title"] = spec.description;
  req["vars"] = [&] {
    Json v = Json::Array{};
    for (const auto &s : spec.vars)
      v.push_back(s);
    return v;
  }();
  req["truth"] = spec.func.truth_string();
  req["png"] = a.is("png");
  req["canonical"] = a.is("canonical");
  req["logic"] = !a.is("no-logic");
  Json designs = Json::Array{};
  for (const auto &d : result.designs)
    designs.push_back(design_json(d, spec.vars));
  req["designs"] = std::move(designs);

  const Json resp = render_call(req);
  Json j;
  j["ok"] = true;
  j["command"] = "render";
  j["out"] = out;
  j["optimum"] = result.optimum;
  j["designs"] = [&] {
    Json v = Json::Array{};
    for (const auto &d : result.designs)
      v.push_back(design_json(d, spec.vars));
    return v;
  }();
  if (resp.contains("index"))
    j["index"] = resp.at("index");
  if (resp.contains("files"))
    j["files"] = resp.at("files");
  if (resp.contains("designs"))
    j["rendered"] = resp.at("designs");

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "rendered " << result.designs.size() << " design(s) to " << out
              << "\n";
    if (resp.contains("index"))
      std::cout << "  index: " << resp.at("index").as_string() << "\n";
    if (resp.contains("files"))
      for (const auto &f : resp.at("files").array())
        std::cout << "  " << f.as_string() << "\n";
  }
  return 0;
}

} // namespace dcstudy
