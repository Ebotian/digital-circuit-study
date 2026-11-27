// Automatic composition: lower a truth-table spec to a network of measured
// atoms (a library is searched) or to a 2:1 mux network (the fallback).
//
// Library search: every atom instance is priced by measuring its mapped cost;
// a multi-start greedy search (exact-cover seeds, one-step lookahead, then a
// beam fallback) picks instances whose output signals determine the spec
// outputs (same signal tuple => same output value) and pays a BDD->mux2 glue
// network for the rest.  Function signatures make the search independent of
// variable names: two functions with the same canonical table are the same
// function up to a permutation of the variables they actually depend on.
//
// Fallback: the spec is decomposed with a reduced ordered BDD and every BDD
// node becomes one mux2 atom.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/partial.hpp"
#include "synthesis/multistage.hpp"

namespace dcstudy {

// One atom instance of the emitted top: which library module, which spec
// variables feed its inputs (in port order) and its output port names.
struct AtomInstance {
  std::string module;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
};

struct ComposeResult {
  bool ok = false;
  bool verified = false;
  std::string reason;
  int nodes = 0;                  // library: signals; BDD: mux2 instances
  std::string rtl;                // emitted top module
  std::string atom_rtl;           // atom modules used by the top
  std::string mode;               // library | atom | bdd-mux2
  RtlSynthResult synth;           // mapping + cost + verification
  std::vector<AtomInstance> plan; // atoms the search selected
  bool timed_out = false;         // the search hit its time budget
  long long time_budget_ms = 0;   // budget the search ran under (0 = none)
};

struct ComposeOptions {
  // false stops after emitting the network: the caller only wants the RTL
  // (the library search inlines the glue into its own top and proves that
  // instead), so mapping and paying for the network separately is wasted work.
  bool synthesize = true;
  // Wall-clock limit for the library search; on expiry the best complete plan
  // found so far is emitted and timed_out is set.  0 = no deadline.
  long long time_budget_ms = 0;
};

// Shape signature of one Boolean function, independent of variable names.
// The essential variables are the ones the function depends on; they are
// reordered until the truth table is lexicographically smallest, and order[j]
// is the original variable index in canonical position j, so
// table(x) == f(vars[order[j]] := x_j).  Functions with equal signatures are
// the same function up to a bijection of their essential variables.  Above
// kMaxSignatureVars the permutation search is skipped (exact == false) and
// only an already-canonical order can match.
struct FuncSignature {
  std::size_t arity = 0;
  std::vector<unsigned char> table;
  std::vector<std::size_t> order;
  bool exact = false;
};

inline constexpr std::size_t kMaxSignatureVars = 6;

FuncSignature canonical_signature(const std::vector<unsigned char> &table,
                                  std::size_t n);

ComposeResult compose_mux2(const MultiProblem &spec,
                           const std::vector<MultiProblem> &library = {},
                           ComposeOptions options = {});

// Build the glue truth table h(S) from signal tables.  h.vars is the declared
// port order; h.outputs[k].func.value(idx) must correspond to that order with
// variable j = minterm bit (ns-1-j) (BoolFunc's big-endian convention).
std::optional<MultiProblem>
glue_from_signals(const MultiProblem &spec,
                  const std::vector<std::vector<unsigned char>> &signals,
                  const std::vector<std::size_t> &order = {});

} // namespace dcstudy
