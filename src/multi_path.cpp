#include "dcstudy/multi_path.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "dcstudy/driver_profile.hpp"
#include "dcstudy/parallel.hpp"

namespace dcstudy {
namespace {

struct EdgeInfo {
  int cost = 0;
  SwKind kind = SwKind::TGH;
  std::string lit;
};

std::string edge_key(const std::string& source, const std::vector<ChainElem>& chain,
                     std::size_t up_to) {
  std::string key = source + "#";
  for (std::size_t i = 0; i <= up_to; ++i) {
    key += switch_name(chain[i].kind);
    key += "(";
    key += chain[i].lit;
    key += ");";
  }
  return key;
}

struct AllPath {
  Branch branch;
  int output = 0;
};

struct PathInfo {
  int path = 0;
  int output = 0;
  std::vector<int> edges;
};

struct Search {
  const std::vector<AllPath>& all;
  const std::vector<PathInfo>& paths;
  const std::vector<EdgeInfo>& edge_info;
  const std::vector<std::vector<int>>& targets;
  int nout = 0;
  int rows = 0;
  int max_cost = 0;
  int best = std::numeric_limits<int>::max();
  std::vector<int> best_selected;
  std::vector<int> selected;
  std::vector<bool> chosen;
  std::vector<int> edge_count;
  int edge_cost = 0;
  std::uint32_t inv_mask = 0;
  std::vector<std::string> driver;
  std::vector<bool> strong_cover;

  Search(const std::vector<AllPath>& a, const std::vector<PathInfo>& p,
         const std::vector<EdgeInfo>& ei,
         const std::vector<std::vector<int>>& t, int budget)
      : all(a), paths(p), edge_info(ei), targets(t),
        nout(static_cast<int>(t.size())), rows(static_cast<int>(t[0].size())),
        max_cost(budget),
        chosen(p.size(), false), edge_count(ei.size(), 0),
        driver(static_cast<std::size_t>(nout * rows)),
        strong_cover(static_cast<std::size_t>(nout * rows), false) {}

  int idx(int o, int m) const { return o * rows + m; }

  // A fast feasible cover to seed branch-and-bound.  Without an initial
  // upper bound the decoder-like PDN covers spend a long time before finding
  // any complete solution, so every prune compares against infinity.
  void seed_greedy() {
    std::vector<char> covered(static_cast<std::size_t>(nout * rows), 0);
    std::vector<int> local_count(edge_info.size(), 0);
    std::vector<int> local_selected;
    int local_edges = 0;
    std::uint32_t local_mask = 0;
    const auto target_row = [&](int o, int m) {
      return targets[static_cast<std::size_t>(o)][static_cast<std::size_t>(m)];
    };
    for (;;) {
      bool any = false;
      for (int o = 0; o < nout; ++o)
        for (int m = 0; m < rows; ++m)
          if (target_row(o, m) >= 0 &&
              !covered[static_cast<std::size_t>(idx(o, m))])
            any = true;
      if (!any) break;
      int best_pi = -1;
      int best_gain = 0;
      long long best_delta = (1LL << 60);
      for (std::size_t pi = 0; pi < paths.size(); ++pi) {
        const auto& p = paths[pi];
        int gain = 0;
        const auto& b = all[static_cast<std::size_t>(p.path)].branch;
        for (int m = 0; m < rows; ++m) {
          const int t = target_row(p.output, m);
          if (t < 0 || covered[static_cast<std::size_t>(idx(p.output, m))])
            continue;
          const auto& bh = b.behavior[static_cast<std::size_t>(m)];
          if (bh.conducts && bh.strong) ++gain;
        }
        if (gain == 0) continue;
        int delta = 0;
        for (int e : p.edges)
          if (local_count[static_cast<std::size_t>(e)] == 0)
            delta += edge_info[static_cast<std::size_t>(e)].cost;
        const std::uint32_t nm = local_mask | b.req_mask;
        delta += 2 * static_cast<int>(__builtin_popcountll(nm) -
                                      __builtin_popcountll(local_mask));
        // Prefer more newly covered rows per transistor.  Ties go to the
        // smaller incremental cost, then to the earlier path.
        const bool better =
            best_pi < 0 ||
            static_cast<long long>(gain) * (best_delta + 1) >
                static_cast<long long>(best_gain) * (delta + 1) ||
            (static_cast<long long>(gain) * (best_delta + 1) ==
                 static_cast<long long>(best_gain) * (delta + 1) &&
             delta < best_delta);
        if (better) {
          best_pi = static_cast<int>(pi);
          best_gain = gain;
          best_delta = delta;
        }
      }
      if (best_pi < 0) return;
      const auto& p = paths[static_cast<std::size_t>(best_pi)];
      const auto& b = all[static_cast<std::size_t>(p.path)].branch;
      for (int e : p.edges)
        if (local_count[static_cast<std::size_t>(e)]++ == 0)
          local_edges += edge_info[static_cast<std::size_t>(e)].cost;
      local_mask |= b.req_mask;
      local_selected.push_back(best_pi);
      for (int m = 0; m < rows; ++m) {
        if (target_row(p.output, m) < 0) continue;
        const auto& bh = b.behavior[static_cast<std::size_t>(m)];
        if (bh.conducts && bh.strong)
          covered[static_cast<std::size_t>(idx(p.output, m))] = 1;
      }
    }
    const int total =
        local_edges + 2 * static_cast<int>(__builtin_popcountll(local_mask));
    if (total <= max_cost && total < best) {
      best = total;
      best_selected = std::move(local_selected);
    }
  }

