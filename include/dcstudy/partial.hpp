// A multi-output Boolean problem whose output rows may be don't-care.
//
// This is the single interface shared by every multi-output synthesizer
// (multi exact / multi synth / multi path): the variables, output names,
// partial functions and their don't-care masks live in one place, so the
// "complete the X rows" and "does this truth satisfy the spec" policies are
// implemented once instead of once per backend.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "dcstudy/bits.hpp"
#include "dcstudy/boolfunc.hpp"

namespace dcstudy {

struct PartialOutput {
  BoolFunc func;  // values on care rows; arbitrary on don't-care rows
  Bits any;       // 1 = this row may be either value (size = 2^n)
};

struct MultiProblem {
  std::string title;
  std::vector<std::string> vars;
  std::vector<std::string> names;
  std::vector<PartialOutput> outputs;

  std::size_t size() const { return outputs.size(); }
  std::size_t rows() const {
    return vars.empty() ? 0 : (std::size_t{1} << vars.size());
  }
  const Bits& any(std::size_t k) const { return outputs[k].any; }
  bool has_dont_cares() const;

  // Cheapest total extension for static mapping: the don't-care rows are
  // chosen to minimise a single-stage static gate (shared-inverter aware).
  BoolFunc completed(std::size_t k) const;

  // True when a switch-level truth string satisfies output k: every care row
  // must match, every don't-care row may be either value but must be driven.
  bool satisfies(std::size_t k, const std::string& truth) const;
};

}  // namespace dcstudy
