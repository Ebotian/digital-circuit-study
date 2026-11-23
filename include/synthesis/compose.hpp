// Automatic composition: lower a truth-table spec to a 2:1 mux network.
//
// The spec is decomposed with a reduced ordered BDD; every BDD node becomes one
// mux2 atom.  The resulting RTL goes through the normal --rtl/--atoms flow, so
// mapping, cost measurement and the miter are unchanged.
#pragma once

#include <string>

#include "core/partial.hpp"
#include "synthesis/multistage.hpp"

namespace dcstudy {

struct ComposeResult {
  bool ok = false;
  bool verified = false;
  std::string reason;
  int nodes = 0;        // mux2 instances in the emitted network
  std::string rtl;      // emitted top module
  std::string atom_rtl; // mux2 module
  RtlSynthResult synth; // mapping + cost + verification
};

ComposeResult compose_mux2(const MultiProblem &spec);

} // namespace dcstudy
