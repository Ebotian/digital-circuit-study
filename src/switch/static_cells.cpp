#include "switch/static_cells.hpp"

#include <algorithm>
#include <set>

#include "switch/switchlevel.hpp"

namespace dcstudy {
namespace {

// Enough to name the failure without dumping one line per input row.
constexpr std::size_t kMaxReasons = 4;

void add_reason(std::vector<std::string> &reasons, std::string reason) {
  if (reasons.size() < kMaxReasons)
    reasons.push_back(std::move(reason));
}

} // namespace

CellStaticCheck check_static_cmos_cell(const Subckt &cell) {
  CellStaticCheck out;
  const std::vector<std::string> ins = cell.inputs();
  const std::vector<std::string> outs = cell.outputs();
  std::string vdd = "VDD";
  std::string vss = "VSS";
  for (const auto &p : cell.ports) {
    const PinDir dir = cell.direction(p);
    if (dir == PinDir::Power)
      vdd = p;
    else if (dir == PinDir::Ground)
      vss = p;
  }

  // Kappa-linearity: a cell input on a channel terminal makes the conduction
  // depend on the channel potential, which is exactly a pass transistor.
  out.kappa_linear = true;
  const std::set<std::string> in_set(ins.begin(), ins.end());
  for (const auto &d : cell.devices) {
    const std::string *bad = nullptr;
    if (in_set.count(d.drain))
      bad = &d.drain;
    else if (in_set.count(d.source))
      bad = &d.source;
    if (bad == nullptr)
      continue;
    out.kappa_linear = false;
    add_reason(out.reasons,
               "kappa: input " + *bad + " is a channel terminal of " + d.name);
  }

  // Complementary behaviour, checked per input row on the cell's own network.
  if (ins.empty() || outs.empty()) {
    add_reason(out.reasons, "no input or output ports declared");
    return out;
  }
  try {
    const SwitchAnalysis a = analyze_switch(cell, ins, outs, false, vdd, vss);
    out.pun_dual = true;
    for (std::size_t row = 0; row < a.rows.size(); ++row) {
      const SwitchRow &r = a.rows[row];
      if (r.conflict) {
        out.pun_dual = false;
        add_reason(out.reasons,
                   "minterm " + std::to_string(row) + ": VDD/VSS conflict");
      }
      for (std::size_t k = 0; k < r.values.size(); ++k) {
        if (r.values[k] == Logic::Zero || r.values[k] == Logic::One)
          continue;
        out.pun_dual = false;
        add_reason(out.reasons, "minterm " + std::to_string(row) + ": output " +
                                    outs[k] + " is " +
                                    std::string(1, logic_symbol(r.values[k])));
      }
    }
  } catch (const std::exception &e) {
    out.pun_dual = false;
    add_reason(out.reasons, std::string("static analysis failed: ") + e.what());
  }
  return out;
}

StaticCellsCheck
check_static_cmos_cells(const CdlLibrary &lib,
                        const std::vector<std::string> &types) {
  StaticCellsCheck out;
  std::set<std::string> distinct(types.begin(), types.end());
  out.checked.assign(distinct.begin(), distinct.end());
  for (const auto &type : out.checked) {
    if (!lib.has(type)) {
      out.kappa_linear = false;
      out.pun_dual = false;
      out.failed.push_back(type);
      out.reasons.push_back(type + ": not in the CDL library");
      continue;
    }
    const CellStaticCheck c = check_static_cmos_cell(lib.cell(type));
    if (!c.kappa_linear)
      out.kappa_linear = false;
    if (!c.pun_dual)
      out.pun_dual = false;
    if (!c.kappa_linear || !c.pun_dual)
      out.failed.push_back(type);
    for (const auto &r : c.reasons)
      out.reasons.push_back(type + ": " + r);
  }
  return out;
}

} // namespace dcstudy
