// General path construction: a reduced BDD is lowered to a switch DAG.
// This is deliberately a constructive path family, not an optimality proof.
#pragma once

#include <string>
#include <vector>

#include "dcstudy/boolfunc.hpp"
#include "dcstudy/driver_profile.hpp"

namespace dcstudy {

struct BddSwitchDesign {
  bool ok = false;
  bool verified = false;
  std::string reason;
  int nodes = 0;
  int total = 0;
  int inverters = 0;
  SwitchGraph graph;
  std::vector<std::string> truths;
};

BddSwitchDesign synthesize_bdd_switch(const BoolFunc& f,
                                      const std::vector<std::string>& vars);

}  // namespace dcstudy
