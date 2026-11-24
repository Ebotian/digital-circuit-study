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
#include "cli/cli_help.hpp"
#include "cli/cmd_basic.hpp"
#include "cli/cmd_design.hpp"
#include "cli/cmd_multi.hpp"
#include "cli/cmd_pipeline.hpp"
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
