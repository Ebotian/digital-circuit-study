#include "dcstudy/static_exact.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "dcstudy/parallel.hpp"
#include "dcstudy/switchlevel.hpp"

namespace dcstudy {
namespace {

Bits cover_of(std::uint64_t care, std::uint64_t value, std::size_t n) {
  const std::uint32_t N = std::uint32_t{1} << n;
  Bits cover(N);
  for (std::uint32_t m = 0; m < N; ++m)
    if ((static_cast<std::uint64_t>(m) & care) == value) cover.set(m);
  return cover;
}

struct MaskEval {
  bool feasible = false;
  int gate_cost = 0;
  std::vector<Sop> pdn;
};

struct CubeSearch {
  const std::vector<Cube>& cubes;
  const Bits& onset;
  std::vector<std::vector<std::size_t>> by_minterm;
  int best_cost = std::numeric_limits<int>::max();
  std::vector<std::size_t> best;

  CubeSearch(const std::vector<Cube>& c, const Bits& o)
      : cubes(c), onset(o), by_minterm(o.size()) {
    for (std::size_t i = 0; i < cubes.size(); ++i)
      for (std::uint32_t m = 0; m < onset.size(); ++m)
        if (cubes[i].cover.test(m)) by_minterm[m].push_back(i);
  }

