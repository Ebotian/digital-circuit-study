#include "dcstudy/tri_decoder.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dcstudy {

TriDecoderResult synthesize_tri_decoder(int bits,
                                        const std::vector<std::string>& vars) {
  TriDecoderResult out;
  if (bits < 1 || bits > 4 || vars.size() < static_cast<std::size_t>(bits)) {
    out.reason = "tri decoder supports 1..4 bits and matching input names";
    return out;
  }
  SwitchGraph& g = out.graph;
  const int root = g.add_net("0");
  g.source_ids = {root};
  std::vector<int> level{root};
  int tg = 0;
  for (int b = 0; b < bits; ++b) {
    std::vector<int> next;
    next.reserve(level.size() * 2);
    for (int parent : level) {
      const int lo = g.add_net("n" + std::to_string(g.nets.size()));
      const int hi = g.add_net("n" + std::to_string(g.nets.size()));
      SwitchGraphEdge e0;
      e0.from = parent;
      e0.to = lo;
      e0.kind = SwKind::TGL;
      e0.lit = vars[static_cast<std::size_t>(b)];
      e0.strong = true;
      e0.cost = 2;
      g.edges.push_back(std::move(e0));
      SwitchGraphEdge e1;
      e1.from = parent;
      e1.to = hi;
      e1.kind = SwKind::TGH;
      e1.lit = vars[static_cast<std::size_t>(b)];
      e1.strong = true;
      e1.cost = 2;
      g.edges.push_back(std::move(e1));
      tg += 2;
      next.push_back(lo);
      next.push_back(hi);
    }
    level = std::move(next);
  }
  out.tg_count = tg;
  out.inverter_cost = 2 * bits;
  out.total = 2 * tg + out.inverter_cost;
  out.outputs.clear();
  for (std::size_t i = 0; i < level.size(); ++i) {
    const std::string name = "Y" + std::to_string(i);
    out.outputs.push_back(name);
    g.outputs.push_back(level[i]);
  }
  g.output = g.outputs.empty() ? root : g.outputs[0];

  std::vector<std::vector<int>> targets(out.outputs.size());
  for (std::size_t o = 0; o < out.outputs.size(); ++o) {
    for (std::uint32_t m = 0; m < (std::uint32_t{1} << bits); ++m) {
      targets[o].push_back(m == o ? 0 : -1);  // active-low, otherwise high-Z
    }
  }
  const auto profiles =
      analyze_switch_graph_outputs(g, vars, targets, true);
  out.verified = profiles.size() == targets.size();
  for (const auto& p : profiles)
    out.verified = out.verified && p.valid;
  out.ok = out.verified;
  if (!out.verified) out.reason = "tri-state TG decoder failed verification";
  return out;
}

}  // namespace dcstudy
