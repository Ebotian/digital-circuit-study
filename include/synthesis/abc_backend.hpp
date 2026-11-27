// ABC backend: rewrite a small glue network through a separate ABC process.
//
// The round-trip is Yosys -> BLIF -> ABC -> BLIF -> Yosys.  ABC runs as a
// subprocess (never linked), inherits the memory cap installed by main() and
// runs under a wall-clock timeout; the caller keeps its own network whenever
// the rewrite fails, times out or comes back larger.  ABC only reshapes the
// netlist (balance/rewrite/refactor/resub/drw): mapping to library cells, cost
// accounting and the final miter stay in the existing flow.
#pragma once

#include <cstddef>
#include <string>

namespace dcstudy {

// The ABC draft fixes 5 s per external step and 2k nodes as the largest glue
// worth handing over.
inline constexpr int kAbcTimeoutSeconds = 5;
inline constexpr std::size_t kMaxAbcNodes = 2000;
inline constexpr char kDefaultAbcCommands[] =
    "balance; rewrite -l; refactor -l; resub -K 6 -N 2; drw -l";

struct AbcOptions {
  // Rewrite commands; empty means kDefaultAbcCommands.
  std::string commands;
  // Explicit ABC path; empty means $DCSTUDY_ABC, then abc on PATH.
  std::string binary;
  int timeout_seconds = kAbcTimeoutSeconds;
  // false keeps the round-trip but runs no rewrite commands (the M0 pass).
  bool optimize = true;
};

struct AbcResult {
  bool ok = false;
  std::string reason;
  std::string verilog;          // rewritten module, same module name and ports
  std::string version;          // first line of ABC's version banner
  std::string commands;         // commands actually run
  std::string log;              // tail of the ABC log, for diagnostics
  std::size_t nodes_before = 0; // .names count in the input BLIF
  std::size_t nodes_after = 0;  // .names count in ABC's output
};

// Locate ABC: the explicit path, then $DCSTUDY_ABC, then abc on PATH.
std::string find_abc_binary(const std::string &explicit_path = {});

// Rewrite one flat module.  The source may define helper modules used by the
// top module; they are flattened away before BLIF so ABC only sees gates.
AbcResult abc_rewrite(const std::string &verilog, const std::string &top,
                      const AbcOptions &options = {});

} // namespace dcstudy
