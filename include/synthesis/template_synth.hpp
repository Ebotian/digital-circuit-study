// Bounded template optimization: the candidate switch graph is fixed, but
// every edge is an optional topology variable.  This is the first finite
// template layer before edge kind/control and flow variables are added.
#pragma once

#include <string>
#include <vector>

#include "core/driver_profile.hpp"

namespace dcstudy {

struct EdgeChoice {
  SwKind kind = SwKind::TGH;
  std::string lit;
  bool strong = false;
  int cost = 0;
};

struct TemplateEdge {
  int from = 0;
  int to = 0;
  SwKind kind = SwKind::TGH; // fallback if choices is empty
  std::string lit;
  bool strong = false;
  int cost = 0;
  std::vector<EdgeChoice> choices;
};

struct NetworkTemplate {
  std::vector<std::string> nets;
  std::vector<TemplateEdge> edges;
  std::vector<int> source_ids;
  std::vector<int> outputs;
  int inverter_cost = 0;
};

struct TemplateResult {
  bool ok = false;
  bool proof = false;
  bool verified = false;
  std::string reason;
  int total = 0;
  int edge_cost = 0;
  int inverter_cost = 0;
  std::vector<int> selected_edges;
  std::vector<int> selected_choices; // -1 if an edge is not selected
  SwitchGraph graph;
};

NetworkTemplate template_from_graph(const SwitchGraph &g, int inverter_cost);

TemplateResult optimize_template(const NetworkTemplate &t,
                                 const std::vector<std::string> &vars,
                                 const std::vector<std::vector<int>> &targets);

} // namespace dcstudy