  void add_path(int pi, std::vector<std::pair<int, std::string>>& old_driver,
                std::vector<std::pair<int, bool>>& old_strong, int& old_edges,
                std::uint32_t& old_mask) {
    const auto& p = paths[static_cast<std::size_t>(pi)];
    const auto& b = all[static_cast<std::size_t>(p.path)].branch;
    old_edges = edge_cost;
    old_mask = inv_mask;
    for (int e : p.edges) {
      if (edge_count[static_cast<std::size_t>(e)]++ == 0)
        edge_cost += edge_info[static_cast<std::size_t>(e)].cost;
    }
    inv_mask |= b.req_mask;
    for (int m = 0; m < rows; ++m) {
      if (!b.behavior[static_cast<std::size_t>(m)].conducts) continue;
      const int k = idx(p.output, m);
      old_driver.emplace_back(k, driver[static_cast<std::size_t>(k)]);
      old_strong.emplace_back(k, strong_cover[static_cast<std::size_t>(k)]);
      driver[static_cast<std::size_t>(k)] = b.source;
      if (b.behavior[static_cast<std::size_t>(m)].strong)
        strong_cover[static_cast<std::size_t>(k)] = true;
    }
  }

  void remove_path(int pi, const std::vector<std::pair<int, std::string>>& old_driver,
                   const std::vector<std::pair<int, bool>>& old_strong,
                   int old_edges, std::uint32_t old_mask) {
    const auto& p = paths[static_cast<std::size_t>(pi)];
    for (auto it = old_driver.rbegin(); it != old_driver.rend(); ++it)
      driver[static_cast<std::size_t>(it->first)] = it->second;
    for (auto it = old_strong.rbegin(); it != old_strong.rend(); ++it)
      strong_cover[static_cast<std::size_t>(it->first)] = it->second;
    for (int e : p.edges) {
      if (--edge_count[static_cast<std::size_t>(e)] == 0)
        edge_cost -= edge_info[static_cast<std::size_t>(e)].cost;
    }
    edge_cost = old_edges;
    inv_mask = old_mask;
  }

