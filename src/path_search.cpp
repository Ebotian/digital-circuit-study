#include "dcstudy/path_search.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
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

struct PathInfo {
  int branch = -1;
  std::string source;
  std::vector<int> edges;
};

struct Search {
  const std::vector<Branch>& branches;
  const std::vector<PathInfo>& paths;
  const std::vector<EdgeInfo>& edge_info;
  const std::vector<int>& target;
  int rows = 0;
  int best = std::numeric_limits<int>::max();
  std::vector<int> best_selected;
  std::vector<int> selected;
  std::vector<bool> chosen;
  std::vector<int> edge_count;
  int edge_cost = 0;
  std::uint32_t inv_mask = 0;
  std::vector<std::string> driver;
  std::vector<bool> strong_cover;
  std::size_t nodes = 0;

  Search(const std::vector<Branch>& b, const std::vector<PathInfo>& p,
         const std::vector<EdgeInfo>& ei, const std::vector<int>& t)
      : branches(b), paths(p), edge_info(ei), target(t),
        rows(static_cast<int>(t.size())),
        chosen(p.size(), false), edge_count(edge_info.size(), 0),
        driver(static_cast<std::size_t>(rows)), strong_cover(static_cast<std::size_t>(rows), false) {}

  void add_path(int pi, std::vector<std::pair<int, std::string>>& old_driver,
                std::vector<std::pair<int, bool>>& old_strong, int& old_edges,
                std::uint32_t& old_mask) {
    const auto& p = paths[static_cast<std::size_t>(pi)];
    const auto& b = branches[static_cast<std::size_t>(p.branch)];
    old_edges = edge_cost;
    old_mask = inv_mask;
    for (int e : p.edges) {
      if (edge_count[static_cast<std::size_t>(e)]++ == 0)
        edge_cost += edge_info[static_cast<std::size_t>(e)].cost;
    }
    inv_mask |= b.req_mask;
    for (int m = 0; m < rows; ++m) {
      if (!b.behavior[static_cast<std::size_t>(m)].conducts) continue;
      old_driver.emplace_back(m, driver[static_cast<std::size_t>(m)]);
      old_strong.emplace_back(m, strong_cover[static_cast<std::size_t>(m)]);
      driver[static_cast<std::size_t>(m)] = b.source;
      if (b.behavior[static_cast<std::size_t>(m)].strong)
        strong_cover[static_cast<std::size_t>(m)] = true;
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
    const auto& b = branches[static_cast<std::size_t>(p.branch)];
    for (int m = 0; m < rows; ++m) {
      if (!b.behavior[static_cast<std::size_t>(m)].conducts) continue;
      const auto& d = driver[static_cast<std::size_t>(m)];
      if (!d.empty() && d != b.source) return false;
    }
    return true;
  }

  void search() {
    if (edge_cost + 2 * static_cast<int>(__builtin_popcountll(inv_mask)) >= best)
      return;
    int row = -1;
    int best_choices = std::numeric_limits<int>::max();
    for (int m = 0; m < rows; ++m) {
      if (strong_cover[static_cast<std::size_t>(m)]) continue;
      int count = 0;
      for (std::size_t pi = 0; pi < paths.size(); ++pi) {
        if (chosen[pi]) continue;
        const auto& b = branches[static_cast<std::size_t>(paths[pi].branch)];
        const auto& bh = b.behavior[static_cast<std::size_t>(m)];
        if (bh.conducts && bh.strong && compatible(paths[pi])) ++count;
      }
      if (count < best_choices) {
        best_choices = count;
        row = m;
      }
    }
    if (row < 0) {
      const int total = edge_cost + 2 * static_cast<int>(__builtin_popcountll(inv_mask));
      if (total < best) {
        best = total;
        best_selected = selected;
      }
      return;
    }
    if (best_choices == 0) return;

    for (std::size_t pi = 0; pi < paths.size(); ++pi) {
      if (chosen[pi]) continue;
      const auto& b = branches[static_cast<std::size_t>(paths[pi].branch)];
      const auto& bh = b.behavior[static_cast<std::size_t>(row)];
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

PathSynthesisResult synthesize_path_variables(
    const BoolFunc& f, const std::vector<std::string>& vars,
    const SpaceOptions& opt) {
  PathSynthesisResult out;
  if (vars.empty() || vars.size() > 4) {
    out.reason = "path synthesis supports 1..4 inputs";
    return out;
  }
  std::vector<int> target;
  for (std::uint32_t m = 0; m < f.num_minterms(); ++m)
    target.push_back(f.value(m) ? 1 : 0);
  const auto all = usable_branches(vars, target, opt);
  if (all.empty()) {
    out.reason = "no usable paths";
    return out;
  }
  if (all.size() > 256) {
    out.reason = "path candidate set exceeds 256; narrow the space";
    return out;
  }

  std::map<std::string, int> edge_ids;
  std::vector<EdgeInfo> edges;
  std::vector<std::string> edge_keys;
  std::vector<PathInfo> paths;
  for (std::size_t bi = 0; bi < all.size(); ++bi) {
    const auto& b = all[bi];
    PathInfo p;
    p.branch = static_cast<int>(bi);
    p.source = b.source;
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

  Search s(all, paths, edges, target);
  s.search();
  if (s.best == std::numeric_limits<int>::max() || s.best_selected.empty()) {
    out.reason = "no path-variable cover found";
    return out;
  }

  out.selected.clear();
  std::set<int> chosen_edges;
  std::uint32_t mask = 0;
  for (int pi : s.best_selected) {
    const auto& p = paths[static_cast<std::size_t>(pi)];
    out.selected.push_back(p.branch);
    mask |= all[static_cast<std::size_t>(p.branch)].req_mask;
    for (int e : p.edges) chosen_edges.insert(e);
  }
  int gate_cost = 0;
  for (int e : chosen_edges) gate_cost += edges[static_cast<std::size_t>(e)].cost;
  out.edge_cost = gate_cost;
  out.inverter_cost = 2 * static_cast<int>(__builtin_popcountll(mask));
  out.total = gate_cost + out.inverter_cost;

  SwitchGraph& g = out.graph;
  const int y = g.add_net("Y");
  g.output = y;
  std::map<std::string, int> nodes;
  std::map<std::string, bool> added_edge;
  std::map<int, std::string> path_source;
  auto node = [&](const std::string& name) {
    auto it = nodes.find(name);
    if (it != nodes.end()) return it->second;
    const int id = g.add_net(name);
    nodes.emplace(name, id);
    return id;
  };
  for (int pi : s.best_selected) {
    const auto& p = paths[static_cast<std::size_t>(pi)];
    const auto& b = all[static_cast<std::size_t>(p.branch)];
    const int src = node(b.source);
    path_source[src] = b.source;
    int prev = src;
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
    const std::string wire = "wire:" + std::to_string(prev);
    if (!added_edge[wire]) {
      SwitchGraphEdge e;
      e.from = prev;
      e.to = y;
      e.kind = SwKind::TGH;
      e.lit = "1";
      e.strong = true;
      g.edges.push_back(std::move(e));
      added_edge[wire] = true;
    }
  }
  for (const auto& [id, name] : path_source) {
    (void)name;
    g.source_ids.push_back(id);
  }

  out.truths.push_back(f.truth_string());
  const DriverProfile dp = analyze_switch_graph(g, vars, target);
  out.verified = dp.valid;
  out.ok = true;
  out.proof = true;
  if (!out.verified) out.reason = "path graph failed dynamic driver validation";
  return out;
}

}  // namespace dcstudy