  void search(Bits uncovered, int cost, std::vector<std::size_t>& chosen) {
    if (uncovered.none()) {
      if (cost < best_cost) {
        best_cost = cost;
        best = chosen;
      }
      return;
    }
    if (cost >= best_cost) return;
    const std::uint32_t m = static_cast<std::uint32_t>(uncovered.find_first());
    const auto& cand = by_minterm[m];
    for (std::size_t ci : cand) {
      const auto& c = cubes[ci];
      if (!uncovered.intersects(c.cover)) continue;
      const int add = c.literals();
      if (cost + add >= best_cost) continue;
      chosen.push_back(ci);
      search(uncovered & ~c.cover, cost + add, chosen);
      chosen.pop_back();
    }
  }
};

std::string inv_name(std::size_t i) { return "inv_" + std::to_string(i); }

std::string literal_name(const std::vector<std::string>& vars,
                         std::size_t i, bool positive) {
  return positive ? vars[i] : inv_name(i);
}

std::string cdl_from_subckt(const Subckt& c) {
  std::string out = ".SUBCKT " + c.name;
  for (const auto& p : c.ports) out += " " + p;
  out += "\n*.PININFO";
  for (const auto& p : c.ports) {
    char d = 'B';
    switch (c.direction(p)) {
      case PinDir::Input: d = 'I'; break;
      case PinDir::Output: d = 'O'; break;
      case PinDir::Power: d = 'P'; break;
      case PinDir::Ground: d = 'G'; break;
      default: break;
    }
    out += " " + p + ":" + d;
  }
  out += "\n";
  for (const auto& dev : c.devices) {
    const std::string bulk = dev.pmos ? "VDD" : "VSS";
    out += dev.name + " " + dev.drain + " " + dev.gate + " " + dev.source +
           " " + bulk + " " + dev.model + "\n";
  }
  out += ".ENDS\n";
  return out;
}

}  // namespace

bool exact_min_dnf_literals(const Bits& onset, std::size_t n,
                            std::uint64_t neg_allowed, Sop& out,
                            const Bits* dc) {
  out = Sop{};
  if (onset.size() != (std::size_t{1} << n))
    throw std::invalid_argument("onset width does not match n");
  if (dc && dc->size() != onset.size())
    throw std::invalid_argument("don't-care width does not match onset");
  // Cubes may cover must-on rows plus don't-care rows, but nothing else.
  const Bits allowed = dc ? (onset | *dc) : onset;
  if (onset.none()) return true;  // constant 0
  if (onset.count() == onset.size()) {
    // Constant 1: a zero-literal term.  The caller treats this as a hard tie.
    Cube c;
    c.care = 0;
    c.value = 0;
    c.cover = Bits::ones(onset.size());
    out.terms.push_back(std::move(c));
    return true;
  }

  // Enumerate every cube over the available literals.  A variable can be
  // absent, positive, or (if neg_allowed) negative.
  std::vector<Cube> cubes;
  std::set<std::pair<std::uint64_t, std::uint64_t>> seen;
  std::uint64_t total = 1;
  for (std::size_t i = 0; i < n; ++i) total *= 3;
  for (std::uint64_t code = 0; code < total; ++code) {
    std::uint64_t care = 0, value = 0, x = code;
    bool valid = true;
    for (std::size_t i = 0; i < n; ++i) {
      const int d = static_cast<int>(x % 3);
      x /= 3;
      const std::uint64_t bit = std::uint64_t{1} << (n - 1 - i);
      if (d == 0) continue;
      if (d == 2 && !((neg_allowed >> i) & 1U)) {
        valid = false;
        break;
      }
      care |= bit;
      if (d == 1) value |= bit;
    }
    if (!valid || care == 0) continue;
    if (!seen.emplace(care, value).second) continue;
    Cube c;
    c.care = care;
    c.value = value;
    c.cover = cover_of(care, value, n);
    if (!c.cover.subset_of(allowed)) continue;
    if (!c.cover.intersects(onset)) continue;
    cubes.push_back(std::move(c));
  }
  if (cubes.empty()) return false;

  CubeSearch cs(cubes, onset);
  Bits uncovered = onset;
  std::vector<std::size_t> chosen;
  cs.search(uncovered, 0, chosen);
  if (cs.best.empty()) return false;
  std::sort(cs.best.begin(), cs.best.end());
  cs.best.erase(std::unique(cs.best.begin(), cs.best.end()), cs.best.end());
  for (std::size_t i : cs.best) {
    out.terms.push_back(cubes[i]);
    out.literals += cubes[i].literals();
  }
  return true;
}

bool truth_satisfies(const std::string& truth, const std::string& target,
                     const Bits& any) {
  if (truth.size() != target.size()) return false;
  for (std::size_t m = 0; m < truth.size(); ++m) {
    if (any.size() == truth.size() && any.test(m)) {
      // Any extension is acceptable, but a static network still has to drive
      // the row to 0 or 1: Z/X is not a valid total extension.
      if (truth[m] != '0' && truth[m] != '1') return false;
      continue;
    }
    if (truth[m] != target[m]) return false;
  }
  return true;
}

BoolFunc complete_with_min_sop(const BoolFunc& f, const Bits& any) {
  if (any.none()) return f;
  const std::size_t n = f.arity();
  if (any.size() != f.bits().size())
    throw std::invalid_argument("don't-care mask width does not match function");
  const Bits onset = f.bits() & ~any;
  Sop sop;
  const std::uint64_t all_literals = (std::uint64_t{1} << n) - 1;
  if (!exact_min_dnf_literals(onset, n, all_literals, sop, &any))
    throw std::invalid_argument("don't-care completion found no cover");
  Bits bits(f.bits().size());
  for (const auto& term : sop.terms) bits |= term.cover;
  return BoolFunc(n, std::move(bits));
}

StaticExactResult synthesize_static_exact_multi(
    const std::vector<BoolFunc>& funcs, const std::vector<std::string>& names,
    const std::vector<std::string>& vars, const std::vector<Bits>& any) {
  StaticExactResult out;
  out.outputs = names;
  if (funcs.empty() || funcs.size() != names.size()) {
    out.reason = "one output name per function is required";
    return out;
  }
  const std::size_t n = vars.size();
  if (n == 0 || n > 4) {
    out.reason = "exact single-stage static CMOS supports 1..4 inputs";
    return out;
  }
  for (const auto& f : funcs) {
    if (f.arity() != n) {
      out.reason = "all outputs must use the same input variables";
      return out;
    }
  }

  // Empty mask used when the caller passed no don't-care sets.
  const Bits no_dc = Bits::zeros(funcs.empty() ? 0 : funcs.front().bits().size());
  const auto dc_of = [&](std::size_t k) -> const Bits& {
    return any.size() == funcs.size() ? any[k] : no_dc;
  };

  int best_total = std::numeric_limits<int>::max();
  int best_gate_cost = 0;
  int best_inv_cost = 0;
  std::uint64_t best_mask = 0;
  std::vector<Sop> best_pdn;

  // Product-term early stop.  If every output is a single cube, the exact
  // static gate is fixed: each cube literal becomes one PDN literal, and the
  // gate cost is 2*k.  The union of required input complements is shared.
  // This is the decoder/PLA case and avoids enumerating 2^n inverter sets.
  bool cube_form = true;
  std::uint64_t cube_mask = 0;
  int cube_gate_cost = 0;
  std::vector<Sop> cube_pdn;
  for (std::size_t k = 0; k < funcs.size(); ++k) {
    if (!dc_of(k).none()) {
      cube_form = false;  // don't-cares need the full cover search
      break;
    }
    const Sop fcube = exact_min_sop(funcs[k].bits(), n);
    if (fcube.terms.size() != 1 || fcube.terms[0].care == 0) {
      cube_form = false;
      break;
    }
    const Cube& fc = fcube.terms[0];
    Sop pdn;
    for (std::size_t i = 0; i < n; ++i) {
      const std::uint64_t bit = std::uint64_t{1} << (n - 1 - i);
      if (!(fc.care & bit)) continue;
      Cube term;
      term.care = bit;
      term.value = (fc.value & bit) ? 0 : bit;  // complementary literal
      if (term.value == 0) cube_mask |= (std::uint64_t{1} << i);
      pdn.terms.push_back(term);
      ++pdn.literals;
    }
    cube_gate_cost += 2 * pdn.literals;
    cube_pdn.push_back(std::move(pdn));
  }
  if (cube_form) {
    best_total = cube_gate_cost +
                 2 * static_cast<int>(__builtin_popcountll(cube_mask));
    best_gate_cost = cube_gate_cost;
    best_inv_cost = best_total - cube_gate_cost;
    best_mask = cube_mask;
    best_pdn = std::move(cube_pdn);
  }

  const std::uint64_t masks = std::uint64_t{1} << n;
  if (!cube_form) {
    // Each inverter subset is an independent exact-DNF problem.  Evaluate
    // them in parallel, then reduce in mask order so ties remain deterministic
    // (the first mask wins, exactly as in the serial loop).
    std::vector<MaskEval> evals(static_cast<std::size_t>(masks));
    parallel_for(static_cast<std::size_t>(masks), parallel_jobs(),
                 [&](std::size_t mi) {
                   const std::uint64_t mask = mi;
                   MaskEval ev;
                   bool feasible = true;
                   for (std::size_t k = 0; k < funcs.size(); ++k) {
                     const Bits& dc = dc_of(k);
                     const Bits onset = ~funcs[k].bits() & ~dc;
                     Sop sop;
                     if (!exact_min_dnf_literals(
                             onset, n, mask, sop, dc.none() ? nullptr : &dc)) {
                       feasible = false;
                       break;
                     }
                     if (sop.terms.empty() ||
                         (sop.terms.size() == 1 &&
                          sop.terms[0].care == 0)) {
                       // Constant outputs need an ideal rail tie; the current
                       // Subckt model only has transistor devices.
                       feasible = false;
                       break;
                     }
                     ev.gate_cost += 2 * sop.literals;
                     ev.pdn.push_back(std::move(sop));
                   }
                   ev.feasible = feasible;
                   evals[mi] = std::move(ev);
                 });
    for (std::uint64_t mask = 0; mask < masks; ++mask) {
      MaskEval& ev = evals[static_cast<std::size_t>(mask)];
      if (!ev.feasible) continue;
      const int inv_cost = 2 * static_cast<int>(__builtin_popcountll(mask));
      const int total = ev.gate_cost + inv_cost;
      if (total < best_total) {
        best_total = total;
        best_gate_cost = ev.gate_cost;
        best_inv_cost = inv_cost;
        best_mask = mask;
        best_pdn = std::move(ev.pdn);
      }
    }
  }

  if (best_total == std::numeric_limits<int>::max()) {
    out.reason = "no single-stage static CMOS implementation in this model";
    return out;
  }

  // Build the flattened transistor netlist.
  Subckt c;
  c.name = "STATIC_EXACT_MULTI";
  for (const auto& v : vars) {
    c.ports.push_back(v);
    c.pin_dir[v] = PinDir::Input;
  }
  for (const auto& o : names) {
    c.ports.push_back(o);
    c.pin_dir[o] = PinDir::Output;
  }
  c.ports.push_back("VDD");
  c.ports.push_back("VSS");
  c.pin_dir["VDD"] = PinDir::Power;
  c.pin_dir["VSS"] = PinDir::Ground;

  int dev_id = 0;
  int nfet = 0, pfet = 0;
  auto add = [&](const std::string& drain, const std::string& gate,
                 const std::string& source, bool pmos) {
    Device d;
    d.name = "M_" + std::to_string(dev_id++);
    d.drain = drain;
    d.gate = gate;
    d.source = source;
    d.pmos = pmos;
    d.model = pmos ? "PMOS" : "NMOS";
    c.devices.push_back(std::move(d));
    if (pmos) ++pfet; else ++nfet;
  };

  // Shared input inverters.
  for (std::size_t i = 0; i < n; ++i) {
    if (!((best_mask >> i) & 1U)) continue;
    const std::string x = vars[i];
    const std::string xb = inv_name(i);
    add(xb, x, "VSS", false);
    add(xb, x, "VDD", true);
  }

  for (std::size_t k = 0; k < names.size(); ++k) {
    const std::string& y = names[k];
    const Sop& sop = best_pdn[k];
    // PDN: parallel product terms, each a series chain from Y to VSS.
    for (std::size_t ti = 0; ti < sop.terms.size(); ++ti) {
      const Cube& cube = sop.terms[ti];
      std::vector<std::string> lits;
      for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t bit = std::uint64_t{1} << (n - 1 - i);
        if (!(cube.care & bit)) continue;
        const bool one = (cube.value & bit) != 0;
        lits.push_back(literal_name(vars, i, one));
      }
      if (lits.empty()) {
        out.reason = "constant output not supported";
        return out;
      }
      std::vector<std::string> nodes;
      nodes.push_back(y);
      for (std::size_t j = 1; j < lits.size(); ++j)
        nodes.push_back("pd_" + std::to_string(k) + "_" +
                        std::to_string(ti) + "_" + std::to_string(j));
      nodes.push_back("VSS");
      for (std::size_t j = 0; j < lits.size(); ++j)
        add(nodes[j], lits[j], nodes[j + 1], false);
    }

    // PUN: series of parallel groups, the dual of the PDN.
    std::string prev = "VDD";
    for (std::size_t ti = 0; ti < sop.terms.size(); ++ti) {
      const Cube& cube = sop.terms[ti];
      std::vector<std::string> lits;
      for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t bit = std::uint64_t{1} << (n - 1 - i);
        if (!(cube.care & bit)) continue;
        const bool one = (cube.value & bit) != 0;
        lits.push_back(literal_name(vars, i, one));
      }
      const std::string next =
          (ti + 1 == sop.terms.size()) ? y
                                       : ("pu_" + std::to_string(k) + "_" +
                                          std::to_string(ti));
      for (const auto& lit : lits) add(prev, lit, next, true);
      prev = next;
    }
  }

  out.netlist = std::move(c);
  out.cdl = cdl_from_subckt(out.netlist);
  out.gate_cost = best_gate_cost;
  out.inverter_cost = best_inv_cost;
  out.total = best_total;
  out.inverted_mask = best_mask;
  out.pdn = std::move(best_pdn);
  out.nfet = nfet;
  out.pfet = pfet;
  out.proof = true;
  out.proof_method = cube_form ? "analytic_shortcut" : "exhaustive";
  out.ok = true;

  // The outputs of this model are independent single-stage gates.  Verify
  // each output on its own pruned netlist so the exact Z3 check also scales
  // with one gate instead of the whole seven-segment display.  Shared input
  // inverters are still included in every cone.
  out.truths.resize(names.size());
  std::vector<char> output_ok(names.size(), 0);
  parallel_for(names.size(), parallel_jobs(), [&](std::size_t k) {
    const Subckt one =
        switch_output_cone(out.netlist, vars, names[k], "VDD", "VSS");
    const SwitchAnalysis a =
        analyze_switch(one, vars, {names[k]}, true, "VDD", "VSS", false, 1);
    std::string truth;
    for (const auto& row : a.rows)
      truth.push_back(logic_symbol(row.values[0]));
    out.truths[k] = std::move(truth);
    output_ok[k] =
        truth_satisfies(out.truths[k], funcs[k].truth_string(), dc_of(k)) ? 1
                                                                         : 0;
  });
  out.verified = std::all_of(output_ok.begin(), output_ok.end(),
                             [](bool v) { return v; });
  if (!out.verified) out.reason = "switch-level truth mismatch";
  return out;
}

}  // namespace dcstudy