  bool compatible(const PathInfo& p) const {
    const auto& b = all[static_cast<std::size_t>(p.path)].branch;
    const auto& target = targets.at(static_cast<std::size_t>(p.output));
    for (int m = 0; m < rows; ++m) {
      if (!b.behavior[static_cast<std::size_t>(m)].conducts) continue;
      if (target[static_cast<std::size_t>(m)] < 0) return false;
      const auto& d = driver[static_cast<std::size_t>(idx(p.output, m))];
      if (!d.empty() && d != b.source) return false;
    }
    return true;
  }

  void search() {
    for (int o = 0; o < nout; ++o)
      for (int m = 0; m < rows; ++m)
        if (targets[static_cast<std::size_t>(o)][static_cast<std::size_t>(m)] < 0)
          strong_cover[static_cast<std::size_t>(idx(o, m))] = true;
    const int sofar =
        edge_cost + 2 * static_cast<int>(__builtin_popcountll(inv_mask));
    if (sofar > max_cost || sofar >= best) return;
    int bo = -1, bm = -1;
    int best_choices = std::numeric_limits<int>::max();
    for (int o = 0; o < nout; ++o)
      for (int m = 0; m < rows; ++m) {
        if (strong_cover[static_cast<std::size_t>(idx(o, m))]) continue;
        int count = 0;
        for (std::size_t pi = 0; pi < paths.size(); ++pi) {
          if (chosen[pi] || paths[pi].output != o) continue;
          const auto& b = all[static_cast<std::size_t>(paths[pi].path)].branch;
          const auto& bh = b.behavior[static_cast<std::size_t>(m)];
          if (bh.conducts && bh.strong && compatible(paths[pi])) ++count;
        }
        if (count < best_choices) {
          best_choices = count;
          bo = o;
          bm = m;
        }
      }
    if (bo < 0) {
      const int total = edge_cost + 2 * static_cast<int>(__builtin_popcountll(inv_mask));
      if (total < best) {
        best = total;
        best_selected = selected;
      }
      return;
    }
    if (best_choices == 0) return;
    for (std::size_t pi = 0; pi < paths.size(); ++pi) {
      if (chosen[pi] || paths[pi].output != bo) continue;
      const auto& b = all[static_cast<std::size_t>(paths[pi].path)].branch;
      const auto& bh = b.behavior[static_cast<std::size_t>(bm)];
      if (!(bh.conducts && bh.strong)) continue;
      if (!compatible(paths[pi])) continue;
      std::vector<std::pair<int, std::string>> old_driver;
      std::vector<std::pair<int, bool>> old_strong;
      int old_edges = 0;
      std::uint32_t old_mask = 0;
      add_path(static_cast<int>(pi), old_driver, old_strong, old_edges, old_mask);
      chosen[pi] = true;
      selected.push_back(static_cast<int>(pi));
      search();
      selected.pop_back();
      chosen[pi] = false;
      remove_path(static_cast<int>(pi), old_driver, old_strong, old_edges, old_mask);
    }
  }
};

std::string branch_key(const Branch& b) {
  std::string key;
  key.reserve(b.behavior.size() * 3 + 16);
  for (const auto& bh : b.behavior) {
    key += bh.conducts ? (bh.strong ? 'S' : 'c') : '.';
    key += static_cast<char>('0' + bh.value);
  }
  key += '#';
  key += std::to_string(b.req_mask);
  return key;
}

struct RailSolution {
  bool ok = false;
  std::vector<AllPath> all;
  std::vector<PathInfo> paths;
  std::vector<EdgeInfo> edges;
  std::vector<std::string> keys;
  std::vector<int> selected;
  int edge_cost = 0;
  std::uint32_t used_mask = 0;
};

MultiPathSynthesisResult synthesize_dual_rail_multi(
    const std::vector<BoolFunc>& funcs, const std::vector<std::string>& names,
    const std::vector<std::string>& vars, const SpaceOptions& opt) {
  MultiPathSynthesisResult out;
  out.outputs = names;
  if (funcs.empty() || funcs.size() != names.size() || vars.empty() ||
      vars.size() > 4) {
    out.reason = "dual-rail synthesis supports 1..4 inputs and matching outputs";
    return out;
  }

  const std::uint32_t rows = std::uint32_t{1} << vars.size();
  std::vector<std::vector<int>> pun_targets;
  std::vector<std::vector<int>> pdn_targets;
  for (const auto& f : funcs) {
    std::vector<int> tp, tn;
    for (std::uint32_t m = 0; m < rows; ++m) {
      const bool one = f.value(m);
      tp.push_back(one ? 1 : -1);
      tn.push_back(one ? -1 : 0);
    }
    pun_targets.push_back(std::move(tp));
    pdn_targets.push_back(std::move(tn));
  }

  // Precompute the universal chain universe once.  Every mask/rail/output is
  // a filtered view; we never rebuild the chains inside the mask loop.
  const std::vector<Branch> universe = build_branches(vars, opt);
  const std::uint64_t masks = std::uint64_t{1} << vars.size();

  struct Best {
    bool ok = false;
    int total = std::numeric_limits<int>::max();
    std::uint32_t used_mask = 0;
    RailSolution pun;
    RailSolution pdn;
  } best;

  auto solve_rail = [&](const std::string& source,
                        const std::vector<std::vector<int>>& rail_targets,
                        std::uint64_t inverter_mask) -> RailSolution {
    RailSolution r;
    std::map<std::string, int> edge_ids;
    for (std::size_t o = 0; o < funcs.size(); ++o) {
      // Keep one cheapest representation for each (behavior, required
      // inverters) pair.  This mirrors usable_branches() but is done against
      // a fixed inverter set and one rail target.
      std::map<std::string, const Branch*> uniq;
      const auto& target = rail_targets[o];
      for (const auto& b : universe) {
        if (b.source != source) continue;
        if ((b.req_mask & ~inverter_mask) != 0) continue;
        if (!b.usable(target)) continue;
        const std::string key = branch_key(b);
        auto it = uniq.find(key);
        if (it == uniq.end() || b.cost < it->second->cost) uniq[key] = &b;
      }
      for (const auto& [_, b] : uniq) {
        AllPath ap{*b, static_cast<int>(o)};
        r.all.push_back(ap);
        PathInfo p;
        p.path = static_cast<int>(r.all.size() - 1);
        p.output = static_cast<int>(o);
        for (std::size_t j = 0; j < b->chain.size(); ++j) {
          const std::string key = edge_key(b->source, b->chain, j);
          auto it = edge_ids.find(key);
          int id;
          if (it == edge_ids.end()) {
            id = static_cast<int>(r.edges.size());
            edge_ids.emplace(key, id);
            r.edges.push_back(EdgeInfo{switch_cost(b->chain[j].kind),
                                       b->chain[j].kind, b->chain[j].lit});
            r.keys.push_back(key);
          } else {
            id = it->second;
          }
          p.edges.push_back(id);
        }
        r.paths.push_back(std::move(p));
      }
    }
    if (r.paths.empty()) return r;
    if (r.paths.size() > 2048) return r;

    Search search(r.all, r.paths, r.edges, rail_targets, opt.max_cost);
    search.seed_greedy();
    search.search();
    if (search.best == std::numeric_limits<int>::max() ||
        search.best_selected.empty())
      return r;

    r.selected = search.best_selected;
    std::set<int> chosen;
    for (int pi : r.selected) {
      if (pi < 0 || pi >= static_cast<int>(r.paths.size())) return RailSolution{};
      for (int e : r.paths[static_cast<std::size_t>(pi)].edges)
        chosen.insert(e);
      r.used_mask |= r.all[static_cast<std::size_t>(r.paths[static_cast<std::size_t>(pi)].path)]
                         .branch.req_mask;
    }
    for (int e : chosen) r.edge_cost += r.edges[static_cast<std::size_t>(e)].cost;
    r.ok = true;
    return r;
  };

  std::vector<Best> candidates(static_cast<std::size_t>(masks));
  parallel_for(static_cast<std::size_t>(masks), parallel_jobs(),
               [&](std::size_t mi) {
                 const std::uint64_t mask = mi;
                 RailSolution pun = solve_rail("1", pun_targets, mask);
                 if (!pun.ok) return;
                 RailSolution pdn = solve_rail("0", pdn_targets, mask);
                 if (!pdn.ok) return;
                 const std::uint32_t used = pun.used_mask | pdn.used_mask;
                 const int total = pun.edge_cost + pdn.edge_cost +
                                   2 * static_cast<int>(
                                           __builtin_popcountll(used));
                 if (total > opt.max_cost) return;
                 Best& b = candidates[mi];
                 b.ok = true;
                 b.total = total;
                 b.used_mask = used;
                 b.pun = std::move(pun);
                 b.pdn = std::move(pdn);
               });
  for (std::size_t mi = 0; mi < candidates.size(); ++mi) {
    const Best& b = candidates[mi];
    if (!b.ok || b.total >= best.total) continue;
    best.ok = true;
    best.total = b.total;
    best.used_mask = b.used_mask;
    best.pun = b.pun;
    best.pdn = b.pdn;
  }

  if (!best.ok) {
    out.reason = "no complementary PUN/PDN path cover found";
    return out;
  }

  // Truth targets for the joint electrical check: every output must be driven
  // to 0/1 in every row by exactly one rail, through a strong path.
  std::vector<std::vector<int>> truth_targets;
  truth_targets.reserve(funcs.size());
  for (const auto& f : funcs) {
    std::vector<int> t;
    for (std::uint32_t m = 0; m < rows; ++m) t.push_back(f.value(m) ? 1 : 0);
    truth_targets.push_back(std::move(t));
  }

  // Emit the selected paths into a fresh graph.  A path's last switch lands
  // directly on its output net: there is deliberately no zero-cost "wire"
  // connector, because a node shared by several outputs would short them
  // together and the per-output cone check would never see it.  Prefix nodes
  // may be shared between outputs when share_prefixes is set; the finished
  // graph still has to pass the joint check below.
  auto build_graph = [&](bool share_prefixes, SwitchGraph& g) {
    const int zero = g.add_net("0");
    const int one = g.add_net("1");
    g.source_ids = {zero, one};
    std::vector<int> output_nodes;
    for (const auto& n : names) output_nodes.push_back(g.add_net("Y" + n));
    g.outputs = output_nodes;
    g.output = output_nodes.empty() ? one : output_nodes.front();
    std::map<std::string, int> nodes;
    nodes.emplace("0", zero);
    nodes.emplace("1", one);
    std::map<std::string, bool> added;
    auto node = [&](const std::string& name, int output) {
      const std::string key =
          share_prefixes ? name : name + "@" + std::to_string(output);
      auto it = nodes.find(key);
      if (it != nodes.end()) return it->second;
      const int id = g.add_net(key);
      nodes.emplace(key, id);
      return id;
    };
    auto emit = [&](const RailSolution& r) {
      for (int pi : r.selected) {
        const PathInfo& p = r.paths[static_cast<std::size_t>(pi)];
        const Branch& b = r.all[static_cast<std::size_t>(p.path)].branch;
        const int o = p.output;
        int prev = node(b.source, o);
        for (std::size_t j = 0; j < p.edges.size(); ++j) {
          const int eid = p.edges[j];
          const std::string& k = r.keys[static_cast<std::size_t>(eid)];
          const bool last = j + 1 == p.edges.size();
          const int next =
              last ? output_nodes[static_cast<std::size_t>(o)] : node(k, o);
          const std::string key =
              last ? ("term:" + std::to_string(o) + ":" + k)
                   : (share_prefixes ? k : k + "@" + std::to_string(o));
          if (!added[key]) {
            SwitchGraphEdge e;
            e.from = prev;
            e.to = next;
            e.kind = r.edges[static_cast<std::size_t>(eid)].kind;
            e.lit = r.edges[static_cast<std::size_t>(eid)].lit;
            e.strong = true;
            e.cost = r.edges[static_cast<std::size_t>(eid)].cost;
            g.edges.push_back(std::move(e));
            added[key] = true;
          }
          prev = next;
        }
      }
    };
    emit(best.pun);
    emit(best.pdn);
  };

  auto graph_cost = [](const SwitchGraph& g) {
    int cost = 0;
    for (const auto& e : g.edges) cost += e.cost;
    return cost;
  };
  auto graph_ok = [&](const SwitchGraph& g) {
    const auto prof =
        analyze_switch_graph_outputs(g, vars, truth_targets, false);
    if (prof.size() != names.size()) return false;
    for (const auto& p : prof)
      if (!p.valid) return false;
    return true;
  };

  // Prefer the cheaper shared-prefix network, but only when the complete
  // network passes the joint check.  Otherwise fall back to a private copy per
  // output (no shared nodes at all), which cannot create cross-output paths.
  SwitchGraph shared;
  build_graph(true, shared);
  SwitchGraph private_g;
  build_graph(false, private_g);
  if (graph_ok(shared)) {
    out.graph = std::move(shared);
  } else if (graph_ok(private_g)) {
    out.graph = std::move(private_g);
  } else {
    out.reason = "dual-rail path network failed the joint electrical check";
    return out;
  }

  out.edge_cost = graph_cost(out.graph);
  out.inverter_cost =
      2 * static_cast<int>(__builtin_popcountll(best.used_mask));
  out.total = out.edge_cost + out.inverter_cost;
  if (out.total > opt.max_cost) {
    out.reason = "valid dual-rail network needs " + std::to_string(out.total) +
                 "T, over --max-cost";
    return out;
  }
  out.verified = true;
  out.proof = true;
  out.ok = true;
  return out;
}

}  // namespace

MultiPathSynthesisResult synthesize_path_variables_multi(
    const std::vector<BoolFunc>& funcs, const std::vector<std::string>& names,
    const std::vector<std::string>& vars, const SpaceOptions& opt,
    bool tri_state) {
  MultiPathSynthesisResult out;
  out.outputs = names;
  if (funcs.empty() || funcs.size() != names.size() || vars.empty() ||
      vars.size() > 4) {
    out.reason = "multi path synthesis supports 1..4 inputs and matching outputs";
    return out;
  }
  if (!tri_state) return synthesize_dual_rail_multi(funcs, names, vars, opt);
  std::vector<std::vector<int>> targets;
  std::vector<AllPath> all;
  for (std::size_t o = 0; o < funcs.size(); ++o) {
    std::vector<int> target;
    for (std::uint32_t m = 0; m < funcs[o].num_minterms(); ++m)
      target.push_back(tri_state ? (funcs[o].value(m) ? 1 : -1)
                                 : (funcs[o].value(m) ? 1 : 0));
    targets.push_back(target);
    const auto branches = usable_branches(vars, target, opt);
    for (const auto& b : branches) {
      if (!tri_state && b.source != "0" && b.source != "1") continue;
      all.push_back(AllPath{b, static_cast<int>(o)});
    }
  }
  if (all.empty()) {
    out.reason = "no usable paths";
    return out;
  }
  if (all.size() > 512) {
    out.reason = "multi path candidate set exceeds 512";
    return out;
  }

  std::map<std::string, int> edge_ids;
  std::vector<EdgeInfo> edges;
  std::vector<std::string> edge_keys;
  std::vector<PathInfo> paths;
  for (std::size_t ai = 0; ai < all.size(); ++ai) {
    const auto& b = all[ai].branch;
    PathInfo p;
    p.path = static_cast<int>(ai);
    p.output = all[ai].output;
    for (std::size_t j = 0; j < b.chain.size(); ++j) {
      const std::string key = edge_key(b.source, b.chain, j);
      auto it = edge_ids.find(key);
      int id;
      if (it == edge_ids.end()) {
        id = static_cast<int>(edges.size());
        edge_ids.emplace(key, id);
        edges.push_back(EdgeInfo{switch_cost(b.chain[j].kind), b.chain[j].kind,
                                 b.chain[j].lit});
        edge_keys.push_back(key);
      } else {
        id = it->second;
      }
      p.edges.push_back(id);
    }
    paths.push_back(std::move(p));
  }

  Search s(all, paths, edges, targets, opt.max_cost);
  s.search();
  if (s.best == std::numeric_limits<int>::max() || s.best_selected.empty()) {
    out.reason = "no multi-output path cover found";
    return out;
  }

  std::set<int> chosen_edges;
  std::uint32_t mask = 0;
  for (int pi : s.best_selected) {
    const auto& p = paths[static_cast<std::size_t>(pi)];
    out.selected.push_back(p.path);
    mask |= all[static_cast<std::size_t>(p.path)].branch.req_mask;
    for (int e : p.edges) chosen_edges.insert(e);
  }
  int gate_cost = 0;
  for (int e : chosen_edges) gate_cost += edges[static_cast<std::size_t>(e)].cost;
  out.edge_cost = gate_cost;
  out.inverter_cost = 2 * static_cast<int>(__builtin_popcountll(mask));
  out.total = gate_cost + out.inverter_cost;

  SwitchGraph& g = out.graph;
  const int zero = g.add_net("0");
  const int one = g.add_net("1");
  g.source_ids = {zero, one};
  std::vector<int> output_nodes;
  for (std::size_t o = 0; o < names.size(); ++o)
    output_nodes.push_back(g.add_net("Y" + names[o]));
  g.outputs = output_nodes;
  g.output = output_nodes[0];
  std::map<std::string, int> nodes;
  nodes.emplace("0", zero);
  nodes.emplace("1", one);
  std::map<std::string, bool> added_edge;
  std::set<std::string> selected_sources;
  auto node = [&](const std::string& name) {
    auto it = nodes.find(name);
    if (it != nodes.end()) return it->second;
    const int id = g.add_net(name);
    nodes.emplace(name, id);
    return id;
  };
  for (int pi : s.best_selected) {
    const auto& p = paths[static_cast<std::size_t>(pi)];
    const auto& b = all[static_cast<std::size_t>(p.path)].branch;
    selected_sources.insert(b.source);
    int prev = node(b.source);
    for (std::size_t j = 0; j < p.edges.size(); ++j) {
      const int eid = p.edges[j];
      const std::string& k = edge_keys[static_cast<std::size_t>(eid)];
      const int next = node(k);
      if (!added_edge[k]) {
        SwitchGraphEdge e;
        e.from = prev;
        e.to = next;
        e.kind = edges[static_cast<std::size_t>(eid)].kind;
        e.lit = edges[static_cast<std::size_t>(eid)].lit;
        e.strong = true;
        e.cost = edges[static_cast<std::size_t>(eid)].cost;
        g.edges.push_back(std::move(e));
        added_edge[k] = true;
      }
      prev = next;
    }
    const std::string wire = "wire:" + std::to_string(prev) + ":" +
                             std::to_string(p.output);
    if (!added_edge[wire]) {
      SwitchGraphEdge e;
      e.from = prev;
      e.to = output_nodes[static_cast<std::size_t>(p.output)];
      e.kind = SwKind::TGH;
      e.lit = "1";
      e.strong = true;
      e.cost = 0;
      g.edges.push_back(std::move(e));
      added_edge[wire] = true;
    }
  }

  for (const auto& s : selected_sources) {
    const int id = node(s);
    if (std::find(g.source_ids.begin(), g.source_ids.end(), id) == g.source_ids.end())
      g.source_ids.push_back(id);
  }

  // Static mode: verify PUN and PDN as separate rail graphs.  A high target
  // row must be reachable from rail 1 only through selected PUN paths, and a
  // low row from rail 0 only through selected PDN paths.  This is the PUN/PDN
  // complementarity the single mixed graph cannot express cleanly.
  auto rail_ok = [&](const std::string& rail, int rv) {
    SwitchGraph r;
    const int src = r.add_net(rail);
    r.source_ids = {src};
    std::vector<int> routs;
    for (const auto& n : names) routs.push_back(r.add_net("Y" + n));
    r.outputs = routs;
    r.output = routs.empty() ? src : routs[0];
    std::map<std::string, int> rnodes;
    rnodes.emplace(rail, src);
    std::map<std::string, bool> redges;
    std::set<std::string> rsources;
    auto rnode = [&](const std::string& n) {
      auto it = rnodes.find(n);
      if (it != rnodes.end()) return it->second;
      const int id = r.add_net(n);
      rnodes.emplace(n, id);
      return id;
    };
    for (int pi : s.best_selected) {
      const auto& p = paths[static_cast<std::size_t>(pi)];
      const auto& b = all[static_cast<std::size_t>(p.path)].branch;
      if (b.source != rail) continue;
      rsources.insert(b.source);
      int prev = rnode(b.source);
      for (std::size_t j = 0; j < p.edges.size(); ++j) {
        const int eid = p.edges[j];
        const std::string& k = edge_keys[static_cast<std::size_t>(eid)];
        const int next = rnode(k);
        if (!redges[k]) {
          SwitchGraphEdge e;
          e.from = prev; e.to = next;
          e.kind = edges[static_cast<std::size_t>(eid)].kind;
          e.lit = edges[static_cast<std::size_t>(eid)].lit;
          e.strong = true; e.cost = edges[static_cast<std::size_t>(eid)].cost;
          r.edges.push_back(std::move(e));
          redges[k] = true;
        }
        prev = next;
      }
      const std::string wire = "wire:" + std::to_string(prev) + ":" +
                               std::to_string(p.output);
      if (!redges[wire]) {
        SwitchGraphEdge e;
        e.from = prev; e.to = routs[static_cast<std::size_t>(p.output)];
        e.kind = SwKind::TGH; e.lit = "1"; e.strong = true; e.cost = 0;
        r.edges.push_back(std::move(e));
        redges[wire] = true;
      }
    }
    std::vector<std::vector<int>> rt(names.size());
    for (std::size_t o = 0; o < names.size(); ++o)
      for (int m = 0; m < static_cast<int>(targets[o].size()); ++m)
        rt[o].push_back(targets[o][static_cast<std::size_t>(m)] == rv ? rv : -1);
    const auto prof = analyze_switch_graph_outputs(r, vars, rt, true);
    if (prof.size() != names.size()) return false;
    for (const auto& p : prof)
      if (!p.valid) return false;
    return true;
  };

  const auto profiles = analyze_switch_graph_outputs(g, vars, targets, tri_state);
  if (tri_state) {
    out.verified = profiles.size() == names.size();
    for (const auto& p : profiles) out.verified = out.verified && p.valid;
  } else {
    out.verified = rail_ok("1", 1) && rail_ok("0", 0);
  }
  out.ok = true;
  out.proof = true;
  if (!out.verified) {
    std::string detail;
    for (std::size_t oi = 0; oi < profiles.size(); ++oi) {
      detail += " " + names[oi] + ":";
      for (int c : profiles[oi].driver_count) detail += std::to_string(c);
    }
    out.reason = "multi-output path graph failed verification" + detail;
    if (!profiles.empty()) {
      out.reason += " first=";
      for (const auto& r0 : profiles[0].rows) {
        out.reason += "[";
        for (const auto& s : r0.sources) out.reason += s + ",";
        out.reason += "]";
      }
    }
  }
  return out;
}

}  // namespace dcstudy
