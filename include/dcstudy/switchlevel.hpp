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

#include "dcstudy/cdl.hpp"

namespace dcstudy {

enum class Logic { Zero, One, Float, Race };

struct SwitchRow {
  std::vector<int> inputs;
  std::vector<Logic> values;        // one entry per requested output
  std::vector<std::string> floating_gates;
  bool conflict = false;            // both supplies forced in one component
  bool unsat = false;               // no consistent assignment at all
};

struct SwitchAnalysis {
  std::string cell;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::vector<SwitchRow> rows;
  bool exact = false;
  bool assumptions_ok = true;
  std::string mode;  // "fixpoint" or "z3"
};

SwitchAnalysis analyze_switch(const Subckt& cell,
                              const std::vector<std::string>& inputs,
                              const std::vector<std::string>& outputs,
                              bool exact = false,
                              const std::string& vdd = "VDD",
                              const std::string& vss = "VSS");

char logic_symbol(Logic v);

}  // namespace dcstudy
