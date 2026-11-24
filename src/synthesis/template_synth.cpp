#include "synthesis/template_synth.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "core/driver_profile.hpp"

namespace dcstudy {
namespace {

int choice_count(const TemplateEdge &e) {
  return e.choices.empty() ? 1 : static_cast<int>(e.choices.size());
}

EdgeChoice choice_at(const TemplateEdge &e, int i) {
  if (e.choices.empty()) {
    EdgeChoice c;
    c.kind = e.kind;
    c.lit = e.lit;
    c.strong = e.strong;
    c.cost = e.cost;
    return c;
  }
  return e.choices[static_cast<std::size_t>(i)];
}

SwitchGraph graph_of(const NetworkTemplate &t, const std::vector<char> &chosen,
                     const std::vector<int> &choice) {
  SwitchGraph g;
  g.nets = t.nets;
  g.source_ids = t.source_ids;
  g.outputs = t.outputs;
  g.output = t.outputs.empty() ? 0 : t.outputs[0];
  for (std::size_t i = 0; i < t.edges.size(); ++i) {
    if (!chosen[i])
      continue;
    const auto &te = t.edges[i];
    const EdgeChoice c = choice_at(te, choice.empty() ? 0 : choice[i]);
    SwitchGraphEdge e;
    e.from = te.from;
    e.to = te.to;
    e.kind = c.kind;
    e.lit = c.lit;
    e.strong = c.strong;
    e.cost = c.cost;
    g.edges.push_back(std::move(e));
  }
  return g;
}

bool verify_graph(const NetworkTemplate &t, const std::vector<char> &chosen,
                  const std::vector<int> &choice,
                  const std::vector<std::string> &vars,
                  const std::vector<std::vector<int>> &targets) {
  const SwitchGraph g = graph_of(t, chosen, choice);
  if (g.source_ids.empty() || g.outputs.empty())
    return false;
  const auto profiles = analyze_switch_graph_outputs(g, vars, targets);
  if (profiles.size() != targets.size())
    return false;
  for (const auto &p : profiles)
    if (!p.valid)
      return false;
  return true;
}

struct Search {
  const NetworkTemplate &t;
  const std::vector<std::string> &vars;
  const std::vector<std::vector<int>> &targets;
  std::vector<char> chosen;
  std::vector<int> choice;
  int best = std::numeric_limits<int>::max();
  std::vector<char> best_chosen;
  std::vector<int> best_choice;
  std::size_t visited = 0;

  Search(const NetworkTemplate &tt, const std::vector<std::string> &v,
         const std::vector<std::vector<int>> &tg)
      : t(tt), vars(v), targets(tg), chosen(tt.edges.size(), 0),
        choice(tt.edges.size(), -1) {}

  void dfs(std::size_t i, int cost) {
    ++visited;
    if (cost >= best)
      return;
    if (i == t.edges.size()) {
      if (verify_graph(t, chosen, choice, vars, targets)) {
        best = cost;
        best_chosen = chosen;
        best_choice = choice;
      }
      return;
    }
    chosen[i] = 0;
    choice[i] = -1;
    dfs(i + 1, cost);
    const int nchoice = choice_count(t.edges[i]);
    chosen[i] = 1;
    for (int c = 0; c < nchoice; ++c) {
      choice[i] = c;
      dfs(i + 1, cost + choice_at(t.edges[i], c).cost);
    }
    chosen[i] = 0;
    choice[i] = -1;
  }
};

} // namespace

NetworkTemplate template_from_graph(const SwitchGraph &g, int inverter_cost) {
  NetworkTemplate t;
  t.nets = g.nets;
  t.source_ids = g.source_ids;
  t.outputs = g.outputs;
  if (t.outputs.empty() && !g.nets.empty())
    t.outputs.push_back(g.output);
  t.inverter_cost = inverter_cost;
  for (const auto &e : g.edges) {
    TemplateEdge te;
    te.from = e.from;
    te.to = e.to;
    te.kind = e.kind;
    te.lit = e.lit;
    te.strong = e.strong;
    te.cost = e.cost;
    EdgeChoice c;
    c.kind = e.kind;
    c.lit = e.lit;
    c.strong = e.strong;
    c.cost = e.cost;
    te.choices.push_back(std::move(c));
    t.edges.push_back(std::move(te));
  }
  return t;
}

TemplateResult optimize_template(const NetworkTemplate &t,
                                 const std::vector<std::string> &vars,
                                 const std::vector<std::vector<int>> &targets) {
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
  for (const auto &target : targets)
    if (target.size() != (std::size_t{1} << vars.size())) {
      out.reason = "target width mismatch";
      return out;
    }

  std::vector<char> all(t.edges.size(), 1);
  std::vector<int> all_choice(t.edges.size(), 0);
  int all_cost = t.inverter_cost;
  for (const auto &e : t.edges)
    all_cost += choice_at(e, 0).cost;
  if (!verify_graph(t, all, all_choice, vars, targets)) {
    out.reason = "template has no valid full-edge solution";
    return out;
  }

  std::vector<char> best = all;
  std::vector<int> best_choice = all_choice;
  int best_cost = all_cost;
  bool proof = true;
  std::size_t combinations = 1;
  for (const auto &e : t.edges)
    combinations *= static_cast<std::size_t>(choice_count(e) + 1);
  // DFS over edge choices; 20 edges / 2M combinations keeps it interactive.
  constexpr std::size_t kMaxTemplateEdges = 20;
  constexpr std::size_t kMaxTemplateCombinations = 2'000'000;
  if (t.edges.size() <= kMaxTemplateEdges &&
      combinations <= kMaxTemplateCombinations) {
    Search s(t, vars, targets);
    s.best = best_cost;
    s.best_chosen = all;
    s.best_choice = all_choice;
    s.dfs(0, t.inverter_cost);
    best = s.best_chosen;
    best_choice = s.best_choice;
    best_cost = s.best;
  } else {
    proof = false;
    bool changed = true;
    while (changed) {
      changed = false;
      for (std::size_t i = 0; i < t.edges.size(); ++i) {
        if (!best[i])
          continue;
        best[i] = 0;
        if (verify_graph(t, best, best_choice, vars, targets)) {
          changed = true;
          best_cost -= choice_at(t.edges[i], best_choice[i]).cost;
          best_choice[i] = -1;
        } else {
          best[i] = 1;
        }
      }
    }
  }

  out.selected_edges.clear();
  out.selected_choices.clear();
  for (std::size_t i = 0; i < best.size(); ++i) {
    if (best[i]) {
      out.selected_edges.push_back(static_cast<int>(i));
      out.selected_choices.push_back(best_choice[i]);
    }
  }
  out.edge_cost = best_cost - t.inverter_cost;
  out.total = best_cost;
  out.graph = graph_of(t, best, best_choice);
  out.verified = verify_graph(t, best, best_choice, vars, targets);
  out.proof = proof;
  out.ok = out.verified;
  return out;
}

} // namespace dcstudy
