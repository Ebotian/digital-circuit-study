#include "core/cmos.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dcstudy {
namespace {

std::uint64_t full_mask(std::size_t n) {
  return n >= 64 ? ~std::uint64_t{0} : ((std::uint64_t{1} << n) - 1);
}

Bits cover_of(std::uint64_t care, std::uint64_t value, std::size_t n) {
  const std::uint32_t N = std::uint32_t{1} << n;
  Bits cover(N);
  for (std::uint32_t m = 0; m < N; ++m)
    if ((static_cast<std::uint64_t>(m) & care) == value)
      cover.set(m);
  return cover;
}

bool covers(const Cube &a, const Cube &b) {
  // a covers b iff every literal required by a is also required (and matched)
  // by b.  Bits outside a.care are don't-cares in a.
  return (a.care & ~b.care) == 0 && (b.value & a.care) == a.value;
}

std::vector<Cube> prime_implicants(const Bits &onset, std::size_t n) {
  const std::uint64_t all = full_mask(n);
  std::vector<Cube> cubes;
  for (std::uint32_t m = 0; m < onset.size(); ++m)
    if (onset.test(m))
      cubes.push_back(Cube{all, static_cast<std::uint64_t>(m),
                           cover_of(all, static_cast<std::uint64_t>(m), n)});

  // Combine cubes that differ in exactly one constrained variable, level by
  // level (Quine-McCluskey).  Keep every generated implicant; primes are the
  // maximal ones.
  bool changed = true;
  while (changed) {
    changed = false;
    std::vector<Cube> next;
    std::set<std::pair<std::uint64_t, std::uint64_t>> seen;
    for (const auto &c : cubes)
      seen.emplace(c.care, c.value);
    for (std::size_t i = 0; i < cubes.size(); ++i) {
      for (std::size_t j = i + 1; j < cubes.size(); ++j) {
        const auto &a = cubes[i];
        const auto &b = cubes[j];
        if (a.care != b.care)
          continue;
        const std::uint64_t diff = (a.value ^ b.value) & a.care;
        if (__builtin_popcountll(diff) != 1)
          continue;
        const std::uint64_t care = a.care & ~diff;
        const std::uint64_t value = a.value & care;
        if (!seen.emplace(care, value).second)
          continue;
        next.push_back(Cube{care, value, cover_of(care, value, n)});
        changed = true;
      }
    }
    cubes.insert(cubes.end(), next.begin(), next.end());
  }

  std::vector<Cube> primes;
  for (std::size_t i = 0; i < cubes.size(); ++i) {
    bool subsumed = false;
    for (std::size_t j = 0; j < cubes.size(); ++j) {
      if (i == j)
        continue;
      if (cubes[j].care == cubes[i].care)
        continue;
      if (covers(cubes[j], cubes[i])) {
        subsumed = true;
        break;
      }
    }
    if (!subsumed)
      primes.push_back(cubes[i]);
  }
  std::sort(primes.begin(), primes.end(), [](const Cube &a, const Cube &b) {
    if (__builtin_popcountll(a.care) != __builtin_popcountll(b.care))
      return __builtin_popcountll(a.care) < __builtin_popcountll(b.care);
    return a.value < b.value;
  });
  return primes;
}

struct CoverSearch {
  const std::vector<Cube> &primes;
  const Bits &onset;
  std::vector<std::vector<std::size_t>> by_minterm;
  int best_cost = 1 << 30;
  std::vector<std::size_t> best;
  std::map<std::vector<std::uint64_t>, int> seen;

  explicit CoverSearch(const std::vector<Cube> &p, const Bits &o)
      : primes(p), onset(o), by_minterm(o.size()) {
    for (std::size_t i = 0; i < primes.size(); ++i)
      for (std::uint32_t m = 0; m < onset.size(); ++m)
        if (primes[i].cover.test(m))
          by_minterm[m].push_back(i);
  }

  std::vector<std::uint64_t> key_of(const Bits &b) const {
    std::vector<std::uint64_t> k;
    k.reserve(b.words());
    for (std::size_t i = 0; i < b.words(); ++i)
      k.push_back(b.word(i));
    return k;
  }

