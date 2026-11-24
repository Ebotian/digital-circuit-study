#include "synthesis/compose.hpp"
#include "synthesis/static_exact.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace dcstudy {

namespace {
constexpr std::size_t kMaxGlueSignals = 12;
constexpr int kMux2FallbackCost = 12;
} // namespace
namespace {

namespace fs = std::filesystem;

// A reduced ordered BDD over the spec's variable order.  Terminals are 0/1;
// node ids >= 2 point into `nodes`.
struct Node {
  int var = 0;
  int lo = 0;
  int hi = 0;
};

struct Bdd {
  std::vector<Node> nodes{Node{}, Node{}}; // 0 = zero, 1 = one
  std::map<std::tuple<int, int, int>, int> unique;

  bool terminal(int id) const { return id < 2; }

  int mk(int var, int lo, int hi) {
    if (lo == hi)
      return lo;
    const auto key = std::make_tuple(var, lo, hi);
    const auto it = unique.find(key);
    if (it != unique.end())
      return it->second;
    const int id = static_cast<int>(nodes.size());
    nodes.push_back(Node{var, lo, hi});
    unique.emplace(key, id);
    return id;
  }

  int build_range(const unsigned char *base, std::size_t size, int level) {
    bool same = true;
    for (std::size_t i = 1; i < size; ++i)
      if (base[i] != base[0]) {
        same = false;
        break;
      }
    if (same)
      return base[0] ? 1 : 0;
    const std::size_t half = size / 2;
    return mk(level, build_range(base, half, level + 1),
              build_range(base + half, half, level + 1));
  }

  int build(const std::vector<unsigned char> &table, int n, int level) {
    (void)n;
    return build_range(table.data(), table.size(), level);
  }
};

std::string temp_dir() {
  static std::atomic<int> counter{0};
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path p =
      fs::temp_directory_path() / ("dcstudy-compose-" + std::to_string(stamp) +
                                   "-" + std::to_string(counter++) + ".d");
  fs::create_directories(p);
  return p.string();
}

} // namespace

namespace {

// Greedy library composition: pick atom instances that reduce the number of
// output conflicts (row pairs with equal selected signals but different
// outputs), then synthesize the remaining glue as a mux2 network.
struct Cand {
  int atom = -1;
  std::vector<std::size_t> vars;                // target variable indices
  std::vector<std::vector<unsigned char>> outs; // [output][row]
};

int bdd_mux_count(const MultiProblem &h) {
  Bdd bdd;
  const std::size_t rows = std::size_t{1} << h.vars.size();
  for (const auto &po : h.outputs) {
    std::vector<unsigned char> table(rows);
    for (std::size_t m = 0; m < rows; ++m)
      table[m] = po.func.value(static_cast<std::uint32_t>(m)) ? 1 : 0;
    (void)bdd.build(table, static_cast<int>(h.vars.size()), 0);
  }
  int count = 0;
  for (std::size_t id = 2; id < bdd.nodes.size(); ++id) {
    const Node &nd = bdd.nodes[id];
    const bool ident = nd.lo == 0 && nd.hi == 1;
    const bool inv = nd.lo == 1 && nd.hi == 0;
    if (!ident && !inv)
      ++count;
  }
  return count;
}

std::string atom_module_rtl(const MultiProblem &atom) {
  std::ostringstream os;
  os << "module " << atom.title << "(";
  for (std::size_t i = 0; i < atom.vars.size(); ++i)
    os << (i ? ", " : "") << "input logic " << atom.vars[i];
  for (std::size_t i = 0; i < atom.names.size(); ++i)
    os << ", output logic " << atom.names[i];
  os << ");\n";
  for (std::size_t k = 0; k < atom.outputs.size(); ++k)
    os << "  assign " << atom.names[k] << " = "
       << atom.outputs[k].func.sop(atom.vars) << ";\n";
  os << "endmodule\n";
  return os.str();
}

} // namespace

// Complete a partially-specified truth table: subtrees without care rows are
// filled to match their sibling, so the BDD collapses free coordinates.
bool fill_dc(unsigned char *val, const unsigned char *care, std::size_t size,
             int level) {
  (void)level;
  bool any = false;
  bool same = true;
  unsigned char first = 0;
  for (std::size_t i = 0; i < size; ++i) {
    if (!care[i])
      continue;
    if (!any) {
      any = true;
      first = val[i];
    } else if (val[i] != first) {
      same = false;
    }
  }
  if (!any) {
    std::fill(val, val + size, 0);
    return false;
  }
  if (same) {
    std::fill(val, val + size, first);
    return true;
  }
  const std::size_t half = size / 2;
  const bool lo = fill_dc(val, care, half, level + 1);
  const bool hi = fill_dc(val + half, care + half, half, level + 1);
  if (!lo && hi)
    std::copy(val + half, val + size, val);
  if (!hi && lo)
    std::copy(val, val + half, val + half);
  return true;
}

