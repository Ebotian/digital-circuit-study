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
#include "cli/cmd_basic.hpp"
#include "cli/cmd_design.hpp"
#include "cli/cmd_multi.hpp"
#include "cli/cmd_render.hpp"
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
using namespace dcstudy;

namespace {

void help() {
  std::cout << R"(dcstudy - Boolean circuits, exact synthesis, formal checks

Usage:
  dcstudy [--json] <command> [options]

Commands:
  bool info      --expr EXPR | --tt BITS [--vars A,B]
  bool count     --n N
  cmos synth     --expr EXPR | --tt BITS [--vars A,B]
  math closure   --n N --edges 0-1,1-2 [--json]
  math gf2       --rows '1,1;0,1' [--ncols N] [--json]
  cdl list       [--cdl FILE]
  cdl solve      --cell NAME [--cdl FILE] [--inputs A,B] [--exact]
                 [--graphblas] [--expect BITS]
  design bdd     --expr EXPR | --tt BITS [--vars A,B]
  design template --expr EXPR | --tt BITS [--vars A,B]
  design path    --expr EXPR | --tt BITS [--max-cost N] [--max-chain N] [--tri-state]
  design min     --expr EXPR | --tt BITS [--max-cost N] [--max-chain N]
                 [--all] [--limit N] [--canonical] [--emit DIR]
                 [--verify none|yosys|sim|both|static] [--static-cmos] [--keep]
  verify equiv   --dut FILE --expr EXPR | --tt BITS [--vars A,B] [--keep]
  verify sim     --dut FILE --expr EXPR | --tt BITS [--vars A,B] [--keep]
  pipeline       --funcs 'name=EXPR,...' [--max-cost N] [--out DIR]
                 [--verify none|yosys|sim|both] [--canonical]
  multi synth    --spec FILE [--out DIR] [--png] [--max-fanin N]
                 [--rtl TOP.v --atoms ATOMS.v] [--json]
  multi exact    --spec FILE [--out DIR] [--json]
  multi compose  --spec FILE [--out DIR] [--json]  (BDD -> 2:1 mux network)
  multi path     --spec FILE [--max-cost N] [--max-chain N] [--tri-state] [--json]
  multi template --spec FILE [--json]
  render         --expr EXPR | --tt BITS [--max-cost N] [--limit N]
                 [--canonical] [--out DIR] [--png] [--no-logic]
                 [--static-cmos]

Options:
  --json         machine-readable output (also on errors)
  --vars         comma-separated variable order, e.g. A,B
  --max-cost     total transistor budget for a design (default 8)
  --max-chain    maximum switches in a series chain (default 4)
  --all          enumerate all optimal designs (up to --limit)
  --limit        maximum enumerated designs (default 8)
  --canonical    quotient designs by input permutations that preserve f
  --png          also rasterise rendered SVGs (needs rsvg-convert)
  --no-logic     skip the logic-abstraction panel (and its LaTeX compile)
  --static-cmos  restrict to static complementary CMOS (kappa-linear; rejects
                 pass/TG; single-stage, with automatic multi-stage fallback)
  --graphblas    use the GraphBLAS worker for switch-level reachability closure
  --tri-state    allow high-Z outputs; a 0 in a one-hot output becomes high-Z
  --spec         JSON circuit spec: {"inputs":[...],"outputs":[{"name":...,"expr":...}]}
  --max-fanin N  static CMOS cell fan-in limit (e.g. 2); 0 = library default
  --rtl FILE     hierarchical Verilog (top module `top`) to map instead of spec RTL
  --atoms FILE   atom modules read as blackboxes; "-" pipes in a previous stage
  --ref FILE     verify structurally against module FILE (spec optional)
  --emit-atom N  emit the generated module (state RTL) as atom N on stdout
  --heavy        allow the n=6 Dedekind count (about 40 s and 10 GB)
  --mem-limit    per-process memory cap in MiB, inherited by every child
                 (not a shared budget: N workers can each reach it). Default:
                 all of MemAvailable except 2 GiB. DCSTUDY_MEM_LIMIT_MB sets
                 the same cap; 0 = hard rlimit

Exit codes: 0 success, 1 verification/feasibility failure, 2 usage/runtime error.
)";
}

// Freed before reporting an allocation failure, so the error path does not
// have to allocate while the cap is already reached.
char *g_oom_reserve = nullptr;

// True when the cap came from --mem-limit / DCSTUDY_MEM_LIMIT_MB rather than
// from the machine, so errors can name the knob that was actually used.
bool g_mem_limit_from_flag = false;