  void search(const Bits &uncovered, int cost,
              std::vector<std::size_t> &chosen) {
    if (uncovered.none()) {
      if (cost < best_cost) {
        best_cost = cost;
        best = chosen;
      }
      return;
    }
    if (cost >= best_cost)
      return;
    auto k = key_of(uncovered);
    auto it = seen.find(k);
    if (it != seen.end() && it->second <= cost)
      return;
    seen[k] = cost;

    // Pick the uncovered minterm with the fewest candidate primes.
    const std::uint32_t m = static_cast<std::uint32_t>(uncovered.find_first());
    const auto &cand = by_minterm[m];
    for (std::size_t pi : cand) {
      const auto &p = primes[pi];
      if (!uncovered.intersects(p.cover))
        continue;
      const int add = p.literals();
      if (cost + add >= best_cost)
        continue;
      chosen.push_back(pi);
      search(uncovered & ~p.cover, cost + add, chosen);
      chosen.pop_back();
    }
  }
};

} // namespace

int Cube::literals() const { return __builtin_popcountll(care); }

std::string Sop::expression(const std::vector<std::string> &vars,
                            bool complement_literals) const {
  if (terms.empty())
    return complement_literals ? "1" : "0";
  std::string out;
  for (const auto &c : terms) {
    if (!out.empty())
      out += " | ";
    if (c.care == 0) {
      out += '1';
      continue;
    }
    std::string term;
    for (std::size_t i = 0; i < vars.size(); ++i) {
      // Cube bits follow the minterm encoding: vars[0] is the MSB.
      const std::uint64_t bit = std::uint64_t{1} << (vars.size() - 1 - i);
      if (!(c.care & bit))
        continue;
      const bool one = (c.value & bit) != 0;
      if (!term.empty())
        term += " & ";
      if (!one)
        term += '~';
      term += vars.at(i);
    }
    out += "(" + term + ")";
  }
  return out;
}

Sop exact_min_sop(const Bits &onset, std::size_t n) {
  if (onset.size() != (std::size_t{1} << n))
    throw std::invalid_argument("onset width does not match n");
  Sop result;
  if (onset.none())
    return result;
  if (onset.count() == onset.size()) {
    result.terms.push_back(Cube{0, 0, Bits::ones(onset.size())});
    result.literals = 0;
    return result;
  }
  auto primes = prime_implicants(onset, n);
  CoverSearch cs(primes, onset);
  Bits uncovered = onset;
  std::vector<std::size_t> chosen;

  // Essential prime implicants: a minterm covered by exactly one prime.
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::uint32_t m = 0; m < onset.size(); ++m) {
      if (!uncovered.test(m))
        continue;
      std::vector<std::size_t> cand;
      for (std::size_t i = 0; i < primes.size(); ++i)
        if (primes[i].cover.test(m))
          cand.push_back(i);
      if (cand.size() == 1) {
        chosen.push_back(cand[0]);
        uncovered &= ~primes[cand[0]].cover;
        changed = true;
      }
    }
  }
  int base = 0;
  for (std::size_t i : chosen)
    base += primes[i].literals();
  cs.best_cost = 1 << 30;
  cs.search(uncovered, base, chosen);
  if (cs.best.empty())
    throw std::runtime_error("exact cover failed (no prime cover)");

  // Deduplicate while keeping the deterministic order.
  std::vector<std::size_t> sel = cs.best;
  std::sort(sel.begin(), sel.end());
  sel.erase(std::unique(sel.begin(), sel.end()), sel.end());
  for (std::size_t i : sel) {
    result.terms.push_back(primes[i]);
    result.literals += primes[i].literals();
  }
  return result;
}

CmosDesign synthesize_cmos(const BoolFunc &f,
                           const std::vector<std::string> &vars) {
  CmosDesign out;
  const BoolFunc pd = ~f;
  if (!pd.is_monotone()) {
    out.realizable = false;
    out.reason = "complement is not monotone increasing";
    return out;
  }
  out.realizable = true;
  out.pull_down = exact_min_sop(pd.bits(), f.arity());
  out.nfet = out.pull_down.literals;
  // The PUN is the dual of the PDN: one PMOS per PDN literal.
  out.pfet = out.nfet;
  out.total = out.nfet + out.pfet;
  (void)vars;
  return out;
}

