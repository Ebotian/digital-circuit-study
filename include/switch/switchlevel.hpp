// Switch-level analysis of transistor netlists.
//
// Fast path: a monotone fixed point over the sets of nets forced to 0/1, with
// final connectivity checked by Boolean-matrix transitive closure.  Exact
// path: a Z3 model of the conducting devices plus unrolled reachability from
// VDD/VSS; it also handles floating gate nodes, which the fixed point
// deliberately does not assign.
#pragma once

#include <string>
#include <vector>

#include "core/cdl.hpp"

namespace dcstudy {

enum class Logic { Zero, One, Float, Race };

struct SwitchRow {
  std::vector<int> inputs;
  std::vector<Logic> values; // one entry per requested output
  std::vector<std::string> floating_gates;
  bool conflict = false; // both supplies forced in one component
  bool unsat = false;    // no consistent assignment at all
};

struct SwitchAnalysis {
  std::string cell;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::vector<SwitchRow> rows;
  bool exact = false;
  bool assumptions_ok = true;
  std::string mode; // "fixpoint" or "z3"
};

// Build the cone of influence feeding one output: all devices on paths from
// the output back to primary inputs (and shared supplies), with only that
// output exposed as a port.  Static multi-output gates are independent, so
// each cone can be checked separately instead of asking Z3 to reason about
// every output at once.
Subckt switch_output_cone(const Subckt &cell,
                          const std::vector<std::string> &inputs,
                          const std::string &output,
                          const std::string &vdd = "VDD",
                          const std::string &vss = "VSS");

SwitchAnalysis
analyze_switch(const Subckt &cell, const std::vector<std::string> &inputs,
               const std::vector<std::string> &outputs, bool exact = false,
               const std::string &vdd = "VDD", const std::string &vss = "VSS",
               bool graphblas = false, int jobs = 0);

char logic_symbol(Logic v);

} // namespace dcstudy