std::optional<MultiProblem>
glue_from_signals(const MultiProblem &spec,
                  const std::vector<std::vector<unsigned char>> &signals,
                  const std::vector<std::size_t> &order) {
  const std::size_t n = spec.vars.size();
  const std::size_t rows = std::size_t{1} << n;
  const std::size_t ns = signals.size();
  if (ns == 0 || ns > kMaxGlueSignals)
    return std::nullopt;
  for (const auto &s : signals)
    if (s.size() != rows)
      return std::nullopt;
  const std::size_t hrows = std::size_t{1} << ns;
  std::vector<std::vector<unsigned char>> ht(
      spec.outputs.size(), std::vector<unsigned char>(hrows, 0));
  std::vector<bool> seen(hrows, false);
  for (std::size_t m = 0; m < rows; ++m) {
    std::size_t idx = 0;
    for (std::size_t j = 0; j < ns; ++j)
      if (signals[order.empty() ? j : order[j]][m])
        idx |= std::size_t{1} << j;
    if (seen[idx])
      continue;
    seen[idx] = true;
    for (std::size_t k = 0; k < spec.outputs.size(); ++k)
      ht[k][idx] =
          spec.outputs[k].func.value(static_cast<std::uint32_t>(m)) ? 1 : 0;
  }
  // Unreachable signal tuples are don't-cares: complete them so the BDD can
  // collapse free coordinates (otherwise they look like real constraints).
  std::vector<unsigned char> care(hrows, 0);
  for (std::size_t idx = 0; idx < hrows; ++idx)
    care[idx] = seen[idx] ? 1 : 0;
  for (std::size_t k = 0; k < spec.outputs.size(); ++k)
    fill_dc(ht[k].data(), care.data(), hrows, 0);

  MultiProblem h;
  h.title = "glue";
  for (std::size_t j = 0; j < ns; ++j) {
    const std::size_t src = order.empty() ? (ns - 1 - j) : order[ns - 1 - j];
    h.vars.push_back("s" + std::to_string(src));
  }
  h.names = spec.names;
  for (std::size_t k = 0; k < spec.outputs.size(); ++k) {
    Bits bits(hrows);
    for (std::size_t j = 0; j < hrows; ++j)
      if (ht[k][j])
        bits.set(j);
    h.outputs.push_back({BoolFunc(ns, bits), Bits{}});
  }
  return h;
}

