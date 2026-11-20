#include "dcstudy/multi_path.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "dcstudy/driver_profile.hpp"

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
    for (int m = 0; m < rows; ++m) {
      if (!b.behavior[static_cast<std::size_t>(m)].conducts) continue;
      const auto& d = driver[static_cast<std::size_t>(idx(p.output, m))];
      if (!d.empty() && d != b.source) return false;
    }
    return true;
  }

  void search() {
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

}  // namespace

MultiPathSynthesisResult synthesize_path_variables_multi(
    const std::vector<BoolFunc>& funcs, const std::vector<std::string>& names,
    const std::vector<std::string>& vars, const SpaceOptions& opt) {
  MultiPathSynthesisResult out;
  out.outputs = names;
  if (funcs.empty() || funcs.size() != names.size() || vars.empty() ||
      vars.size() > 4) {
    out.reason = "multi path synthesis supports 1..4 inputs and matching outputs";
    return out;
  }
  std::vector<std::vector<int>> targets;
  std::vector<AllPath> all;
  for (std::size_t o = 0; o < funcs.size(); ++o) {
    std::vector<int> target;
    for (std::uint32_t m = 0; m < funcs[o].num_minterms(); ++m)
      target.push_back(funcs[o].value(m) ? 1 : 0);
    targets.push_back(target);
    const auto branches = usable_branches(vars, target, opt);
    for (const auto& b : branches) all.push_back(AllPath{b, static_cast<int>(o)});
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
      g.edges.push_back(std::move(e));
      added_edge[wire] = true;
    }
  }

  for (const auto& s : selected_sources) {
    const int id = node(s);
    if (std::find(g.source_ids.begin(), g.source_ids.end(), id) == g.source_ids.end())
      g.source_ids.push_back(id);
  }
  const auto profiles = analyze_switch_graph_outputs(g, vars, targets);
  out.verified = profiles.size() == names.size();
  for (const auto& p : profiles) out.verified = out.verified && p.valid;
  out.ok = true;
  out.proof = true;
  if (!out.verified) {
    std::string detail;
    for (std::size_t oi = 0; oi < profiles.size(); ++oi) {
      detail += " " + names[oi] + ":";
      for (int c : profiles[oi].driver_count) detail += std::to_string(c);
    }
    out.reason = "multi-output path graph failed verification" + detail;
  }
  return out;
}

}  // namespace dcstudy
