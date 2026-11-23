// Minimum-cost branch selection as a 0/1 integer program solved by Z3's
// Optimize.  Coverage is a set-cover constraint over input minterms; inverter
// use is coupled to the branches that need the inverted literal.  After the
// optimum is known, a plain Z3 solver enumerates all optimal branch sets.
#pragma once

#include <string>
#include <vector>

#include "core/designspace.hpp"

namespace dcstudy {

struct Design {
  std::vector<Branch> branches;
  std::vector<std::size_t> inv_vars;
  int cost = 0;
  bool canonical = false;

  std::string topologies(const std::vector<std::string>& vars) const;
  std::string inverter_names(const std::vector<std::string>& vars) const;
};

struct SynthesisResult {
  bool feasible = false;
  int optimum = 0;
  std::vector<Design> designs;
  std::size_t branch_count = 0;
  std::string message;
};

SynthesisResult synthesize_min(const std::vector<std::string>& vars,
                               const std::vector<int>& target,
                               const SpaceOptions& opt,
                               std::size_t max_solutions = 64,
                               bool canonical = false);

}  // namespace dcstudy