std::optional<ComposeResult>
compose_from_library(const MultiProblem &spec,
                     const std::vector<MultiProblem> &library) {
  const std::size_t n = spec.vars.size();
  const std::size_t rows = std::size_t{1} << n;
  if (library.empty() || spec.outputs.empty())
    return std::nullopt;

  std::vector<unsigned char> tgt_rows(spec.outputs.size() * rows);
  for (std::size_t k = 0; k < spec.outputs.size(); ++k)
    for (std::size_t m = 0; m < rows; ++m)
      tgt_rows[k * rows + m] =
          spec.outputs[k].func.value(static_cast<std::uint32_t>(m)) ? 1 : 0;
  std::vector<std::vector<unsigned char>> tvars(
      n, std::vector<unsigned char>(rows));
  for (std::size_t v = 0; v < n; ++v)
    for (std::size_t m = 0; m < rows; ++m)
      tvars[v][m] = (m >> (n - 1 - v)) & 1u;

  std::vector<Cand> cands;
  std::set<std::string> cand_sig;
  int glue_mux_cost = kMux2FallbackCost; // used only if no measured mux2
  std::vector<int> atom_cost(library.size(), -1);
  for (std::size_t ai = 0; ai < library.size(); ++ai) {
    const MultiProblem &atom = library[ai];
    const std::size_t k = atom.vars.size();
    if (k == 0 || k > n)
      continue;
    // <=4-input atoms can be costed by the exact backend (no Yosys process);
    // the final mapping still re-measures every atom it actually uses.
    int cost = -1;
    if (atom.vars.size() <= 4) {
      const StaticExactResult ex = synthesize_static_exact_multi(atom);
      if (ex.ok && ex.verified)
        cost = ex.total;
    }
    if (cost < 0) {
      const MultiStageMultiDesign am = synthesize_multistage_static_multi(atom);
      if (!am.ok || !am.verified)
        continue;
      cost = am.total;
    }
    atom_cost[ai] = cost;
    if (atom.title == "mux2") {
      const MultiStageMultiDesign am = synthesize_multistage_static_multi(atom);
      if (am.ok && am.verified)
        glue_mux_cost = am.total;
    }
    std::vector<std::size_t> comb(k);
    std::iota(comb.begin(), comb.end(), 0);
    while (true) {
      Cand c;
      c.atom = static_cast<int>(ai);
      c.vars = comb;
      c.outs.assign(atom.outputs.size(), std::vector<unsigned char>(rows));
      for (std::size_t m = 0; m < rows; ++m) {
        std::uint32_t idx = 0;
        for (std::size_t j = 0; j < k; ++j)
          idx = (idx << 1) | tvars[comb[j]][m];
        for (std::size_t o = 0; o < atom.outputs.size(); ++o)
          c.outs[o][m] = atom.outputs[o].func.value(idx) ? 1 : 0;
      }
      auto add_candidate = [&](Cand &&cand) {
        std::string key = std::to_string(cand.atom) + ":";
        for (const auto &o : cand.outs)
          key.append(reinterpret_cast<const char *>(o.data()), o.size());
        if (cand_sig.insert(std::move(key)).second)
          cands.push_back(std::move(cand));
      };
      add_candidate(std::move(c));
      if (k == 4 && n == 3 * k) {
        std::vector<std::size_t> rv(comb.rbegin(), comb.rend());
        Cand rc;
        rc.atom = static_cast<int>(ai);
        rc.vars = rv;
        rc.outs.assign(atom.outputs.size(), std::vector<unsigned char>(rows));
        for (std::size_t m = 0; m < rows; ++m) {
          std::uint32_t idx = 0;
          for (std::size_t j = 0; j < k; ++j)
            idx = (idx << 1) | tvars[rv[j]][m];
          for (std::size_t o = 0; o < atom.outputs.size(); ++o)
            rc.outs[o][m] = atom.outputs[o].func.value(idx) ? 1 : 0;
        }
        add_candidate(std::move(rc));
      }
      // permutations for small atoms (k<=3); k=4 stayed identity because the
      // 24x candidate growth outweighed the search benefit on the current set.
      if (k <= 3) {
        auto perm = comb;
        while (std::next_permutation(perm.begin(), perm.end())) {
          Cand pc;
          pc.atom = static_cast<int>(ai);
          pc.vars = perm;
          pc.outs.assign(atom.outputs.size(), std::vector<unsigned char>(rows));
          for (std::size_t m = 0; m < rows; ++m) {
            std::uint32_t idx = 0;
            for (std::size_t j = 0; j < k; ++j)
              idx = (idx << 1) | tvars[perm[j]][m];
            for (std::size_t o = 0; o < atom.outputs.size(); ++o)
              pc.outs[o][m] = atom.outputs[o].func.value(idx) ? 1 : 0;
          }
          add_candidate(std::move(pc));
        }
      }
      // next combination
      std::size_t i = k;
      while (i > 0) {
        --i;
        if (comb[i] + 1 < n - (k - 1 - i)) {
          ++comb[i];
          break;
        }
        if (i == 0) {
          comb.clear();
          break;
        }
      }
      if (comb.empty())
        break;
      for (std::size_t j = i + 1; j < k; ++j)
        comb[j] = comb[j - 1] + 1;
    }
  }
  if (cands.empty())
    return std::nullopt;

  auto conflicts =
      [&](const std::vector<const std::vector<unsigned char> *> &sigs) {
        long long total = 0;
        for (std::size_t k = 0; k < spec.outputs.size(); ++k) {
          std::unordered_map<std::uint64_t, std::pair<int, int>> buckets;
          for (std::size_t m = 0; m < rows; ++m) {
            std::uint64_t key = 0;
            for (const auto *s : sigs)
              key = (key << 1) | ((*s)[m] ? 1u : 0u);
            auto &b = buckets[key];
            if (tgt_rows[k * rows + m])
              ++b.second;
            else
              ++b.first;
          }
          for (const auto &[_, b] : buckets)
            total += 1LL * b.first * b.second;
        }
        return total;
      };

  auto build_h =
      [&](const std::vector<const std::vector<unsigned char> *> &sigs,
          const std::vector<std::size_t> &order = {})
      -> std::optional<MultiProblem> {
    std::vector<std::vector<unsigned char>> tables;
    tables.reserve(sigs.size());
    for (const auto *s : sigs)
      tables.push_back(*s);
    return glue_from_signals(spec, tables, order);
  };

  auto best_h = [&](const std::vector<const std::vector<unsigned char> *> &sigs)
      -> std::pair<std::optional<MultiProblem>, int> {
    const std::size_t ns = sigs.size();
    std::optional<MultiProblem> best;
    int best_count = std::numeric_limits<int>::max();
    std::vector<std::size_t> id(ns);
    std::iota(id.begin(), id.end(), 0);
    std::vector<std::vector<std::size_t>> orders;
    orders.push_back(id);
    for (std::size_t j = 0; j < ns; ++j) {
      std::vector<std::size_t> o;
      o.reserve(ns);
      o.push_back(j);
      for (std::size_t k = 0; k < ns; ++k)
        if (k != j)
          o.push_back(k);
      orders.push_back(std::move(o));
    }
    std::vector<std::size_t> rev(id.rbegin(), id.rend());
    orders.push_back(std::move(rev));
    for (const auto &order : orders) {
      auto h = build_h(sigs, order);
      if (!h)
        continue;
      const int c = bdd_mux_count(*h);
      if (c < best_count) {
        best = std::move(h);
        best_count = c;
      }
    }
    return {best, best_count};
  };

  // Partition constructor for a multi-output atom of width k on n = 2k:
  // two disjoint groups + optional single-output atoms, costed exactly.
  std::optional<std::vector<int>> partition_best;
  int partition_total = std::numeric_limits<int>::max();
  for (std::size_t ai = 0; ai < library.size(); ++ai) {
    const std::size_t k = library[ai].vars.size();
    if (library[ai].outputs.size() < 2 || k < 2 || n != 2 * k)
      continue;
    std::vector<int> molist, solist;
    for (std::size_t ci = 0; ci < cands.size(); ++ci) {
      if (cands[ci].atom == static_cast<int>(ai))
        molist.push_back(static_cast<int>(ci));
      else if (cands[ci].vars.size() == k &&
               library[cands[ci].atom].outputs.size() == 1)
        solist.push_back(static_cast<int>(ci));
    }
    for (std::size_t i = 0; i < molist.size(); ++i)
      for (std::size_t j = i + 1; j < molist.size(); ++j) {
        const auto &a = cands[molist[i]].vars;
        const auto &b = cands[molist[j]].vars;
        std::vector<bool> used(n, false);
        bool ok = true;
        for (auto v : a) {
          if (used[v])
            ok = false;
          used[v] = true;
        }
        for (auto v : b) {
          if (used[v])
            ok = false;
          used[v] = true;
        }
        for (bool u : used)
          ok = ok && u;
        if (!ok)
          continue;
        for (int mask = 0; mask < 4; ++mask) {
          std::vector<int> plan{molist[i], molist[j]};
          for (int g = 0; g < 2; ++g) {
            if (!(mask & (1 << g)))
              continue;
            for (int ci : solist) {
              if (cands[ci].vars != (g == 0 ? a : b))
                continue;
              plan.push_back(ci);
              break;
            }
          }
          std::vector<const std::vector<unsigned char> *> sv;
          int cost = 0;
          for (int ci : plan) {
            for (const auto &o : cands[ci].outs)
              sv.push_back(&o);
            cost += atom_cost[cands[ci].atom];
          }
          if (conflicts(sv) != 0)
            continue;
          const auto [h, mux] = best_h(sv);
          if (!h)
            continue;
          const int total = cost + glue_mux_cost * mux;
          if (total < partition_total) {
            partition_total = total;
            partition_best = plan;
          }
        }
      }
  }

  std::vector<int> chosen;
  std::vector<const std::vector<unsigned char> *> sigs;
  long long cur = conflicts(sigs);
  if (partition_best) {
    chosen = *partition_best;
    for (int ci : chosen)
      for (const auto &o : cands[ci].outs)
        sigs.push_back(&o);
    cur = 0;
  }

  // g=3 direct cover: atom output tables that equal spec output tables.
  if (!partition_best) {
    for (std::size_t ai = 0; ai < library.size(); ++ai) {
      const std::size_t k = library[ai].vars.size();
      if (library[ai].outputs.size() < 2 || n != 3 * k)
        continue;
      std::map<std::string, int> spec_key;
      for (std::size_t o = 0; o < spec.outputs.size(); ++o)
        spec_key[std::string(
            reinterpret_cast<const char *>(tgt_rows.data() + o * rows), rows)] =
            static_cast<int>(o);
      struct Hit {
        int ci;
        unsigned mask;
      };
      std::vector<Hit> hits;
      std::set<std::pair<int, std::string>> hit_seen;
      for (std::size_t ci = 0; ci < cands.size(); ++ci) {
        if (cands[ci].atom != static_cast<int>(ai))
          continue;
        unsigned mask = 0;
        bool all = true;
        for (const auto &o : cands[ci].outs) {
          auto it = spec_key.find(
              std::string(reinterpret_cast<const char *>(o.data()), rows));
          if (it == spec_key.end() || (mask & (1u << it->second))) {
            all = false;
            break;
          }
          mask |= 1u << it->second;
        }
        if (!all)
          continue;
        std::vector<std::size_t> key_vars = cands[ci].vars;
        std::sort(key_vars.begin(), key_vars.end());
        std::string key(reinterpret_cast<const char *>(key_vars.data()),
                        key_vars.size() * sizeof(std::size_t));
        if (hit_seen.emplace(static_cast<int>(mask), key).second)
          hits.push_back({static_cast<int>(ci), mask});
      }
      const unsigned full_mask = (1u << spec.outputs.size()) - 1u;
      // Expand each hit over its don't-care inputs: an atom output may not
      // depend on every input (pe42 ignores a0), so the same function can
      // occupy any k-set containing its essential vars. The unused slots are
      // filled with the vars left over by the other groups.
      std::vector<Hit> expanded;
      for (const auto &hh : hits) {
        const Cand &c = cands[hh.ci];
        std::vector<int> free_pos;
        std::vector<std::size_t> essential;
        for (std::size_t j = 0; j < k; ++j) {
          bool dep = false;
          const std::uint32_t bit = 1u << (n - 1 - c.vars[j]);
          for (std::size_t m = 0; m < rows && !dep; ++m)
            for (const auto &o : c.outs)
              if (o[m] != o[m ^ bit]) {
                dep = true;
                break;
              }
          if (dep)
            essential.push_back(c.vars[j]);
          else
            free_pos.push_back(static_cast<int>(j));
        }
        if (!free_pos.empty()) {
          std::vector<std::size_t> free_vars;
          for (std::size_t v = 0; v < n; ++v)
            if (std::find(essential.begin(), essential.end(), v) ==
                essential.end())
              free_vars.push_back(v);
          std::vector<std::size_t> pick(free_pos.size());
          std::function<void(std::size_t, std::size_t)> rec =
              [&](std::size_t at, std::size_t from) {
                if (at == pick.size()) {
                  Cand nc = c;
                  for (std::size_t t = 0; t < pick.size(); ++t)
                    nc.vars[free_pos[t]] = pick[t];
                  cands.push_back(std::move(nc));
                  expanded.push_back(
                      {static_cast<int>(cands.size() - 1), hh.mask});
                  return;
                }
                for (std::size_t v = from; v < free_vars.size(); ++v) {
                  pick[at] = free_vars[v];
                  rec(at + 1, v + 1);
                }
              };
          rec(0, 0);
        } else {
          expanded.push_back(hh);
        }
      }
      hits = std::move(expanded);
      for (std::size_t i = 0; i < hits.size() && !partition_best; ++i)
        for (std::size_t j = i + 1; j < hits.size() && !partition_best; ++j)
          for (std::size_t l = j + 1; l < hits.size() && !partition_best; ++l) {
            if ((hits[i].mask & hits[j].mask) ||
                (hits[i].mask & hits[l].mask) || (hits[j].mask & hits[l].mask))
              continue;
            if ((hits[i].mask | hits[j].mask | hits[l].mask) != full_mask)
              continue;
            std::vector<int> plan{hits[i].ci, hits[j].ci, hits[l].ci};
            std::vector<bool> used(n, false);
            bool ok = true;
            for (int ci : plan) {
              for (auto v : cands[ci].vars) {
                if (used[v])
                  ok = false;
                used[v] = true;
              }
            }
            for (bool u : used)
              ok = ok && u;
            if (!ok)
              continue;
            std::vector<const std::vector<unsigned char> *> sv;
            for (int ci : plan)
              for (const auto &o : cands[ci].outs)
                sv.push_back(&o);
            if (conflicts(sv) != 0)
              continue;
            const auto [h, mux] = best_h(sv);
            if (!h)
              continue;
            partition_best = plan;
          }
    }
  }

  if (!partition_best) {
    struct State {
      std::vector<int> chosen;
      long long conf = 0;
      int cost = 0;
    };
    std::vector<State> beam{
        {},
    };
    beam[0].conf = conflicts({});
    const int width =
        std::min(16, std::max(4, static_cast<int>(std::sqrt(cands.size()))));
    std::optional<State> best_state;
    for (int depth = 0; depth < 6 && !beam.empty(); ++depth) {
      std::vector<State> next;
      std::set<std::string> seen;
      for (const auto &st : beam) {
        for (std::size_t ci = 0; ci < cands.size(); ++ci) {
          if (std::find(st.chosen.begin(), st.chosen.end(),
                        static_cast<int>(ci)) != st.chosen.end())
            continue;
          State ns = st;
          ns.chosen.push_back(static_cast<int>(ci));
          ns.cost += atom_cost[cands[ci].atom];
          std::vector<const std::vector<unsigned char> *> sv;
          for (int x : ns.chosen)
            for (const auto &o : cands[x].outs)
              sv.push_back(&o);
          ns.conf = conflicts(sv);
          if (ns.conf == 0) {
            if (!best_state || ns.cost < best_state->cost)
              best_state = ns;
            continue;
          }
          std::string key;
          for (int x : ns.chosen)
            key += std::to_string(x) + ",";
          if (!seen.insert(key).second)
            continue;
          next.push_back(std::move(ns));
        }
      }
      std::sort(next.begin(), next.end(), [](const State &a, const State &b) {
        return a.conf != b.conf ? a.conf < b.conf : a.cost < b.cost;
      });
      if (static_cast<int>(next.size()) > width)
        next.resize(width);
      beam = std::move(next);
    }
    if (best_state) {
      chosen = best_state->chosen;
      for (int ci : chosen)
        for (const auto &o : cands[ci].outs)
          sigs.push_back(&o);
      cur = 0;
    }
  }
  while (cur > 0 && sigs.size() < kMaxGlueSignals) {
    int best = -1;
    long long best_conf = cur;
    int best_cost = std::numeric_limits<int>::max();
    for (std::size_t ci = 0; ci < cands.size(); ++ci) {
      if (std::find(chosen.begin(), chosen.end(), static_cast<int>(ci)) !=
          chosen.end())
        continue;
      std::vector<const std::vector<unsigned char> *> tmp = sigs;
      for (const auto &o : cands[ci].outs)
        tmp.push_back(&o);
      const long long c = conflicts(tmp);
      const int cost = atom_cost[cands[ci].atom];
      if (c < best_conf || (c == best_conf && cost < best_cost)) {
        best = static_cast<int>(ci);
        best_conf = c;
        best_cost = cost;
      }
    }
    if (best < 0 || best_conf >= cur)
      break;
    chosen.push_back(best);
    for (const auto &o : cands[best].outs)
      sigs.push_back(&o);
    cur = best_conf;
  }
  if (cur != 0 || chosen.empty())
    return std::nullopt;

  const auto glue_res = best_h(sigs);
  if (!glue_res.first)
    return std::nullopt;
  const MultiProblem &glue_problem = *glue_res.first;
  const ComposeResult glue = compose_mux2(glue_problem, {});

  const std::size_t ns = sigs.size();

  // Emit the final top: atom instances + the glue module.
  std::ostringstream os;
  os << "module top(";
  for (std::size_t i = 0; i < n; ++i)
    os << (i ? ", " : "") << "input logic " << spec.vars[i];
  for (std::size_t i = 0; i < spec.names.size(); ++i)
    os << ", output logic " << spec.names[i];
  os << ");\n";
  for (std::size_t j = 0; j < ns; ++j)
    os << "  wire s" << j << ";\n";
  std::ostringstream atoms_rtl;
  std::set<std::string> emitted;
  std::size_t out_idx = 0;
  for (std::size_t i = 0; i < chosen.size(); ++i) {
    const Cand &c = cands[chosen[i]];
    const MultiProblem &atom = library[c.atom];
    os << "  " << atom.title << " a" << i << "(";
    for (std::size_t j = 0; j < c.vars.size(); ++j)
      os << (j ? ", " : "") << spec.vars[c.vars[j]];
    for (std::size_t o = 0; o < c.outs.size(); ++o)
      os << ", s" << (out_idx++);
    os << ");\n";
    if (emitted.insert(atom.title).second)
      atoms_rtl << atom_module_rtl(atom) << "\n";
  }
  // Inline the glue network (mux2 instances) into the top module; the flow
  // blackboxes mux2, so the glue is charged by the atom measurement.
  const std::string &glue_rtl = glue.rtl;
  const auto body_begin = glue_rtl.find(");");
  const auto body_end = glue_rtl.rfind("endmodule");
  if (body_begin == std::string::npos || body_end == std::string::npos)
    return std::nullopt;
  os << glue_rtl.substr(body_begin + 2, body_end - (body_begin + 2));
  os << "endmodule\n";
  atoms_rtl << "module mux2(input logic A, B, S, output logic Y);\n"
               "  assign Y = (S & B) | (~S & A);\nendmodule\n";

  ComposeResult out;
  out.rtl = os.str();
  out.atom_rtl = atoms_rtl.str();
  out.nodes = static_cast<int>(ns);
  const std::string dir = temp_dir();
  const fs::path top_path = fs::path(dir) / "top.v";
  const fs::path atom_path = fs::path(dir) / "atoms.v";
  {
    std::ofstream(top_path) << out.rtl;
    std::ofstream(atom_path) << out.atom_rtl;
  }
  try {
    out.synth = synthesize_rtl_atoms(top_path.string(), atom_path.string(),
                                     spec, "", 0, true);
    out.ok = out.synth.ok;
    out.verified = out.synth.verified;
    if (!out.synth.ok)
      out.reason = out.synth.reason;
  } catch (const std::exception &e) {
    out.reason = e.what();
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
  return out;
}

ComposeResult compose_mux2(const MultiProblem &spec,
                           const std::vector<MultiProblem> &library) {
  ComposeResult out;
  if (spec.vars.empty() || spec.outputs.empty()) {
    out.reason = "compose needs a non-empty spec";
    return out;
  }
  if (spec.has_dont_cares()) {
    out.reason = "compose does not support don't-care masks yet";
    return out;
  }

  if (const auto lib = compose_from_library(spec, library); lib)
    return *lib;

  // Exact whole-function match first: one atom instance, no search needed.
  for (const auto &atom : library) {
    if (atom.vars.size() != spec.vars.size() ||
        atom.outputs.size() != spec.outputs.size())
      continue;
    bool same = true;
    const std::size_t at_rows = std::size_t{1} << atom.vars.size();
    for (std::size_t k = 0; k < atom.outputs.size() && same; ++k)
      for (std::size_t m = 0; m < at_rows && same; ++m)
        if (atom.outputs[k].func.value(static_cast<std::uint32_t>(m)) !=
            spec.outputs[k].func.value(static_cast<std::uint32_t>(m)))
          same = false;
    if (!same)
      continue;

    std::ostringstream top;
    top << "module top(";
    for (std::size_t i = 0; i < spec.vars.size(); ++i)
      top << (i ? ", " : "") << "input logic " << spec.vars[i];
    for (std::size_t i = 0; i < spec.names.size(); ++i)
      top << ", output logic " << spec.names[i];
    top << ");\n  " << atom.title << " u(";
    for (std::size_t i = 0; i < atom.vars.size(); ++i)
      top << (i ? ", " : "") << "." << atom.vars[i] << "(" << spec.vars[i]
          << ")";
    for (std::size_t i = 0; i < atom.names.size(); ++i)
      top << ", ." << atom.names[i] << "(" << spec.names[i] << ")";
    top << ");\nendmodule\n";

    std::ostringstream at;
    at << "module " << atom.title << "(";
    for (std::size_t i = 0; i < atom.vars.size(); ++i)
      at << (i ? ", " : "") << "input logic " << atom.vars[i];
    for (std::size_t i = 0; i < atom.names.size(); ++i)
      at << ", output logic " << atom.names[i];
    at << ");\n";
    for (std::size_t k = 0; k < atom.outputs.size(); ++k)
      at << "  assign " << atom.names[k] << " = "
         << atom.outputs[k].func.sop(atom.vars) << ";\n";
    at << "endmodule\n";

    out.rtl = top.str();
    out.atom_rtl = at.str();
    out.nodes = 1;
    const std::string dir = temp_dir();
    const fs::path top_path = fs::path(dir) / "top.v";
    const fs::path atom_path = fs::path(dir) / (atom.title + ".v");
    {
      std::ofstream(top_path) << out.rtl;
      std::ofstream(atom_path) << out.atom_rtl;
    }
    try {
      out.synth =
          synthesize_rtl_atoms(top_path.string(), atom_path.string(), spec);
      out.ok = out.synth.ok;
      out.verified = out.synth.verified;
      if (!out.synth.ok)
        out.reason = out.synth.reason;
    } catch (const std::exception &e) {
      out.reason = e.what();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    return out;
  }

  Bdd bdd;
  std::vector<int> roots;
  const std::size_t rows = std::size_t{1} << spec.vars.size();
  for (const auto &po : spec.outputs) {
    std::vector<unsigned char> table(rows);
    for (std::size_t m = 0; m < rows; ++m)
      table[m] = po.func.value(static_cast<std::uint32_t>(m)) ? 1 : 0;
    roots.push_back(bdd.build(table, static_cast<int>(spec.vars.size()), 0));
  }

  // A BDD node whose function is a single literal is just that signal: no mux
  // instance.  (Otherwise Y = S?B:A would expand into muxes for A and B too.)
  auto is_identity = [&](int id) {
    const Node &nd = bdd.nodes[id];
    return nd.lo == 0 && nd.hi == 1;
  };
  auto is_inverted = [&](int id) {
    const Node &nd = bdd.nodes[id];
    return nd.lo == 1 && nd.hi == 0;
  };
  std::vector<int> mux_nodes;
  for (std::size_t id = 2; id < bdd.nodes.size(); ++id)
    if (!is_identity(static_cast<int>(id)) &&
        !is_inverted(static_cast<int>(id)))
      mux_nodes.push_back(static_cast<int>(id));

  std::ostringstream os;
  os << "module top(";
  for (std::size_t i = 0; i < spec.vars.size(); ++i)
    os << (i ? ", " : "") << "input logic " << spec.vars[i];
  for (std::size_t i = 0; i < spec.names.size(); ++i)
    os << ", output logic " << spec.names[i];
  os << ");\n";
  for (int id : mux_nodes)
    os << "  wire n" << id << ";\n";

  std::function<std::string(int)> expr = [&](int id) -> std::string {
    if (id == 0)
      return "1'b0";
    if (id == 1)
      return "1'b1";
    const Node &nd = bdd.nodes[id];
    if (is_identity(id))
      return spec.vars[nd.var];
    if (is_inverted(id))
      return "~" + spec.vars[nd.var];
    return "n" + std::to_string(id);
  };
  for (int id : mux_nodes) {
    const Node &nd = bdd.nodes[id];
    os << "  mux2 u" << id << "(.A(" << expr(nd.lo) << "), .B(" << expr(nd.hi)
       << "), .S(" << spec.vars[nd.var] << "), .Y(n" << id << "));\n";
  }
  for (std::size_t k = 0; k < roots.size(); ++k)
    os << "  assign " << spec.names[k] << " = " << expr(roots[k]) << ";\n";
  os << "endmodule\n";

  out.rtl = os.str();
  out.nodes = static_cast<int>(mux_nodes.size());

  // Exhaustively evaluate the composed network against the spec rows; this is
  // the only check that stays feasible when the truth table is above the
  // Yosys SOP reference limit.
  std::function<bool(int, std::uint32_t)> eval = [&](int id,
                                                     std::uint32_t m) -> bool {
    if (id < 2)
      return id == 1;
    const Node &nd = bdd.nodes[id];
    const bool bit =
        (m >> (spec.vars.size() - 1 - static_cast<std::size_t>(nd.var))) & 1u;
    return eval(bit ? nd.hi : nd.lo, m);
  };
  bool self_check = true;
  for (std::size_t k = 0; k < roots.size() && self_check; ++k)
    for (std::size_t m = 0; m < rows; ++m)
      if (eval(roots[k], static_cast<std::uint32_t>(m)) !=
          spec.outputs[k].func.value(static_cast<std::uint32_t>(m))) {
        self_check = false;
        out.reason = "compose self-check failed at output " + spec.names[k];
        break;
      }
  if (!self_check)
    return out;
  out.atom_rtl = "module mux2(input logic A, B, S, output logic Y);\n"
                 "  assign Y = (S & B) | (~S & A);\nendmodule\n";

  const std::string dir = temp_dir();
  const fs::path top = fs::path(dir) / "top.v";
  const fs::path atoms = fs::path(dir) / "mux2.v";
  const fs::path gold = fs::path(dir) / "golden.v";
  {
    std::ofstream(top) << out.rtl;
    std::ofstream(atoms) << out.atom_rtl;
  }
  // Above kMaxArity-1 the SOP reference is too deep for Yosys; use a
  // behavioural golden with the same BDD (which the self-check above tied to
  // the spec) and verify the mapped netlist against it structurally.
  if (spec.vars.size() > kMaxGlueSignals) {
    std::function<std::string(int)> tern = [&](int id) -> std::string {
      if (id == 0)
        return "1'b0";
      if (id == 1)
        return "1'b1";
      const Node &nd = bdd.nodes[id];
      if (is_identity(id))
        return spec.vars[nd.var];
      if (is_inverted(id))
        return "~" + spec.vars[nd.var];
      return "(" + spec.vars[nd.var] + " ? " + tern(nd.hi) + " : " +
             tern(nd.lo) + ")";
    };
    std::ostringstream g;
    g << "module golden(";
    for (std::size_t i = 0; i < spec.vars.size(); ++i)
      g << (i ? ", " : "") << "input logic " << spec.vars[i];
    for (std::size_t i = 0; i < spec.names.size(); ++i)
      g << ", output logic " << spec.names[i];
    g << ");\n";
    for (std::size_t k = 0; k < roots.size(); ++k)
      g << "  assign " << spec.names[k] << " = " << tern(roots[k]) << ";\n";
    g << "endmodule\n";
    std::ofstream(gold) << g.str();
  }
  try {
    if (spec.vars.size() > kMaxGlueSignals)
      out.synth = synthesize_rtl_atoms(top.string(), atoms.string(),
                                       MultiProblem{}, gold.string());
    else
      out.synth = synthesize_rtl_atoms(top.string(), atoms.string(), spec);
    out.ok = out.synth.ok;
    out.verified = out.synth.verified && self_check;
    if (!out.synth.ok)
      out.reason = out.synth.reason;
  } catch (const std::exception &e) {
    out.reason = e.what();
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
  return out;
}

} // namespace dcstudy
