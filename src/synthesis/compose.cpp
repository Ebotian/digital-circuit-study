#include "synthesis/compose.hpp"
#include "synthesis/static_exact.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
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

  struct TableRange {
    const unsigned char *base;
    std::size_t size;
  };

  int build_range(TableRange range, int level) {
    const unsigned char *base = range.base;
    const std::size_t size = range.size;
    bool same = true;
    for (std::size_t i = 1; i < size; ++i)
      if (base[i] != base[0]) {
        same = false;
        break;
      }
    if (same)
      return base[0] ? 1 : 0;
    const std::size_t half = size / 2;
    return mk(level, build_range({base, half}, level + 1),
              build_range({base + half, half}, level + 1));
  }

  int build(const std::vector<unsigned char> &table, int level) {
    return build_range({table.data(), table.size()}, level);
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

// One library instance wired to a set of spec inputs, with the signal table of
// every output.  The search prices these, keeps the cheapest instance per
// distinct signal tuple, and then looks for the cheapest set whose signals
// determine all spec outputs; the undetermined rest becomes mux2 glue.
struct Cand {
  int atom = -1;
  int cost = 0;
  std::vector<std::size_t> vars;                // atom var -> spec var
  std::vector<std::vector<unsigned char>> outs; // [output][row]
};

int bdd_mux_count(const MultiProblem &h) {
  Bdd bdd;
  const std::size_t rows = std::size_t{1} << h.vars.size();
  for (const auto &po : h.outputs) {
    std::vector<unsigned char> table(rows);
    for (std::size_t m = 0; m < rows; ++m)
      table[m] = po.func.value(static_cast<std::uint32_t>(m)) ? 1 : 0;
    (void)bdd.build(table, 0);
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
FuncSignature canonical_signature(const std::vector<unsigned char> &table,
                                  std::size_t n) {
  FuncSignature sig;
  const std::size_t rows = std::size_t{1} << n;
  if (n == 0 || table.size() < rows)
    return sig;
  std::vector<std::size_t> essential;
  for (std::size_t v = 0; v < n; ++v) {
    const std::size_t bit = std::size_t{1} << (n - 1 - v);
    for (std::size_t m = 0; m < rows; ++m) {
      if (table[m] != table[m ^ bit]) {
        essential.push_back(v);
        break;
      }
    }
  }
  const std::size_t a = essential.size();
  sig.arity = a;
  if (a == 0) {
    sig.table.assign(1, table[0]);
    sig.exact = true;
    return sig;
  }
  // f restricted to its essential variables, big-endian in ascending order.
  std::vector<unsigned char> projected(std::size_t{1} << a);
  for (std::size_t p = 0; p < projected.size(); ++p) {
    std::size_t m = 0;
    for (std::size_t i = 0; i < a; ++i)
      if (((p >> (a - 1 - i)) & 1u) != 0)
        m |= std::size_t{1} << (n - 1 - essential[i]);
    projected[p] = table[m];
  }
  sig.order = essential;
  sig.table = projected;
  sig.exact = a <= kMaxSignatureVars;
  if (!sig.exact)
    return sig;
  // Rename the essential variables: keep the order with the smallest table.
  std::vector<std::size_t> order = essential;
  while (std::next_permutation(order.begin(), order.end())) {
    std::vector<std::size_t> pos(a, 0); // essential index -> canonical slot
    for (std::size_t j = 0; j < a; ++j)
      for (std::size_t i = 0; i < a; ++i)
        if (essential[i] == order[j])
          pos[i] = j;
    std::vector<unsigned char> perm(projected.size());
    for (std::size_t p = 0; p < perm.size(); ++p) {
      std::size_t q = 0;
      for (std::size_t i = 0; i < a; ++i)
        if (((p >> (a - 1 - pos[i])) & 1u) != 0)
          q |= std::size_t{1} << (a - 1 - i);
      perm[p] = projected[q];
    }
    if (perm < sig.table) {
      sig.table = std::move(perm);
      sig.order = order;
    }
  }
  return sig;
}
// Complete a partially-specified truth table: subtrees without care rows are
// filled to match their sibling, so the BDD collapses free coordinates.
struct MaskBuffers {
  unsigned char *val;
  const unsigned char *care;
};

struct DcRange {
  std::size_t size;
  int level;
};

bool fill_dc(MaskBuffers buffers, DcRange range) {
  unsigned char *val = buffers.val;
  const unsigned char *care = buffers.care;
  const std::size_t size = range.size;
  const int level = range.level;
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
  const bool lo = fill_dc({val, care}, {half, level + 1});
  const bool hi = fill_dc({val + half, care + half}, {half, level + 1});
  if (!lo && hi)
    std::copy(val + half, val + size, val);
  if (!hi && lo)
    std::copy(val, val + half, val + half);
  return true;
}

// Signal nets in the emitted top must not collide with a spec port: an input
// named s0 plus an atom output s0 puts two drivers on one net, and the mapped
// netlist then computes something else while the tables still match.
std::string signal_prefix(const MultiProblem &spec, std::size_t count) {
  std::set<std::string> used(spec.vars.begin(), spec.vars.end());
  used.insert(spec.names.begin(), spec.names.end());
  auto clashes = [&](const std::string &prefix) {
    for (std::size_t j = 0; j < count; ++j)
      if (used.count(prefix + std::to_string(j)))
        return true;
    return false;
  };
  std::string prefix = "s";
  while (clashes(prefix))
    prefix += "x";
  return prefix;
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
    fill_dc({ht[k].data(), care.data()}, {hrows, 0});

  MultiProblem h;
  h.title = "glue";
  const std::string prefix = signal_prefix(spec, ns);
  for (std::size_t j = 0; j < ns; ++j) {
    const std::size_t src = order.empty() ? (ns - 1 - j) : order[ns - 1 - j];
    h.vars.push_back(prefix + std::to_string(src));
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
namespace {

constexpr int kUnreachable = std::numeric_limits<int>::max();
// Deterministic work caps: a default run must not depend on the machine's
// speed, and --time-budget-ms adds a wall-clock cut on top.  200k conflict
// evaluations is roughly a third of a second on the development machine.
constexpr std::size_t kMaxSearchNodes = 200000;
// Greedy starts: the empty plan, the strongest exact covers, and pairs of
// disjoint covers.  Covers are cheap to rank (one set bit per covered output),
// so a bigger seed list costs little; the pair list is capped because it grows
// quadratically.
constexpr std::size_t kMaxCoverSeeds = 16;
constexpr std::size_t kMaxCoverPairs = 4;
constexpr std::size_t kMaxGreedySteps = 6;
// Each plan evaluation builds a BDD glue network, so the lookahead sweep stops
// evaluating after this many completions; the cheapest ones found so far win.
constexpr std::size_t kMaxPlanEvaluations = 64;

// Wall-clock limit shared by every phase of the library search.  The clock is
// read on candidate and expansion boundaries only, so an unlimited run (0) does
// not pay for it in the inner loops.
struct SearchBudget {
  bool bounded = false;
  bool expired = false;
  std::chrono::steady_clock::time_point deadline;

  void start(long long ms) {
    if (ms <= 0)
      return;
    bounded = true;
    deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  }
  bool check() {
    if (!bounded || expired)
      return expired;
    if (std::chrono::steady_clock::now() >= deadline)
      expired = true;
    return expired;
  }
};

// The measured problem: atom prices, every candidate instance, and the target
// output tables.  Candidate output tables live in a vector by value, so beam
// states hold pointers into elements that never move during the search.
struct Problem {
  Problem(const MultiProblem &spec_, const std::vector<MultiProblem> &library_)
      : spec(spec_), library(library_), n(spec_.vars.size()),
        rows(std::size_t{1} << n), atom_cost(library_.size(), -1) {
    tgt_rows.resize(spec.outputs.size() * rows);
    for (std::size_t k = 0; k < spec.outputs.size(); ++k)
      for (std::size_t m = 0; m < rows; ++m)
        tgt_rows[k * rows + m] =
            spec.outputs[k].func.value(static_cast<std::uint32_t>(m)) ? 1 : 0;
    tvars.assign(n, std::vector<unsigned char>(rows));
    for (std::size_t v = 0; v < n; ++v)
      for (std::size_t m = 0; m < rows; ++m)
        tvars[v][m] = static_cast<unsigned char>((m >> (n - 1 - v)) & 1u);
  }

  const MultiProblem &spec;
  const std::vector<MultiProblem> &library;
  std::size_t n;
  std::size_t rows;
  std::vector<unsigned char> tgt_rows;
  std::vector<std::vector<unsigned char>> tvars;
  std::vector<int> atom_cost;
  std::vector<Cand> cands;
  std::unordered_map<std::string, std::size_t> cand_index;
  std::vector<unsigned> cover_mask; // spec outputs a candidate realises exactly
  std::vector<int> zero;            // conflict scratch, reused per call
  std::vector<int> one;
  std::vector<int> touched;
  std::size_t nodes = 0;
  int mux_cost = kMux2FallbackCost;
  SearchBudget budget;
};

struct Plan {
  std::vector<int> chosen;
  int atoms = 0;
  int muxes = 0;
  int total = 0;
};

struct State {
  std::vector<int> chosen;
  std::vector<const std::vector<unsigned char> *> sigs;
  long long conf = 0;
  int atoms = 0;
};

std::string signal_key(const std::vector<std::vector<unsigned char>> &outs) {
  std::string key;
  for (const auto &o : outs)
    key.append(reinterpret_cast<const char *>(o.data()), o.size());
  return key;
}

// Signals identical on every spec row are interchangeable, so only the cheapest
// instance that produces them stays in the candidate list.  This is what keeps
// the search independent of variable names: permutations that realise the same
// function collapse into one candidate.
void add_candidate(Problem &p, int ai, const std::vector<std::size_t> &vars) {
  const MultiProblem &atom = p.library[static_cast<std::size_t>(ai)];
  Cand c;
  c.atom = ai;
  c.cost = p.atom_cost[static_cast<std::size_t>(ai)];
  c.vars = vars;
  c.outs.assign(atom.outputs.size(), std::vector<unsigned char>(p.rows, 0));
  for (std::size_t m = 0; m < p.rows; ++m) {
    std::uint32_t idx = 0;
    for (std::size_t j = 0; j < vars.size(); ++j)
      idx = (idx << 1u) | static_cast<std::uint32_t>(p.tvars[vars[j]][m]);
    for (std::size_t o = 0; o < atom.outputs.size(); ++o)
      c.outs[o][m] = atom.outputs[o].func.value(idx) ? 1 : 0;
  }
  std::string key = signal_key(c.outs);
  const auto [it, fresh] = p.cand_index.emplace(std::move(key), p.cands.size());
  if (!fresh) {
    Cand &old = p.cands[it->second];
    if (c.cost < old.cost || (c.cost == old.cost && c.vars < old.vars)) {
      old.atom = ai;
      old.cost = c.cost;
      old.vars = std::move(c.vars);
    }
    return;
  }
  p.cands.push_back(std::move(c));
}

// The glue network instantiates the hard-coded mux2 module emitted below, so a
// library module called mux2 has to be that same 2:1 mux with the same ports.
// Anything else would either be redefined in atoms.v or silently mapped as the
// glue cell, so it is not a usable atom.
bool glue_mux_compatible(const MultiProblem &atom) {
  if (atom.vars.size() != 3 || atom.outputs.size() != 1)
    return false;
  if (atom.vars != std::vector<std::string>{"A", "B", "S"} ||
      atom.names.size() != 1 || atom.names[0] != "Y")
    return false;
  for (std::size_t x = 0; x < 8; ++x) {
    const bool a = (x >> 2u) & 1u;
    const bool b = (x >> 1u) & 1u;
    const bool sel = x & 1u;
    const bool expected = (sel && b) || (!sel && a);
    if (atom.outputs[0].func.value(static_cast<std::uint32_t>(x)) != expected)
      return false;
  }
  return true;
}

int measure_atom_cost(const MultiProblem &atom) {
  if (atom.vars.size() <= 4) {
    const StaticExactResult ex = synthesize_static_exact_multi(atom);
    if (ex.ok && ex.verified)
      return ex.total;
  }
  const MultiStageMultiDesign am = synthesize_multistage_static_multi(atom);
  if (!am.ok || !am.verified)
    return -1;
  return am.total;
}
void add_candidate_combinations(Problem &p, std::size_t ai) {
  const std::size_t k = p.library[ai].vars.size();
  const int a = static_cast<int>(ai);
  std::vector<std::size_t> comb(k);
  std::iota(comb.begin(), comb.end(), 0);
  while (true) {
    if (p.budget.check())
      return;
    add_candidate(p, a, comb);
    if (k <= 3) {
      std::vector<std::size_t> perm = comb;
      while (std::next_permutation(perm.begin(), perm.end())) {
        if (p.budget.check())
          return;
        add_candidate(p, a, perm);
      }
    } else if (k == 4) {
      // k=4 stays identity + reverse: the signature pass below adds exactly
      // the permutations that realise a spec output, without 24x the cost.
      std::vector<std::size_t> rev(comb.rbegin(), comb.rend());
      add_candidate(p, a, rev);
    }
    std::size_t i = k;
    while (i > 0) {
      --i;
      if (comb[i] + 1 < p.n - (k - 1 - i)) {
        ++comb[i];
        break;
      }
      if (i == 0)
        return;
    }
    for (std::size_t j = i + 1; j < k; ++j)
      comb[j] = comb[j - 1] + 1;
  }
}

// Equality of canonical tables means the atom output realises the spec output
// when essential atom var a.order[j] is wired to essential spec var t.order[j].
bool signature_match(const FuncSignature &a, const FuncSignature &t,
                     std::vector<std::size_t> &vars) {
  if (a.arity != t.arity || a.table != t.table)
    return false;
  if (a.exact != t.exact)
    return false; // canonical and non-canonical orders are incomparable
  for (std::size_t j = 0; j < a.arity; ++j)
    vars[a.order[j]] = t.order[j];
  return true;
}

// Wire the atom inputs the matched function does not depend on to the lowest
// spec variables nobody claimed yet.  They cannot change the signal value, but
// they do have to name an existing net.
std::optional<std::vector<std::size_t>>
complete_vars(const Problem &p, std::vector<std::size_t> vars,
              std::vector<bool> set) {
  std::vector<bool> used(p.n, false);
  for (std::size_t j = 0; j < vars.size(); ++j)
    if (set[j])
      used[vars[j]] = true;
  std::size_t next = 0;
  for (std::size_t j = 0; j < vars.size(); ++j) {
    if (set[j])
      continue;
    while (next < p.n && used[next])
      ++next;
    if (next >= p.n)
      return std::nullopt;
    vars[j] = next;
    used[next] = true;
    set[j] = true;
  }
  return vars;
}
void add_signature_matches(Problem &p) {
  std::vector<FuncSignature> tgt(p.spec.outputs.size());
  for (std::size_t t = 0; t < p.spec.outputs.size(); ++t) {
    const unsigned char *base = p.tgt_rows.data() + t * p.rows;
    tgt[t] = canonical_signature(
        std::vector<unsigned char>(base, base + p.rows), p.n);
  }
  for (std::size_t ai = 0; ai < p.library.size(); ++ai) {
    const MultiProblem &atom = p.library[ai];
    const std::size_t k = atom.vars.size();
    if (p.atom_cost[ai] < 0 || k == 0 || k > p.n)
      continue;
    std::vector<FuncSignature> sigs(atom.outputs.size());
    for (std::size_t o = 0; o < atom.outputs.size(); ++o) {
      std::vector<unsigned char> own(std::size_t{1} << k);
      for (std::size_t x = 0; x < own.size(); ++x)
        own[x] =
            atom.outputs[o].func.value(static_cast<std::uint32_t>(x)) ? 1 : 0;
      sigs[o] = canonical_signature(own, k);
    }
    for (std::size_t o = 0; o < sigs.size(); ++o) {
      for (std::size_t t = 0; t < tgt.size(); ++t) {
        std::vector<std::size_t> vars(k, 0);
        if (!signature_match(sigs[o], tgt[t], vars))
          continue;
        std::vector<bool> set(k, false);
        for (std::size_t j = 0; j < sigs[o].arity; ++j)
          set[sigs[o].order[j]] = true;
        if (auto full = complete_vars(p, vars, set))
          add_candidate(p, static_cast<int>(ai), *full);
      }
    }
    // One mapping consistent across every output turns a multi-output atom into
    // a single instance instead of one instance per output.
    std::vector<std::size_t> merged(k, 0);
    std::vector<bool> set(k, false);
    bool consistent = true;
    for (std::size_t o = 0; o < sigs.size() && consistent; ++o) {
      for (std::size_t t = 0; t < tgt.size(); ++t) {
        std::vector<std::size_t> vars(k, 0);
        if (!signature_match(sigs[o], tgt[t], vars))
          continue;
        bool clash = false;
        for (std::size_t j = 0; j < sigs[o].arity && !clash; ++j) {
          const std::size_t av = sigs[o].order[j];
          clash = set[av] && merged[av] != vars[av];
        }
        if (clash) {
          consistent = false;
          break;
        }
        for (std::size_t j = 0; j < sigs[o].arity; ++j) {
          const std::size_t av = sigs[o].order[j];
          merged[av] = vars[av];
          set[av] = true;
        }
        break;
      }
    }
    if (consistent) {
      if (auto full = complete_vars(p, merged, set))
        add_candidate(p, static_cast<int>(ai), *full);
    }
  }
}

void generate_candidates(Problem &p) {
  for (std::size_t ai = 0; ai < p.library.size(); ++ai) {
    const MultiProblem &atom = p.library[ai];
    if (atom.vars.empty() || atom.vars.size() > p.n)
      continue;
    if (atom.title == "mux2" && !glue_mux_compatible(atom))
      continue;
    const int cost = measure_atom_cost(atom);
    if (cost < 0)
      continue;
    p.atom_cost[ai] = cost;
    if (atom.title == "mux2")
      p.mux_cost = cost;
    add_candidate_combinations(p, ai);
  }
  if (!p.budget.expired)
    add_signature_matches(p);
}

long long
conflicts(Problem &p,
          const std::vector<const std::vector<unsigned char> *> &sigs) {
  if (sigs.size() > kMaxGlueSignals)
    return kUnreachable;
  const std::size_t buckets = std::size_t{1} << sigs.size();
  if (p.zero.size() < buckets) {
    p.zero.resize(buckets, 0);
    p.one.resize(buckets, 0);
  }
  long long total = 0;
  for (std::size_t k = 0; k < p.spec.outputs.size(); ++k) {
    p.touched.clear();
    for (std::size_t m = 0; m < p.rows; ++m) {
      std::size_t key = 0;
      for (const auto *s : sigs)
        key = (key << 1u) | ((*s)[m] ? 1u : 0u);
      if (p.zero[key] == 0 && p.one[key] == 0)
        p.touched.push_back(static_cast<int>(key));
      if (p.tgt_rows[k * p.rows + m])
        ++p.one[key];
      else
        ++p.zero[key];
    }
    // Only the buckets this output touched are cleared again.
    for (int key : p.touched) {
      const std::size_t at = static_cast<std::size_t>(key);
      total += 1LL * p.zero[at] * p.one[at];
      p.zero[at] = 0;
      p.one[at] = 0;
    }
  }
  return total;
}

std::optional<MultiProblem>
glue_for(const Problem &p,
         const std::vector<const std::vector<unsigned char> *> &sigs,
         const std::vector<std::size_t> &order) {
  std::vector<std::vector<unsigned char>> tables;
  tables.reserve(sigs.size());
  for (const auto *s : sigs)
    tables.push_back(*s);
  return glue_from_signals(p.spec, tables, order);
}

// The BDD glue cost depends on the variable order; try the natural one, each
// single signal first, and the reversed order, and keep the cheapest.
std::pair<std::optional<MultiProblem>, int>
best_glue(Problem &p,
          const std::vector<const std::vector<unsigned char> *> &sigs) {
  const std::size_t ns = sigs.size();
  std::optional<MultiProblem> best;
  int best_count = std::numeric_limits<int>::max();
  std::vector<std::size_t> id(ns);
  std::iota(id.begin(), id.end(), 0);
  std::vector<std::vector<std::size_t>> orders;
  orders.push_back(id);
  for (std::size_t j = 0; j < ns; ++j) {
    std::vector<std::size_t> first;
    first.reserve(ns);
    first.push_back(j);
    for (std::size_t k = 0; k < ns; ++k)
      if (k != j)
        first.push_back(k);
    orders.push_back(std::move(first));
  }
  std::vector<std::size_t> rev(id.rbegin(), id.rend());
  orders.push_back(std::move(rev));
  for (const auto &order : orders) {
    if (p.budget.check())
      break;
    auto h = glue_for(p, sigs, order);
    if (!h)
      continue;
    const int c = bdd_mux_count(*h);
    if (c < best_count) {
      best = std::move(h);
      best_count = c;
    }
  }
  return {best, best_count};
}

int estimate_total(Problem &p, const std::vector<int> &chosen, int *muxes) {
  std::vector<const std::vector<unsigned char> *> sigs;
  int atoms = 0;
  for (int ci : chosen) {
    const Cand &c = p.cands[static_cast<std::size_t>(ci)];
    atoms += c.cost;
    for (const auto &o : c.outs)
      sigs.push_back(&o);
  }
  if (sigs.size() > kMaxGlueSignals)
    return kUnreachable;
  if (conflicts(p, sigs) != 0)
    return kUnreachable;
  const auto [h, mux] = best_glue(p, sigs);
  if (!h)
    return kUnreachable;
  if (muxes)
    *muxes = mux;
  return atoms + p.mux_cost * mux;
}

std::optional<Plan> make_plan(Problem &p, const std::vector<int> &chosen) {
  int muxes = 0;
  const int total = estimate_total(p, chosen, &muxes);
  if (total == kUnreachable)
    return std::nullopt;
  Plan plan;
  plan.chosen = chosen;
  for (int ci : chosen)
    plan.atoms += p.cands[static_cast<std::size_t>(ci)].cost;
  plan.muxes = muxes;
  plan.total = total;
  return plan;
}

bool better_plan(const Plan &a, const Plan &b) {
  if (a.total != b.total)
    return a.total < b.total;
  if (a.chosen.size() != b.chosen.size())
    return a.chosen.size() < b.chosen.size();
  return a.chosen < b.chosen;
}

// A candidate whose output table equals a spec output table covers that output:
// the glue needs no mux for it, only a wire.  The mask makes the value of an
// exact match visible to the seed selection below.
void compute_cover_masks(Problem &p) {
  p.cover_mask.assign(p.cands.size(), 0);
  for (std::size_t ci = 0; ci < p.cands.size(); ++ci) {
    unsigned mask = 0;
    for (const auto &o : p.cands[ci].outs)
      for (std::size_t t = 0; t < p.spec.outputs.size(); ++t) {
        const unsigned char *base = p.tgt_rows.data() + t * p.rows;
        if (std::equal(o.begin(), o.end(), base))
          mask |= 1u << static_cast<unsigned>(t);
      }
    p.cover_mask[ci] = mask;
  }
}

// Extend a plan one atom at a time, always taking the candidate with the best
// conflict reduction; per_cost splits the score by transistor, so a cheap
// signal wins over an expensive one with the same effect.
std::optional<Plan> extend_greedy(Problem &p, std::vector<int> chosen,
                                  bool per_cost) {
  std::vector<const std::vector<unsigned char> *> sigs;
  for (int ci : chosen)
    for (const auto &o : p.cands[static_cast<std::size_t>(ci)].outs)
      sigs.push_back(&o);
  long long conf = conflicts(p, sigs);
  while (conf > 0) {
    if (p.budget.check() || chosen.size() >= kMaxGreedySteps)
      return std::nullopt;
    int pick = -1;
    long long pick_gain = 0;
    int pick_cost = 0;
    for (std::size_t ci = 0; ci < p.cands.size(); ++ci) {
      if (std::find(chosen.begin(), chosen.end(), static_cast<int>(ci)) !=
          chosen.end())
        continue;
      const Cand &c = p.cands[ci];
      if (sigs.size() + c.outs.size() > kMaxGlueSignals)
        continue;
      if (++p.nodes > kMaxSearchNodes)
        return std::nullopt;
      auto child = sigs;
      for (const auto &o : c.outs)
        child.push_back(&o);
      const long long gain = conf - conflicts(p, child);
      if (gain <= 0)
        continue;
      bool better = pick < 0;
      if (!better && per_cost) {
        // gain/cost compared as integers, so the choice is deterministic
        const long long lhs = gain * pick_cost;
        const long long rhs = pick_gain * c.cost;
        better = lhs > rhs || (lhs == rhs && c.cost < pick_cost);
      } else if (!better) {
        better = gain > pick_gain || (gain == pick_gain && c.cost < pick_cost);
      }
      if (better) {
        pick = static_cast<int>(ci);
        pick_gain = gain;
        pick_cost = c.cost;
      }
    }
    if (pick < 0)
      return std::nullopt;
    const Cand &c = p.cands[static_cast<std::size_t>(pick)];
    chosen.push_back(pick);
    for (const auto &o : c.outs)
      sigs.push_back(&o);
    conf -= pick_gain;
  }
  return make_plan(p, chosen);
}

int atom_total(const Problem &p, const std::vector<int> &chosen) {
  int total = 0;
  for (int ci : chosen)
    total += p.cands[static_cast<std::size_t>(ci)].cost;
  return total;
}

// One step of lookahead from every seed: a seed plus one atom that already
// determines the outputs is often the cheapest plan, and the greedy ordering
// can walk past it by preferring a single bigger conflict reduction.  Plan
// evaluations are capped because each one builds a BDD glue network.
std::optional<Plan> pair_search(Problem &p,
                                const std::vector<std::vector<int>> &seeds,
                                std::optional<Plan> best) {
  std::size_t evaluated = 0;
  for (const auto &seed : seeds) {
    std::vector<const std::vector<unsigned char> *> sigs;
    for (int ci : seed)
      for (const auto &o : p.cands[static_cast<std::size_t>(ci)].outs)
        sigs.push_back(&o);
    const int seed_atoms = atom_total(p, seed);
    for (std::size_t ci = 0; ci < p.cands.size(); ++ci) {
      const Cand &c = p.cands[ci];
      if (std::find(seed.begin(), seed.end(), static_cast<int>(ci)) !=
          seed.end())
        continue;
      if (sigs.size() + c.outs.size() > kMaxGlueSignals)
        continue;
      if (best && seed_atoms + c.cost >= best->total)
        continue;
      if (p.budget.check() || ++p.nodes > kMaxSearchNodes)
        return best;
      auto child = sigs;
      for (const auto &o : c.outs)
        child.push_back(&o);
      if (conflicts(p, child) != 0)
        continue;
      if (evaluated++ >= kMaxPlanEvaluations)
        continue;
      std::vector<int> chosen = seed;
      chosen.push_back(static_cast<int>(ci));
      if (auto plan = make_plan(p, chosen);
          plan && (!best || better_plan(*plan, *best)))
        best = std::move(plan);
    }
  }
  return best;
}

// Multi-start greedy: the empty plan, every strong exact cover, and pairs of
// disjoint covers.  A covered output costs no glue at all, so covers are the
// most valuable starting points; the two scoring modes hedge against a locally
// attractive atom that blocks a cheaper plan.
std::optional<Plan> greedy_search(Problem &p) {
  compute_cover_masks(p);
  std::vector<std::size_t> covers;
  for (std::size_t ci = 0; ci < p.cands.size(); ++ci)
    if (p.cover_mask[ci] != 0)
      covers.push_back(ci);
  std::sort(covers.begin(), covers.end(), [&](std::size_t a, std::size_t b) {
    const int pa = std::popcount(p.cover_mask[a]);
    const int pb = std::popcount(p.cover_mask[b]);
    if (pa != pb)
      return pa > pb;
    if (p.cands[a].cost != p.cands[b].cost)
      return p.cands[a].cost < p.cands[b].cost;
    return a < b;
  });
  // Take one cover per mask class first, so the seed list spans different
  // outputs instead of repeating the same function with another wiring.
  std::vector<std::size_t> diverse;
  std::set<unsigned> masks;
  for (std::size_t ci : covers)
    if (masks.insert(p.cover_mask[ci]).second)
      diverse.push_back(ci);
  for (std::size_t ci : covers)
    if (std::find(diverse.begin(), diverse.end(), ci) == diverse.end())
      diverse.push_back(ci);
  covers = std::move(diverse);
  if (covers.size() > kMaxCoverSeeds)
    covers.resize(kMaxCoverSeeds);
  std::vector<std::vector<int>> seeds;
  seeds.push_back({});
  for (std::size_t ci : covers)
    seeds.push_back({static_cast<int>(ci)});
  const std::size_t pairs = std::min(covers.size(), kMaxCoverPairs);
  for (std::size_t i = 0; i < pairs; ++i)
    for (std::size_t j = i + 1; j < pairs; ++j)
      if ((p.cover_mask[covers[i]] & p.cover_mask[covers[j]]) == 0)
        seeds.push_back(
            {static_cast<int>(covers[i]), static_cast<int>(covers[j])});
  std::optional<Plan> best = pair_search(p, seeds, std::nullopt);
  for (const auto &seed : seeds) {
    for (bool per_cost : {true, false}) {
      if (p.budget.check() || p.nodes > kMaxSearchNodes)
        return best;
      if (auto plan = extend_greedy(p, seed, per_cost);
          plan && (!best || better_plan(*plan, *best)))
        best = std::move(plan);
    }
  }
  return best;
}

// Extend a partial state greedily until the signals determine every output.
std::optional<Plan> greedy_complete(Problem &p, State st) {
  while (st.conf > 0) {
    if (p.budget.check() || st.chosen.size() >= kMaxGreedySteps)
      return std::nullopt;
    int pick = -1;
    long long pick_conf = st.conf;
    int pick_cost = kUnreachable;
    for (std::size_t ci = 0; ci < p.cands.size(); ++ci) {
      if (std::find(st.chosen.begin(), st.chosen.end(), static_cast<int>(ci)) !=
          st.chosen.end())
        continue;
      const Cand &c = p.cands[ci];
      if (st.sigs.size() + c.outs.size() > kMaxGlueSignals)
        continue;
      if (++p.nodes > kMaxSearchNodes)
        return std::nullopt;
      auto sigs = st.sigs;
      for (const auto &o : c.outs)
        sigs.push_back(&o);
      const long long conf = conflicts(p, sigs);
      if (conf < pick_conf || (conf == pick_conf && c.cost < pick_cost)) {
        pick = static_cast<int>(ci);
        pick_conf = conf;
        pick_cost = c.cost;
      }
    }
    if (pick < 0)
      return std::nullopt;
    const Cand &c = p.cands[static_cast<std::size_t>(pick)];
    st.chosen.push_back(pick);
    st.atoms += c.cost;
    for (const auto &o : c.outs)
      st.sigs.push_back(&o);
    st.conf = pick_conf;
  }
  return make_plan(p, st.chosen);
}

// Fallback when the greedy starts all fail: breadth-first layers of additions
// keep the cheapest states per conflict count, so a plan that needs a locally
// unattractive atom is still reachable.
std::optional<Plan> beam_search(Problem &p) {
  if (p.cands.empty())
    return std::nullopt;
  State start;
  start.conf = conflicts(p, {});
  std::vector<State> beam{start};
  std::optional<Plan> best;
  State best_partial = start;
  const int width = std::clamp(
      static_cast<int>(std::sqrt(static_cast<double>(p.cands.size()))), 4, 16);
  for (std::size_t depth = 0; depth < p.cands.size() && !beam.empty();
       ++depth) {
    if (p.budget.check())
      break;
    std::vector<State> next;
    std::set<std::string> seen;
    for (const State &st : beam) {
      for (std::size_t ci = 0; ci < p.cands.size(); ++ci) {
        const Cand &c = p.cands[ci];
        if (std::find(st.chosen.begin(), st.chosen.end(),
                      static_cast<int>(ci)) != st.chosen.end())
          continue;
        if (best && st.atoms + c.cost >= best->total)
          continue;
        if (st.sigs.size() + c.outs.size() > kMaxGlueSignals)
          continue;
        if (++p.nodes > kMaxSearchNodes)
          return best;
        State ns = st;
        ns.chosen.push_back(static_cast<int>(ci));
        ns.atoms += c.cost;
        for (const auto &o : c.outs)
          ns.sigs.push_back(&o);
        ns.conf = conflicts(p, ns.sigs);
        if (ns.conf == 0) {
          if (auto plan = make_plan(p, ns.chosen);
              plan && (!best || better_plan(*plan, *best)))
            best = std::move(plan);
          continue; // complete states only get cheaper by removing atoms
        }
        if (ns.conf < best_partial.conf ||
            (ns.conf == best_partial.conf && ns.atoms < best_partial.atoms))
          best_partial = ns;
        std::string key;
        for (int x : ns.chosen)
          key += std::to_string(x) + ",";
        if (!seen.insert(std::move(key)).second)
          continue;
        next.push_back(std::move(ns));
      }
    }
    std::sort(next.begin(), next.end(), [](const State &a, const State &b) {
      if (a.conf != b.conf)
        return a.conf < b.conf;
      if (a.atoms != b.atoms)
        return a.atoms < b.atoms;
      return a.chosen < b.chosen;
    });
    if (static_cast<int>(next.size()) > width)
      next.resize(static_cast<std::size_t>(width));
    beam = std::move(next);
  }
  if (!best)
    best = greedy_complete(p, best_partial);
  return best;
}

// Drop instances the signals do not need: removing one is a win whenever the
// glue grows by less than the atom it removes.
void polish(Problem &p, Plan &plan) {
  bool changed = true;
  while (changed && !p.budget.check()) {
    changed = false;
    for (std::size_t i = 0; i < plan.chosen.size(); ++i) {
      std::vector<int> trial = plan.chosen;
      trial.erase(trial.begin() + static_cast<std::ptrdiff_t>(i));
      auto smaller = make_plan(p, trial);
      if (smaller && smaller->total < plan.total) {
        plan = std::move(*smaller);
        changed = true;
        break;
      }
    }
  }
}

} // namespace

std::optional<ComposeResult>
compose_from_library(const MultiProblem &spec,
                     const std::vector<MultiProblem> &library,
                     long long time_budget_ms, bool *timed_out) {
  const std::size_t n = spec.vars.size();
  if (library.empty() || spec.outputs.empty())
    return std::nullopt;
  Problem p(spec, library);
  p.budget.start(time_budget_ms);
  generate_candidates(p);
  std::optional<Plan> plan = greedy_search(p);
  if (!plan)
    plan = beam_search(p);
  if (!plan) {
    *timed_out = p.budget.expired;
    return std::nullopt;
  }
  polish(p, *plan);
  *timed_out = p.budget.expired;

  std::vector<const std::vector<unsigned char> *> sigs;
  for (int ci : plan->chosen)
    for (const auto &o : p.cands[static_cast<std::size_t>(ci)].outs)
      sigs.push_back(&o);
  const std::size_t ns = sigs.size();
  const std::string prefix = signal_prefix(spec, ns);
  const auto glue_res = best_glue(p, sigs);
  if (!glue_res.first)
    return std::nullopt;
  // RTL only: the glue is inlined into the top below and proved there.
  const ComposeResult glue =
      compose_mux2(*glue_res.first, {}, {.synthesize = false});
  if (!glue.ok || glue.rtl.empty())
    return std::nullopt;

  // Emit the final top: atom instances + the glue module.
  std::ostringstream os;
  os << "module top(";
  for (std::size_t i = 0; i < n; ++i)
    os << (i ? ", " : "") << "input logic " << spec.vars[i];
  for (std::size_t i = 0; i < spec.names.size(); ++i)
    os << ", output logic " << spec.names[i];
  os << ");\n";
  for (std::size_t j = 0; j < ns; ++j)
    os << "  wire " << prefix << j << ";\n";
  std::ostringstream atoms_rtl;
  std::set<std::string> emitted;
  std::size_t out_idx = 0;
  std::vector<AtomInstance> plan_atoms;
  for (std::size_t i = 0; i < plan->chosen.size(); ++i) {
    const Cand &c = p.cands[static_cast<std::size_t>(plan->chosen[i])];
    const MultiProblem &atom = library[static_cast<std::size_t>(c.atom)];
    os << "  " << atom.title << " a" << i << "(";
    AtomInstance inst;
    inst.module = atom.title;
    for (std::size_t j = 0; j < c.vars.size(); ++j) {
      os << (j ? ", " : "") << spec.vars[c.vars[j]];
      inst.inputs.push_back(spec.vars[c.vars[j]]);
    }
    for (std::size_t o = 0; o < c.outs.size(); ++o) {
      os << ", " << prefix << (out_idx++);
      inst.outputs.push_back(atom.names[o]);
    }
    os << ");\n";
    plan_atoms.push_back(std::move(inst));
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
  if (emitted.insert("mux2").second)
    atoms_rtl << "module mux2(input logic A, B, S, output logic Y);\n"
                 "  assign Y = (S & B) | (~S & A);\nendmodule\n";

  ComposeResult out;
  out.rtl = os.str();
  out.atom_rtl = atoms_rtl.str();
  out.mode = "library";
  out.nodes = static_cast<int>(ns);
  out.plan = std::move(plan_atoms);
  out.time_budget_ms = time_budget_ms;
  out.timed_out = p.budget.expired;
  const std::string dir = temp_dir();
  const fs::path top_path = fs::path(dir) / "top.v";
  const fs::path atom_path = fs::path(dir) / "atoms.v";
  {
    std::ofstream(top_path) << out.rtl;
    std::ofstream(atom_path) << out.atom_rtl;
  }
  try {
    // Prove the netlist that is actually written out.  The table-level search
    // above checked the atom and glue functions, but not the emitted text: a
    // net-name collision used to survive it.  Above the SOP reference size the
    // miter is too deep, so that range keeps the table-level check only.
    const bool prove = spec.vars.size() <= kMaxGlueSignals;
    out.synth = synthesize_rtl_atoms(top_path.string(), atom_path.string(),
                                     spec, "", 0, !prove);
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
                           const std::vector<MultiProblem> &library,
                           ComposeOptions options) {
  ComposeResult out;
  out.time_budget_ms = options.time_budget_ms;
  if (spec.vars.empty() || spec.outputs.empty()) {
    out.reason = "compose needs a non-empty spec";
    return out;
  }
  if (spec.has_dont_cares()) {
    out.reason = "compose does not support don't-care masks yet";
    return out;
  }

  // Library search: the beam picks measured atoms and pays only for the glue
  // its signals leave undetermined.  timed_out survives the fallback to the
  // pure mux2 network so callers can tell an exhaustive search from a cut one.
  bool timed_out = false;
  if (auto lib = compose_from_library(spec, library, options.time_budget_ms,
                                      &timed_out);
      lib)
    return *lib;
  out.timed_out = timed_out;

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
    out.mode = "atom";
    out.nodes = 1;
    out.plan.push_back({atom.title, spec.vars, atom.names});
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
    roots.push_back(bdd.build(table, 0));
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
  // Node wires must not collide with a port: a spec input named n4 plus a
  // mux output n4 puts two drivers on one net, and the composed netlist still
  // verified against the truth table (which only looks at the RTL expression).
  std::set<std::string> used_names(spec.vars.begin(), spec.vars.end());
  used_names.insert(spec.names.begin(), spec.names.end());
  std::string node_prefix = "n";
  auto node_collides = [&](const std::string &prefix) {
    for (int id : mux_nodes)
      if (used_names.count(prefix + std::to_string(id)))
        return true;
    return false;
  };
  while (node_collides(node_prefix))
    node_prefix += "x";
  auto node_net = [&](int id) { return node_prefix + std::to_string(id); };

  for (int id : mux_nodes)
    os << "  wire " << node_net(id) << ";\n";

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
    return node_net(id);
  };
  for (int id : mux_nodes) {
    const Node &nd = bdd.nodes[id];
    os << "  mux2 u" << id << "(.A(" << expr(nd.lo) << "), .B(" << expr(nd.hi)
       << "), .S(" << spec.vars[nd.var] << "), .Y(" << node_net(id) << "));\n";
  }
  for (std::size_t k = 0; k < roots.size(); ++k)
    os << "  assign " << spec.names[k] << " = " << expr(roots[k]) << ";\n";
  os << "endmodule\n";

  out.rtl = os.str();
  out.mode = "bdd-mux2";
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
  if (!options.synthesize) {
    // RTL only: the emitted network is checked against its own tables here,
    // and the caller proves the composed top it inlines this into.
    out.ok = true;
    out.verified = self_check;
    return out;
  }
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
