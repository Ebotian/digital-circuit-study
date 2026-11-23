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
  // What the proof actually covers: the search is exact only over the
  // retained candidate path set, not over the whole design space.
  std::string proof_scope;
  std::string proof_method;
  bool verified = false;
  std::string reason;
  int total = 0;
  int edge_cost = 0;
  int inverter_cost = 0;
  std::vector<std::string> outputs;
  std::vector<int> selected;
  SwitchGraph graph;
};

// `any` is an optional per-output don't-care mask; partial functions are
// completed with a minimum-literal SOP before the search (tri-state mode has
// no 0/1 extension semantics and rejects don't-cares).
MultiPathSynthesisResult synthesize_path_variables_multi(
    const std::vector<BoolFunc>& funcs, const std::vector<std::string>& names,
    const std::vector<std::string>& vars, const SpaceOptions& opt,
    bool tri_state = false, const std::vector<Bits>& any = {});

}  // namespace dcstudy
