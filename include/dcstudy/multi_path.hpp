// Multi-output path-variable synthesis with shared switch edges and input
// inverters.  Each output has its own source-to-Y paths; the objective is the
// union of unique switch edges, so paths from different outputs may share.
#pragma once

#include <string>
#include <vector>

#include "dcstudy/boolfunc.hpp"
#include "dcstudy/designspace.hpp"
#include "dcstudy/driver_profile.hpp"

namespace dcstudy {

struct MultiPathSynthesisResult {
  bool ok = false;
  bool proof = false;
  bool verified = false;
  std::string reason;
  int total = 0;
  int edge_cost = 0;
  int inverter_cost = 0;
  std::vector<std::string> outputs;
  std::vector<int> selected;
  SwitchGraph graph;
};

MultiPathSynthesisResult synthesize_path_variables_multi(
    const std::vector<BoolFunc>& funcs, const std::vector<std::string>& names,
    const std::vector<std::string>& vars, const SpaceOptions& opt);

}  // namespace dcstudy
