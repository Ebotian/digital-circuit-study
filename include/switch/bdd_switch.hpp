// General path construction: a reduced BDD is lowered to a switch DAG.
// This is deliberately a constructive path family, not an optimality proof.
#pragma once

#include <string>
#include <vector>

#include "core/boolfunc.hpp"
#include "core/driver_profile.hpp"

namespace dcstudy {

struct BddSwitchDesign {
  bool ok = false;
  bool verified = false;
  std::string reason;
  int nodes = 0;
  int total = 0;
  int inverters = 0;
  std::vector<int> order;
  SwitchGraph graph;
  std::vector<std::string> truths;
};

BddSwitchDesign synthesize_bdd_switch(const BoolFunc& f,
                                      const std::vector<std::string>& vars);

// Search all input orders (n <= 4) and return the smallest verified BDD DAG.
BddSwitchDesign synthesize_bdd_switch_best(const BoolFunc& f,
                                           const std::vector<std::string>& vars);

}  // namespace dcstudy