// Install the memory cap before any command runs: --mem-limit wins, then
// DCSTUDY_MEM_LIMIT_MB, then whatever the machine can spare (MemAvailable
// minus a 2 GiB reserve).  Children inherit RLIMIT_AS, so this bounds the
// whole process tree.
void configure_mem_limit(int argc, char **argv) {
  bool explicit_limit = false;
  std::string text;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--mem-limit" && i + 1 < argc) {
      text = argv[++i];
      explicit_limit = true;
    } else if (arg.rfind("--mem-limit=", 0) == 0) {
      text = arg.substr(std::string("--mem-limit=").size());
      explicit_limit = true;
    }
  }
  if (!explicit_limit) {
    if (const char *env = std::getenv("DCSTUDY_MEM_LIMIT_MB"); env && *env) {
      text = env;
      explicit_limit = true;
    }
  }
  unsigned long mb = 0;
  g_mem_limit_from_flag = explicit_limit;
  if (explicit_limit) {
    try {
      mb = parse_mem_limit_mb(text);
    } catch (const std::invalid_argument &e) {
      throw CliError(e.what());
    }
  } else {
    const unsigned long available = available_memory_mb();
    mb = mem_limit_from_available(available);
    if (available == 0)
      std::cerr << "warning: cannot read /proc/meminfo; running without a "
                   "memory cap\n";
    else if (available <= kMemReserveMb)
      std::cerr << "warning: only " << available
                << " MB available, under the 2 GiB reserve; refusing to "
                   "allocate past 1 MB\n";
  }
  const unsigned long installed = apply_mem_limit(mb);
  if (mb != 0 && installed != 0 && installed < mb)
    std::cerr << "warning: memory cap " << mb << " MB clamped to " << installed
              << " MB by the hard rlimit\n";
}

int cmd_pipeline(int argc, char **argv) {
  const Args a = parse_args(argc, argv, 2);
  const bool json = a.is("json");
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", "8"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  if (opt.max_cost < 1 || opt.max_cost > 32)
    throw CliError("--max-cost out of range");
  if (opt.max_chain < 1 || opt.max_chain > 8)
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

} // namespace

int dcstudy_cli_main(int argc, char **argv) {
  bool want_json = false;
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--json")
      want_json = true;
  try {
    if (argc < 2) {
      help();
      return 2;
    }
    // Allow the global --json before the command: move it to the end so the
    // two-word command dispatch (and parse_args) still works.
    if (argc > 2 && std::string(argv[1]) == "--json") {
      char *json_arg = argv[1];
      for (int i = 1; i + 1 < argc; ++i)
        argv[i] = argv[i + 1];
      argv[argc - 1] = json_arg;
    }
    configure_mem_limit(argc, argv);
    g_oom_reserve = new (std::nothrow) char[64 * 1024];
    const std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
      help();
      return 0;
    }
    if (argc < 3)
      throw CliError("missing subcommand or arguments for " + cmd);
    const std::string sub = argv[2];
    if (cmd == "bool")
      return cmd_bool(sub, argc, argv);
    if (cmd == "cmos")
      return cmd_cmos(sub, argc, argv);
    if (cmd == "math")
      return cmd_math(sub, argc, argv);
    if (cmd == "cdl")
      return cmd_cdl(sub, argc, argv);
    if (cmd == "design")
      return cmd_design(sub, argc, argv);
    if (cmd == "verify")
      return cmd_verify(sub, argc, argv);
    if (cmd == "multi")
      return cmd_multi(sub, argc, argv);
    if (cmd == "render")
      return cmd_render(argc, argv);
    if (cmd == "pipeline")
      return cmd_pipeline(argc, argv);
    throw CliError("unknown command: " + cmd);
  } catch (const std::bad_alloc &) {
    delete[] g_oom_reserve;
    g_oom_reserve = nullptr;
    const unsigned long mb = current_mem_limit_mb();
    std::string why = "out of memory";
    if (mb != 0)
      why = "memory limit reached (" + std::to_string(mb) + " MB, " +
            (g_mem_limit_from_flag ? "--mem-limit" : "default cap") +
            "); raise the limit or shrink the search";
    emit_error(want_json, why, 2);
    return 2;
  } catch (const CliError &e) {
    emit_error(want_json, e.what(), e.code);
    return e.code;
  } catch (const std::exception &e) {
    std::string msg = e.what();
    // A capped child (Z3, yosys, node) reports its own "out of memory"; name
    // the cap so the fix is obvious from the error alone.
    if (const unsigned long cap = current_mem_limit_mb();
        cap != 0 && msg.find("out of memory") != std::string::npos)
      msg += " (the process tree is capped at " + std::to_string(cap) +
             " MB; raise --mem-limit)";
    emit_error(want_json, msg, 2);
    return 2;
  }
}
