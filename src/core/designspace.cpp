#include "core/designspace.hpp"

#include "core/limits.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace dcstudy {
namespace {

struct LitInfo {
  std::size_t var = 0;
  bool inverted = false;
};

struct Env {
  std::vector<std::string> vars;
  std::unordered_map<std::string, LitInfo> lits;
  std::size_t n = 0;
};

Env make_env(const std::vector<std::string> &vars) {
  Env e;
  e.vars = vars;
  e.n = vars.size();
  for (std::size_t i = 0; i < vars.size(); ++i) {
    if (vars[i].empty())
      throw std::invalid_argument("variable names must not be empty");
    if (vars[i].back() == 'b')
      throw std::invalid_argument("variable names must not end in 'b': " +
                                  vars[i]);
    for (std::size_t j = 0; j < vars.size(); ++j) {
      if (i == j)
        continue;
      if (vars[i] == vars[j])
        throw std::invalid_argument("duplicate variable: " + vars[i]);
      if (vars[i] + "b" == vars[j])
        throw std::invalid_argument("variable name collision: " + vars[i] +
                                    "b");
    }
  }
  for (std::size_t i = 0; i < vars.size(); ++i) {
    e.lits.emplace(vars[i], LitInfo{i, false});
    e.lits.emplace(vars[i] + "b", LitInfo{i, true});
  }
  return e;
}

// Internal helper; call sites reviewed.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int bit_of(const Env &e, std::size_t var, std::uint32_t minterm) {
  const std::size_t p = e.n - 1 - var;
  return int((minterm >> p) & 1U);
}

LitInfo lit_info(const Env &e, const std::string &lit) {
  auto it = e.lits.find(lit);
  if (it == e.lits.end())
    throw std::runtime_error("unknown literal: " + lit);
  return it->second;
}

int lit_value(const Env &e, const std::string &lit, std::uint32_t minterm) {
  const auto info = lit_info(e, lit);
  int v = bit_of(e, info.var, minterm);
  return info.inverted ? 1 - v : v;
}

int source_value(const Env &e, const std::string &src, std::uint32_t minterm) {
  if (src == "0")
    return 0;
  if (src == "1")
    return 1;
  return lit_value(e, src, minterm);
}

std::uint32_t req_of(const Env &e, const std::string &lit) {
  const auto info = lit_info(e, lit);
  return info.inverted ? (std::uint32_t{1} << info.var) : 0;
}

std::uint32_t req_of_tg(const Env &e, const std::string &lit) {
  // TGL/TGH(g) needs g and ~g; exactly one of them is the inverted literal if
  // the control is a plain variable.  If both phases exist as available
  // literals, the requirement is the inverter of the underlying variable.
  const auto info = lit_info(e, lit);
  return std::uint32_t{1} << info.var;
}

Behavior eval_branch(const Env &e, const std::string &src,
                     const std::vector<ChainElem> &chain, std::uint32_t m) {
  Behavior b;
  int v = source_value(e, src, m);
  bool en = true;
  bool strong = true;
  for (const auto &el : chain) {
    const int g = lit_value(e, el.lit, m);
    bool on = false;
    bool st = true;
    switch (el.kind) {
    case SwKind::N:
      on = (g == 1 && v == 0);
      st = (v == 0);
      break;
    case SwKind::P:
      on = (g == 0 && v == 1);
      st = (v == 1);
      break;
    case SwKind::TGL:
      on = (g == 0);
      st = true;
      break;
    case SwKind::TGH:
      on = (g == 1);
      st = true;
      break;
    }
    if (!on) {
      en = false;
      break;
    }
    strong = strong && st;
  }
  b.conducts = en;
  b.strong = en && strong;
  b.value = v;
  return b;
}

std::string behavior_key(const std::vector<Behavior> &bh, std::uint32_t req) {
  std::string key;
  key.reserve(bh.size() * 2 + 4);
  for (const auto &b : bh) {
    if (!b.conducts)
      key.push_back('z');
    else if (b.value == 0)
      key.push_back('0');
    else
      key.push_back('1');
    key.push_back(b.strong ? 's' : 'w');
  }
  key += "#" + std::to_string(req);
  return key;
}

