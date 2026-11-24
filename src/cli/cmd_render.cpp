#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cli/cli_common.hpp"
#include "cli/cmd_design.hpp"
#include "cli/cmd_multi.hpp"
#include "cli/math_client.hpp"
#include "cli/render_client.hpp"
#include "core/boolfunc.hpp"
#include "core/cdl.hpp"
#include "core/cmos.hpp"
#include "core/designspace.hpp"
#include "core/driver_profile.hpp"
#include "core/json.hpp"
#include "core/limits.hpp"
#include "core/partial.hpp"
#include "core/verify.hpp"
#include "core/verilog.hpp"
#include "switch/bdd_switch.hpp"
#include "switch/switchlevel.hpp"
#include "synthesis/compose.hpp"
#include "synthesis/multi_path.hpp"
#include "synthesis/multistage.hpp"
#include "synthesis/path_search.hpp"
#include "synthesis/static_exact.hpp"
#include "synthesis/synthesis.hpp"
#include "synthesis/template_synth.hpp"

namespace fs = std::filesystem;
#include "cli/cli_common.hpp"

using namespace dcstudy;

namespace dcstudy {

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
  const FuncSpec spec = get_spec(a);
  if (spec.vars.size() > 4)
    throw CliError("render supports at most 4 variables");
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
