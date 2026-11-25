// Automatic composition: lower a truth-table spec to a 2:1 mux network.
//
// The spec is decomposed with a reduced ordered BDD; every BDD node becomes one
// mux2 atom.  The resulting RTL goes through the normal --rtl/--atoms flow, so
// mapping, cost measurement and the miter are unchanged.
#pragma once

#include <optional>
#include <string>
#include <vector>

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

// `synthesize = false` stops after emitting the network: the caller only
// wants the RTL (the library search inlines the glue into its own top and
// proves that instead), so mapping and paying for the network separately is
// wasted work.
ComposeResult compose_mux2(const MultiProblem &spec,
                           const std::vector<MultiProblem> &library = {},
                           bool synthesize = true);

// Build the glue truth table h(S) from signal tables.  h.vars is the declared
// port order; h.outputs[k].func.value(idx) must correspond to that order with
// variable j = minterm bit (ns-1-j) (BoolFunc's big-endian convention).
std::optional<MultiProblem>
glue_from_signals(const MultiProblem &spec,
                  const std::vector<std::vector<unsigned char>> &signals,
                  const std::vector<std::size_t> &order = {});

} // namespace dcstudy