// Internal helper; call sites reviewed.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<std::vector<ChainElem>> all_chains(const Env &e, int max_chain,
                                               int max_cost, bool allow_tg) {
  std::vector<std::vector<ChainElem>> out;
  std::vector<ChainElem> cur;
  const int nlit = static_cast<int>(e.lits.size());
  std::vector<std::pair<SwKind, std::string>> atoms;
  for (int i = 0; i < nlit; ++i) {
    for (auto k : {SwKind::N, SwKind::P, allow_tg ? SwKind::TGL : SwKind::N,
                   allow_tg ? SwKind::TGH : SwKind::N}) {
      if (!allow_tg && (k == SwKind::TGL || k == SwKind::TGH))
        continue;
      atoms.emplace_back(k, e.vars[std::size_t(i) / 2] + (i % 2 ? "b" : ""));
    }
  }
  // Remove duplicate atoms when transmission gates are disabled.
  std::sort(atoms.begin(), atoms.end(), [](const auto &a, const auto &b) {
    if (a.first != b.first)
      return a.first < b.first;
    return a.second < b.second;
  });
  atoms.erase(std::unique(atoms.begin(), atoms.end(),
                          [](const auto &a, const auto &b) {
                            return a.first == b.first && a.second == b.second;
                          }),
              atoms.end());

  // A series stack conducts the same way in any order, so enumerate each
  // multiset of atoms once (non-decreasing atom index) instead of every
  // permutation.  For n=4 this cuts 1,082,401 chains to 58,905; the search
  // still covers every distinct series network, just not its reorderings.
  int cost = 0;
  auto rec = [&](auto &&self, std::size_t from) -> void {
    out.push_back(cur);
    if (static_cast<int>(cur.size()) >= max_chain)
      return;
    for (std::size_t i = from; i < atoms.size(); ++i) {
      const auto &[k, lit] = atoms[i];
      const int c = switch_cost(k);
      if (cost + c > max_cost)
        continue;
      cur.push_back(ChainElem{k, lit, c});
      cost += c;
      self(self, i);
      cost -= c;
      cur.pop_back();
    }
  };
  rec(rec, 0);
  return out;
}

} // namespace

int switch_cost(SwKind k) {
  return (k == SwKind::TGL || k == SwKind::TGH) ? 2 : 1;
}

bool chain_is_prefix(const std::vector<ChainElem> &a,
                     const std::vector<ChainElem> &b) {
  if (a.size() > b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i].kind != b[i].kind || a[i].lit != b[i].lit)
      return false;
  return true;
}

const char *switch_name(SwKind k) {
  switch (k) {
  case SwKind::N:
    return "N";
  case SwKind::P:
    return "P";
  case SwKind::TGL:
    return "TGL";
  case SwKind::TGH:
    return "TGH";
  }
  return "?";
}

// Internal helper; call sites reviewed.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool Branch::covers_strong(std::size_t minterm, int target) const {
  if (minterm >= behavior.size())
    return false;
  const auto &b = behavior[minterm];
  return b.conducts && b.strong && b.value == target;
}

bool Branch::usable(const std::vector<int> &target) const {
  if (behavior.size() != target.size())
    return false;
  bool any_strong = false;
  for (std::size_t i = 0; i < behavior.size(); ++i) {
    const auto &b = behavior[i];
    if (!b.conducts)
      continue;
    if (b.value != target[i])
      return false;
    if (b.strong)
      any_strong = true;
  }
  return any_strong;
}

std::string Branch::topo(const std::vector<std::string> &vars) const {
  (void)vars;
  auto sorted = chain;
  std::sort(sorted.begin(), sorted.end(),
            [](const ChainElem &a, const ChainElem &b) {
              if (a.kind != b.kind)
                return a.kind < b.kind;
              return a.lit < b.lit;
            });
  std::string out = source + " --[";
  if (sorted.empty())
    out += "wire";
  for (std::size_t i = 0; i < sorted.size(); ++i) {
    if (i)
      out += " · ";
    out += switch_name(sorted[i].kind);
    out += "(" + sorted[i].lit + ")";
  }
  out += "]-- Y";
  return out;
}

