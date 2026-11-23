// Single-stage complementary CMOS synthesis.
//
// A function is realisable iff its complement is monotone increasing.  The
// pull-down network is then an exact minimum-literal sum of products, found by
// Quine-McCluskey prime implicants plus an exact Petrick cover (branch and
// bound, not a greedy cover).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bits.hpp"
#include "core/boolfunc.hpp"
#include "core/cdl.hpp"

namespace dcstudy {

struct Cube {
  std::uint64_t care = 0;  // bit i set => variable i is constrained
  std::uint64_t value = 0; // required value for constrained variables
  Bits cover;              // minterms covered by this cube

  int literals() const;
  bool contains(std::uint32_t m) const {
    return (static_cast<std::uint64_t>(m) & care) == value;
  }
};

struct Sop {
  std::vector<Cube> terms;
  int literals = 0;
  std::string expression(const std::vector<std::string> &vars,
                         bool complement_literals = false) const;
};

Sop exact_min_sop(const Bits &onset, std::size_t n);

struct CmosDesign {
  bool realizable = false;
  Sop pull_down;
  int nfet = 0;
  int pfet = 0;
  int total = 0;
  std::string reason;
};

CmosDesign synthesize_cmos(const BoolFunc &f,
                           const std::vector<std::string> &vars);

// PUN as the De-Morgan dual of the PDN: product over PDN terms of the OR of
// complemented literals.  This is a product-of-sums, not a Sop.
std::string pull_up_expression(const CmosDesign &d,
                               const std::vector<std::string> &vars);

// Transistor-level single-stage complementary CMOS netlist (PDN + PUN).
// Inputs are the positive variables; output is Y; supplies are VDD/VSS.
Subckt build_static_cmos_subckt(const CmosDesign &d,
                                const std::vector<std::string> &vars);

} // namespace dcstudy
