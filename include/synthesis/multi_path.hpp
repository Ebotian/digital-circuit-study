// Multi-output path-variable synthesis with shared switch edges and input
// inverters.  Each output has its own source-to-Y paths; the objective is the
// union of unique switch edges, so paths from different outputs may share.
#pragma once

#include <string>
#include <vector>

#include "core/boolfunc.hpp"
#include "core/designspace.hpp"
#include "core/driver_profile.hpp"
#include "core/partial.hpp"

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

// Don't-care rows are completed with the cheapest static-gate extension
// before the search (tri-state has no 0/1 extension semantics and rejects
// don't-cares).
MultiPathSynthesisResult synthesize_path_variables_multi(
    const MultiProblem& problem, const SpaceOptions& opt,
    bool tri_state = false);

}  // namespace dcstudy