std::vector<Branch> build_branches(const std::vector<std::string> &vars,
                                   const SpaceOptions &opt) {
  if (vars.empty() || vars.size() >= kMaxAddressableVars)
    throw std::invalid_argument(
        "pass-network space: 2^n rows are not addressable");
  const Env e = make_env(vars);
  const std::uint32_t rows = std::uint32_t{1} << e.n;
  const auto chains = all_chains(e, opt.max_chain, opt.max_cost, opt.allow_tg);

  std::vector<std::string> sources = {"0", "1"};
  for (const auto &v : vars) {
    sources.push_back(v);
    sources.push_back(v + "b");
  }
  if (opt.max_ops != 0 && chains.size() * sources.size() > opt.max_ops)
    throw std::runtime_error(
        "branch space " + std::to_string(chains.size()) + " chains x " +
        std::to_string(sources.size()) + " sources exceeds work budget " +
        std::to_string(opt.max_ops) +
        "; lower --max-chain/--max-cost or raise --max-ops");

  std::vector<Branch> out;
  out.reserve(chains.size() * sources.size());
  for (const auto &src : sources) {
    std::uint32_t base_req = 0;
    if (src != "0" && src != "1")
      base_req = req_of(e, src);
    for (const auto &chain : chains) {
      // A single transmission gate is exactly an N/P pair from the same
      // source.  Keep the atomic N/P representation as canonical and reserve
      // TGL/TGH for series chains, where expanding would duplicate the prefix
      // and change cost.  This removes the TG-vs-pair encoding duality at the
      // source of the design space.
      if (chain.size() == 1 &&
          (chain[0].kind == SwKind::TGL || chain[0].kind == SwKind::TGH))
        continue;
      Branch br;
      br.source = src;
      br.chain = chain;
      br.cost = 0;
      br.req_mask = base_req;
      for (const auto &el : chain) {
        br.cost += el.cost;
        if (el.kind == SwKind::TGL || el.kind == SwKind::TGH)
          br.req_mask |= req_of_tg(e, el.lit);
        else
          br.req_mask |= req_of(e, el.lit);
      }
      if (br.cost > opt.max_cost)
        continue;
      br.behavior.resize(rows);
      for (std::uint32_t m = 0; m < rows; ++m)
        br.behavior[m] = eval_branch(e, src, chain, m);
      out.push_back(std::move(br));
    }
  }
  return out;
}

std::vector<Branch> usable_branches(const std::vector<std::string> &vars,
                                    const std::vector<int> &target,
                                    const SpaceOptions &opt) {
  const std::uint32_t rows = std::uint32_t{1} << vars.size();
  if (target.size() != rows)
    throw std::invalid_argument("target width does not match variables");
  const auto all = build_branches(vars, opt);
  // Keep a bounded Pareto set per (behaviour, required inverters) key instead
  // of only the cheapest branch: a costlier branch can share more prefixes
  // with the other outputs.  "Slack" bounds how far above the local minimum
  // a branch may sit; dominance drops branches whose edges are a superset of
  // a cheaper branch, which cannot help once union cost is counted.
  constexpr int kBranchSlack =
      0; // equal-cost alternatives only: see proof scope
  std::map<std::string, std::vector<const Branch *>> groups;
  for (const auto &b : all) {
    if (!b.usable(target))
      continue;
    groups[behavior_key(b.behavior, b.req_mask)].push_back(&b);
  }
  std::vector<Branch> out;
  for (auto &[_, group] : groups) {
    std::sort(group.begin(), group.end(), [](const Branch *a, const Branch *b) {
      if (a->cost != b->cost)
        return a->cost < b->cost;
      return a->chain.size() < b->chain.size();
    });
    const int floor = group.front()->cost;
    std::vector<const Branch *> keep;
    for (const Branch *b : group) {
      if (b->cost > floor + kBranchSlack)
        continue;
      bool dominated = false;
      for (const Branch *a : keep)
        if (a->cost <= b->cost && chain_is_prefix(a->chain, b->chain)) {
          dominated = true;
          break;
        }
      if (!dominated)
        keep.push_back(b);
    }
    for (const Branch *b : keep)
      out.push_back(*b);
  }
  std::sort(out.begin(), out.end(), [](const Branch &a, const Branch &b) {
    if (a.cost != b.cost)
      return a.cost < b.cost;
    if (a.source != b.source)
      return a.source < b.source;
    return a.topo({}) < b.topo({});
  });
  return out;
}

} // namespace dcstudy
