// Static-CMOS classification of library cells, measured on their CDL
// transistors instead of assumed from the cell name.
//
// The two facts the synthesis JSON used to assert by construction are:
//   kappa-linearity  - no cell input sits on a channel terminal, so every
//                      transistor is controlled by its gate alone (no pass
//                      transistors);
//   PUN/PDN duality  - for every input row each output is driven to exactly
//                      one rail: the pull-up conducts exactly when the
//                      pull-down does not, never both and never neither.
//
// A cell-level netlist is static complementary CMOS iff every cell it
// instantiates is, because flattening only renames nets.
#pragma once

#include <string>
#include <vector>

#include "core/cdl.hpp"

namespace dcstudy {

struct CellStaticCheck {
  bool kappa_linear = false;
  bool pun_dual = false;
  std::vector<std::string> reasons;
};

CellStaticCheck check_static_cmos_cell(const Subckt &cell);

struct StaticCellsCheck {
  bool kappa_linear = true;
  bool pun_dual = true;
  std::vector<std::string> checked; // distinct types, sorted
  std::vector<std::string> failed;  // types failing either check
  std::vector<std::string> reasons; // "TYPE: reason"
};

// Checks every distinct type in `types` against `lib`.  A type missing from
// the library fails both checks instead of being skipped.
StaticCellsCheck check_static_cmos_cells(const CdlLibrary &lib,
                                         const std::vector<std::string> &types);

} // namespace dcstudy
