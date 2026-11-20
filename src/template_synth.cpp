#include "dcstudy/template_synth.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "dcstudy/driver_profile.hpp"

namespace dcstudy {
namespace {

SwitchGraph graph_of(const NetworkTemplate& t, const std::vector<char>& chosen) {
  SwitchGraph g;
  g.nets = t.nets;
  g.source_ids = t.source_ids;
  g.outputs = t.outputs;
  g.output = t.outputs.empty() ? 0 : t.outputs[0];
  for (std::size_t i = 0; i < t.edges.size(); ++i) {
    if (!chosen[i]) continue;
    const auto& e = t.edges[i];
    SwitchGraphEdge ge;
    ge.from = e.from;
    ge.to = e.to;
    ge.kind = e.kind;
    ge.lit = e.lit;
    ge.strong = e.strong;
    ge.cost = e.cost;
    g.edges.push_back(std::move(ge));
  }
  return g;
}

bool verify_graph(const NetworkTemplate& t, const std::vector<char>& chosen,
                  const std::vector<std::string>& vars,
                  const std::vector<std::vector<int>>& targets) {
  const SwitchGraph g = graph_of(t, chosen);
  if (g.source_ids.empty() || g.outputs.empty()) return false;
  const auto profiles = analyze_switch_graph_outputs(g, vars, targets);
  if (profiles.size() != targets.size()) return false;
  for (const auto& p : profiles)
    if (!p.valid) return false;
  return true;
}

struct Search {
  const NetworkTemplate& t;
  const std::vector<std::string>& vars;
  const std::vector<std::vector<int>>& targets;
  std::vector<char> chosen;
  int best = std::numeric_limits<int>::max();
  std::vector<char> best_chosen;
  std::size_t visited = 0;

  Search(const NetworkTemplate& tt, const std::vector<std::string>& v,
         const std::vector<std::vector<int>>& tg)
      : t(tt), vars(v), targets(tg), chosen(tt.edges.size(), 1) {}

  void dfs(std::size_t i, int cost) {
    ++visited;
    if (cost >= best) return;
    if (i == t.edges.size()) {
      if (verify_graph(t, chosen, vars, targets)) {
        best = cost;
        best_chosen = chosen;
      }
      return;
    }
    // Exclude first: low-cost subsets are found early.
    chosen[i] = 0;
    dfs(i + 1, cost);
    chosen[i] = 1;
    dfs(i + 1, cost + t.edges[i].cost);
    chosen[i] = 1;
  }
};

}  // namespace

NetworkTemplate template_from_graph(const SwitchGraph& g, int inverter_cost) {
  NetworkTemplate t;
  t.nets = g.nets;
  t.source_ids = g.source_ids;
  t.outputs = g.outputs;
  if (t.outputs.empty() && !g.nets.empty()) t.outputs.push_back(g.output);
  t.inverter_cost = inverter_cost;
  for (const auto& e : g.edges) {
    TemplateEdge te;
    te.from = e.from;
    te.to = e.to;
    te.kind = e.kind;
    te.lit = e.lit;
    te.strong = e.strong;
    te.cost = e.cost;
    t.edges.push_back(std::move(te));
  }
  return t;
}

TemplateResult optimize_template(const NetworkTemplate& t,
                                 const std::vector<std::string>& vars,
                                 const std::vector<std::vector<int>>& targets) {
  TemplateResult out;
  out.inverter_cost = t.inverter_cost;
  if (t.edges.empty() || t.source_ids.empty() || t.outputs.empty()) {
    out.reason = "empty template";
    return out;
  }
  if (targets.empty()) {
    out.reason = "no target outputs";
    return out;
  }
  for (const auto& target : targets)
    if (target.size() != (std::size_t{1} << vars.size())) {
      out.reason = "target width mismatch";
      return out;
    }

  const auto all_cost = [&] {
    int c = t.inverter_cost;
    for (const auto& e : t.edges) c += e.cost;
    return c;
  }();
  std::vector<char> all(t.edges.size(), 1);
  if (!verify_graph(t, all, vars, targets)) {
    out.reason = "template has no valid full-edge solution";
    return out;
  }

  std::vector<char> best = all;
  int best_cost = all_cost;
  bool proof = true;
  if (t.edges.size() <= 20) {
    Search s(t, vars, targets);
    s.best = best_cost;
    s.best_chosen = all;
    s.dfs(0, t.inverter_cost);
    best = s.best_chosen;
    best_cost = s.best;
  } else {
    // Larger fixed templates: greedy edge deletion with full C^*
    // verification after every deletion.  This is a bound, not a proof.
    proof = false;
    bool changed = true;
    while (changed) {
      changed = false;
      for (std::size_t i = 0; i < t.edges.size(); ++i) {
        if (!best[i]) continue;
        best[i] = 0;
        if (verify_graph(t, best, vars, targets)) {
          changed = true;
          best_cost -= t.edges[i].cost;
        } else {
          best[i] = 1;
        }
      }
    }
  }

  auto chosen_edges = [&] {
    std::vector<int> ids;
    for (std::size_t i = 0; i < best.size(); ++i)
      if (best[i]) ids.push_back(static_cast<int>(i));
    return ids;
  };
  out.selected_edges = chosen_edges();
  out.edge_cost = best_cost - t.inverter_cost;
  out.total = best_cost;
  out.graph = graph_of(t, best);
  out.verified = verify_graph(t, best, vars, targets);
  out.proof = proof;
  out.ok = out.verified;
  return out;
}

}  // namespace dcstudy
