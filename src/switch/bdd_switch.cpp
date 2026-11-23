#include "switch/bdd_switch.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dcstudy {
namespace {

struct BddNode {
  bool leaf = false;
  bool value = false;
  int var = 0;
  int low = 0;
  int high = 0;
};

Bits cofactor(const Bits& f, std::size_t n, std::size_t var, bool value) {
  Bits out(f.size());
  for (std::uint32_t m = 0; m < f.size(); ++m) {
    const bool bit = ((m >> (n - 1 - var)) & 1U) != 0;
    if (bit == value && f.test(m)) out.set(m);
  }
  return out;
}

struct Bdd {
  std::vector<BddNode> nodes;
  int root = 0;
  std::set<int> used_vars;

  Bdd() {
    BddNode zero;
    zero.leaf = true;
    zero.value = false;
    nodes.push_back(zero);
    BddNode one;
    one.leaf = true;
    one.value = true;
    nodes.push_back(one);
  }

  int build(std::size_t n, std::size_t level, const Bits& f,
            std::map<std::string, int>& memo, const std::vector<int>& order) {
    if (f.none()) return 0;
    if (f.count() == f.size()) return 1;
    if (level >= n) return f.any() ? 1 : 0;
    const int var = order[level];
    const std::string key = std::to_string(var) + ":" + f.to_binary_string();
    auto it = memo.find(key);
    if (it != memo.end()) return it->second;
    const int low = build(n, level + 1, cofactor(f, n, var, false), memo, order);
    const int high = build(n, level + 1, cofactor(f, n, var, true), memo, order);
    if (low == high) {
      memo.emplace(key, low);
      return low;
    }
    used_vars.insert(static_cast<int>(var));
    BddNode node;
    node.var = var;
    node.low = low;
    node.high = high;
    const int id = static_cast<int>(nodes.size());
    nodes.push_back(node);
    memo.emplace(key, id);
    return id;
  }
};

}  // namespace

BddSwitchDesign synthesize_bdd_switch_order(
    const BoolFunc& f, const std::vector<std::string>& vars,
    const std::vector<int>& order) {
  BddSwitchDesign out;
  const std::size_t n = vars.size();
  if (n == 0 || n > 4) {
    out.reason = "BDD switch construction supports 1..4 inputs";
    return out;
  }
  Bdd bdd;
  std::map<std::string, int> memo;
  bdd.root = bdd.build(n, 0, f.bits(), memo, order);
  if (bdd.root < 2) {
    out.reason = "constant BDD has no switch path";
    return out;
  }

  SwitchGraph& g = out.graph;
  const int zero = g.add_net("0");
  const int one = g.add_net("1");
  g.source_ids = {zero, one};
  std::map<int, int> net_of;
  auto net = [&](auto&& self, int id) -> int {
    auto it = net_of.find(id);
    if (it != net_of.end()) return it->second;
    if (id == 0) return zero;
    if (id == 1) return one;
    const BddNode& node = bdd.nodes[static_cast<std::size_t>(id)];
    const int here = g.add_net("b" + std::to_string(id));
    net_of[id] = here;
    const int lo = self(self, node.low);
    const int hi = self(self, node.high);
    SwitchGraphEdge el;
    el.from = here;
    el.to = lo;
    el.kind = SwKind::TGL;
    el.lit = vars[static_cast<std::size_t>(node.var)];
    el.strong = true;
    el.cost = 2;
    g.edges.push_back(el);
    SwitchGraphEdge eh;
    eh.from = here;
    eh.to = hi;
    eh.kind = SwKind::TGH;
    eh.lit = vars[static_cast<std::size_t>(node.var)];
    eh.strong = true;
    eh.cost = 2;
    g.edges.push_back(eh);
    return here;
  };
  g.output = net(net, bdd.root);

  out.order = order;
  out.nodes = static_cast<int>(bdd.nodes.size()) - 2;
  out.inverters = 2 * static_cast<int>(bdd.used_vars.size());
  out.total = 4 * out.nodes + out.inverters;
  out.ok = true;

  std::vector<int> target;
  for (std::uint32_t m = 0; m < f.num_minterms(); ++m)
    target.push_back(f.value(m) ? 1 : 0);
  const DriverProfile p = analyze_switch_graph(g, vars, target);
  out.verified = p.valid;
  out.truths.push_back(f.truth_string());
  if (!p.valid) out.reason = "BDD switch graph failed dynamic driver validation";
  return out;
}

BddSwitchDesign synthesize_bdd_switch(const BoolFunc& f,
                                      const std::vector<std::string>& vars) {
  std::vector<int> order(vars.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
  return synthesize_bdd_switch_order(f, vars, order);
}

BddSwitchDesign synthesize_bdd_switch_best(const BoolFunc& f,
                                           const std::vector<std::string>& vars) {
  BddSwitchDesign best;
  best.ok = false;
  std::vector<int> order(vars.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
  do {
    BddSwitchDesign d = synthesize_bdd_switch_order(f, vars, order);
    if (d.ok && d.verified &&
        (!best.ok || d.total < best.total)) {
      best = std::move(d);
    }
  } while (std::next_permutation(order.begin(), order.end()));
  if (!best.ok) best.reason = "no verified BDD switch order";
  return best;
}

}  // namespace dcstudy
