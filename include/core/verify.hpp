// External formal checks: Yosys SAT equivalence proof and Verilator
// simulation.  Both run in an isolated temporary directory.
#pragma once

#include <string>
#include <vector>

#include "core/boolfunc.hpp"

namespace dcstudy {

struct ToolResult {
  bool ok = false;
  int exit_code = -1;
  std::string output;
  std::string workdir;
};

ToolResult prove_yosys(const std::string &dut_source, const BoolFunc &target,
                       const std::vector<std::string> &vars, bool keep = false);

ToolResult simulate_verilator(const std::string &dut_source,
                              const BoolFunc &target,
                              const std::vector<std::string> &vars,
                              bool keep = false);

} // namespace dcstudy