std::string pull_up_expression(const CmosDesign &d,
                               const std::vector<std::string> &vars) {
  if (!d.realizable)
    return "0";
  if (d.pull_down.terms.empty())
    return "1";
  const std::size_t n = vars.size();
  std::string out;
  for (const auto &c : d.pull_down.terms) {
    std::string clause;
    for (std::size_t i = 0; i < n; ++i) {
      const std::uint64_t bit = std::uint64_t{1} << (n - 1 - i);
      if (!(c.care & bit))
        continue;
      if (!clause.empty())
        clause += " | ";
      clause += "~" + vars[i];
    }
    if (clause.empty())
      continue;
    if (!out.empty())
      out += " & ";
    out += "(" + clause + ")";
  }
  return out.empty() ? "1" : out;
}

Subckt build_static_cmos_subckt(const CmosDesign &d,
                                const std::vector<std::string> &vars) {
  if (!d.realizable)
    throw std::runtime_error("static CMOS design is not realisable");
  Subckt c;
  c.name = "STATIC_CMOS";
  for (const auto &v : vars) {
    c.ports.push_back(v);
    c.pin_dir[v] = PinDir::Input;
  }
  c.ports.push_back("Y");
  c.ports.push_back("VDD");
  c.ports.push_back("VSS");
  c.pin_dir["Y"] = PinDir::Output;
  c.pin_dir["VDD"] = PinDir::Power;
  c.pin_dir["VSS"] = PinDir::Ground;

  const std::size_t n = vars.size();
  int dev_id = 0;
  auto add = [&](const std::string &drain, const std::string &gate,
                 const std::string &source, bool pmos) {
    Device dev;
    dev.name = "M_" + std::to_string(dev_id++);
    dev.drain = drain;
    dev.gate = gate;
    dev.source = source;
    dev.model = pmos ? "PMOS" : "NMOS";
    dev.pmos = pmos;
    c.devices.push_back(std::move(dev));
  };

  auto term_vars = [&](const Cube &cube) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < n; ++i) {
      const std::uint64_t bit = std::uint64_t{1} << (n - 1 - i);
      if (!(cube.care & bit))
        continue;
      if (!(cube.value & bit))
        throw std::runtime_error("PDN cube contains a negated literal");
      out.push_back(vars[i]);
    }
    return out;
  };

  // PDN: parallel branches of series NMOS.
  const auto &terms = d.pull_down.terms;
  for (std::size_t ti = 0; ti < terms.size(); ++ti) {
    const auto lits = term_vars(terms[ti]);
    if (lits.empty())
      continue; // constant; handled by caller if needed
    std::vector<std::string> nodes;
    nodes.push_back("Y");
    for (std::size_t j = 1; j < lits.size(); ++j)
      nodes.push_back("pd_" + std::to_string(ti) + "_" + std::to_string(j));
    nodes.push_back("VSS");
    for (std::size_t j = 0; j < lits.size(); ++j)
      add(nodes[j], lits[j], nodes[j + 1], false);
  }

  // PUN: series groups, each group is parallel PMOS (dual of a PDN term).
  std::string prev = "VDD";
  for (std::size_t ti = 0; ti < terms.size(); ++ti) {
    const auto lits = term_vars(terms[ti]);
    const std::string next =
        (ti + 1 == terms.size()) ? "Y" : ("pu_" + std::to_string(ti));
    for (const auto &g : lits)
      add(prev, g, next, true);
    prev = next;
  }

  // Formal kappa-linearity invariant: gates are inputs, and no signal input
  // appears as a source/drain terminal (that would be a pass transistor).
  for (const auto &dev : c.devices) {
    if (std::find(vars.begin(), vars.end(), dev.gate) == vars.end())
      throw std::runtime_error("static CMOS: gate is not an input");
    if (std::find(vars.begin(), vars.end(), dev.drain) != vars.end() ||
        std::find(vars.begin(), vars.end(), dev.source) != vars.end())
      throw std::runtime_error(
          "static CMOS: signal input used as channel terminal");
  }
  return c;
}

} // namespace dcstudy
