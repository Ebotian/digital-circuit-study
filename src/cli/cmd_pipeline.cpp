#include "cli/cli_common.hpp"
#include "cli/cli_includes.hpp"

namespace fs = std::filesystem;
using namespace dcstudy;

namespace dcstudy {

int cmd_pipeline(int argc, char **argv) {
  const Args a = parse_args(argc, argv, 2);
  const bool json = a.is("json");
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", "8"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  if (opt.max_cost < 1 || opt.max_cost > kMaxSearchCost)
    throw CliError("--max-cost out of range");
  if (opt.max_chain < 1 || opt.max_chain > kMaxSearchChain)
    throw CliError("--max-chain out of range");
  const VerifyMode vmode = parse_verify(a, VerifyMode::Both);
  // "static" is the switch-level proof of a single-stage static gate and only
  // applies to `design min --static-cmos`; elsewhere it would verify nothing.
  if (vmode == VerifyMode::Static)
    throw CliError(
        "--verify static is only available for design min --static-cmos");
  const auto funcs = split_csv(a.need("funcs"));
  if (funcs.empty())
    throw CliError("empty --funcs");

  Json runs = Json::Array{};
  bool all_ok = true;
  for (const auto &item : funcs) {
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
    } catch (const std::exception &e) {
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
      const auto &d = result.designs[i];
      fs::path emit_dir;
      if (a.has("out"))
        emit_dir = fs::path(a.get("out")) / (name + "_" + std::to_string(i));
      const auto v = verify_design(d, spec, vmode, a.is("keep"),
                                   a.has("out") ? &emit_dir : nullptr);
      if (vmode != VerifyMode::None && !v.yosys && !v.sim)
        ok = false;
      if ((vmode == VerifyMode::Yosys || vmode == VerifyMode::Both) && !v.yosys)
        ok = false;
      if ((vmode == VerifyMode::Sim || vmode == VerifyMode::Both) && !v.sim)
        ok = false;
      Json dj = design_json(d, spec.vars);
      if (vmode != VerifyMode::None)
        dj["verification"] = v.json;
      if (a.has("out"))
        dj["files"] = emit_dir.string();
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
    for (const auto &r : j["runs"].array()) {
      std::cout << r.at("name").as_string() << ": " << r.at("expr").as_string()
                << "  ";
      if (r.at("feasible").dump(0) != "true") {
        std::cout << "no design\n";
        continue;
      }
      std::cout << r.at("optimum").dump(0)
                << "T  designs=" << r.at("designs").size() << "\n";
      for (const auto &d : r.at("designs").array()) {
        std::cout << "  " << d.at("cost").dump(0) << "T  "
                  << d.at("topology").as_string() << "\n";
        if (d.contains("verification") && d.at("verification").is_object()) {
          const Json &v = d.at("verification");
          if (v.contains("yosys") && v.at("yosys").is_object())
            std::cout << "    yosys: "
                      << (v.at("yosys").at("ok").as_bool() ? "PASS" : "FAIL")
                      << "\n";
          if (v.contains("verilator") && v.at("verilator").is_object())
            std::cout << "    verilator: "
                      << (v.at("verilator").at("ok").as_bool() ? "PASS"
                                                               : "FAIL")
                      << "\n";
        }
      }
    }
  }
  return all_ok ? 0 : 1;
}

} // namespace dcstudy
