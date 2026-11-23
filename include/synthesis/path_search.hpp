// Bounded path-variable synthesis over the flat branch/path universe.
//
// A path is a source-to-Y series chain.  Paths may share a prefix, so the
// objective charges each unique switch edge once.  The search selects paths
// such that every input has exactly one conducting source group and a strong
// path, then verifies the resulting shared switch graph with C_0/C_1 closure.
#pragma once

#include <string>
#include <vector>

#include "core/boolfunc.hpp"
#include "core/designspace.hpp"
#include "core/driver_profile.hpp"

namespace dcstudy {

struct PathSynthesisResult {
  bool ok = false;
  bool proof = false;
  // Scope of the optimality claim (candidate path set, not the full space).
  std::string proof_scope;
  std::string proof_method;
  bool verified = false;
  std::string reason;
  int total = 0;
  int edge_cost = 0;
  int inverter_cost = 0;
  std::vector<int> selected; // branch ids
  SwitchGraph graph;
  std::vector<std::string> truths;
};

PathSynthesisResult
synthesize_path_variables(const BoolFunc &f,
                          const std::vector<std::string> &vars,
                          const SpaceOptions &opt, bool tri_state = false);

} // namespace dcstudy
