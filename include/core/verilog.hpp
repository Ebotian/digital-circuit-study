// Verilog emission: the candidate pass-network, a mechanically generated
// reference from the truth table, a miter for Yosys SAT, and a Verilator
// testbench.
#pragma once

#include <string>
#include <vector>

#include "core/boolfunc.hpp"
#include "synthesis/synthesis.hpp"

namespace dcstudy {

std::string emit_dut(const Design &design,
                     const std::vector<std::string> &vars);
std::string emit_ref(const BoolFunc &target,
                     const std::vector<std::string> &vars);
// Wire names used inside the generated miter.  They must not collide with the
// input names: Verilog merges a wire with a port of the same name, so an input
// called `y_dut` or `mismatch` would be tied to the comparison signal and the
// proof would silently weaken.
struct MiterNames {
  std::string y_ref;
  std::string y_dut;
  std::string ok;
  std::string mismatch;
};
MiterNames miter_names(const std::vector<std::string> &vars);
std::string emit_miter(const std::vector<std::string> &vars);
std::string emit_tb(const BoolFunc &target,
                    const std::vector<std::string> &vars);

} // namespace dcstudy
