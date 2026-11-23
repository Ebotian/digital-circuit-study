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
std::string emit_miter(const std::vector<std::string> &vars);
std::string emit_tb(const BoolFunc &target,
                    const std::vector<std::string> &vars);

} // namespace dcstudy
