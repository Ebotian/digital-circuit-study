// Dynamic driver-set analysis for a concrete pass-switch design.
//
// For every input minterm we build the Boolean adjacency matrix C_m of the
// selected switch paths, form C_m^* = I | C | C^2 | ... | C^(N-1), and read
// D_m = { source net s | (C_m^*)[s][Y] }.  This is the general path view; it
// also works when paths share internal nodes.  The flat branch solver uses a
// closed-form specialization of the same relation.
#pragma once

#include <string>
#include <vector>

#include "dcstudy/designspace.hpp"
#include "dcstudy/synthesis.hpp"

namespace dcstudy {

struct DriverRow {
  std::vector<int> source_ids;      // node ids of connected sources
  std::vector<std::string> sources; // source net names, unique
  std::vector<int> values;          // parallel to sources
  std::vector<bool> strong;         // parallel to sources
};

struct DriverProfile {
  std::vector<DriverRow> rows;
  std::vector<int> driver_count;
  int max_drivers = 0;
  int power_steps = 0;
  bool all_single = true;
};

DriverProfile analyze_design_drivers(const Design& d,
                                    const std::vector<std::string>& vars,
                                    const std::vector<int>& target);

}  // namespace dcstudy
