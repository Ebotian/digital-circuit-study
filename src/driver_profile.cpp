#include "dcstudy/driver_profile.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "dcstudy/boolmatrix.hpp"

namespace dcstudy {
namespace {

int var_index(const std::vector<std::string>& vars, const std::string& name) {
  for (std::size_t i = 0; i < vars.size(); ++i)
    if (vars[i] == name) return static_cast<int>(i);
  return -1;
}

int source_value(const std::string& src, std::uint32_t m,
                 const std::vector<std::string>& vars) {
  if (src == "0") return 0;
  if (src == "1") return 1;
  const bool neg = src.size() > 1 && src.back() == 'b';
  const std::string base = neg ? src.substr(0, src.size() - 1) : src;
  const int i = var_index(vars, base);
  if (i < 0) return 0;
  const int bit = static_cast<int>(
      (m >> (vars.size() - 1 - static_cast<std::size_t>(i))) & 1U);
  return neg ? 1 - bit : bit;
}

int literal_value(const std::string& lit, std::uint32_t m,
                  const std::vector<std::string>& vars) {
  return source_value(lit, m, vars);
}

bool switch_on(SwKind kind, int gate, int value) {
  switch (kind) {
    case SwKind::N: return gate == 1 && value == 0;
    case SwKind::P: return gate == 0 && value == 1;
    case SwKind::TGL: return gate == 0;
    case SwKind::TGH: return gate == 1;
  }
  return false;
}

struct Path {
  std::string source;
  std::vector<int> nodes;
  std::vector<ChainElem> elems;
  std::vector<bool> strong;
};

struct Graph {
  std::vector<std::string> names;
  std::vector<int> source_ids;
  int y = 0;
  std::vector<Path> paths;
};

Graph build_graph(const Design& d) {
  Graph g;
  std::map<std::string, int> ids;
  auto node = [&](const std::string& n) {
    auto it = ids.find(n);
    if (it != ids.end()) return it->second;
    const int id = static_cast<int>(g.names.size());
    g.names.push_back(n);
    ids.emplace(n, id);
    return id;
  };
  g.y = node("Y");
  std::set<std::string> source_names;
  for (const auto& b : d.branches) {
    source_names.insert(b.source);
    Path p;
    p.source = b.source;
    p.nodes.push_back(node(b.source));
    for (std::size_t j = 0; j < b.chain.size(); ++j) {
      p.nodes.push_back(node("n" + std::to_string(g.paths.size()) + "_" +
                             std::to_string(j)));
      p.elems.push_back(b.chain[j]);
    }
    p.nodes.push_back(g.y);
    p.strong.resize(b.behavior.size());
    for (std::size_t m = 0; m < b.behavior.size(); ++m)
      p.strong[m] = b.behavior[m].strong;
    g.paths.push_back(std::move(p));
  }
  for (const auto& s : source_names) g.source_ids.push_back(node(s));
  return g;
}

// Add this graph's C_m edges into the block at offset.
void fill_matrix(BoolMatrix& c, const Graph& g, std::uint32_t m,
                 const std::vector<std::string>& vars, std::size_t offset) {
  for (const auto& p : g.paths) {
    int value = source_value(p.source, m, vars);
    bool all_on = true;
    for (std::size_t j = 0; j < p.elems.size(); ++j) {
      const auto& e = p.elems[j];
      const int gate = literal_value(e.lit, m, vars);
      if (!switch_on(e.kind, gate, value)) {
        all_on = false;
        break;
      }
      const auto a = offset + static_cast<std::size_t>(p.nodes[j]);
      const auto b = offset + static_cast<std::size_t>(p.nodes[j + 1]);
      c.set(a, b);
      c.set(b, a);
    }
    if (all_on) {
      const auto a = offset + static_cast<std::size_t>(p.nodes[p.nodes.size() - 2]);
      const auto b = offset + static_cast<std::size_t>(p.nodes.back());
      c.set(a, b);
      c.set(b, a);
    }
  }
}

BoolMatrix power_closure(const BoolMatrix& c, std::size_t n, int& steps) {
  BoolMatrix reach = BoolMatrix::identity(n);
  BoolMatrix power = c;
  for (std::size_t k = 1; k < n; ++k) {
    for (std::size_t i = 0; i < n; ++i) reach.row(i) |= power.row(i);
    power = power * c;
    ++steps;
  }
  return reach;
}

DriverRow extract_row(const Graph& g, const BoolMatrix& reach,
                      std::size_t offset, std::uint32_t m,
                      const std::vector<std::string>& vars,
                      const std::vector<int>& target, bool& valid,
                      bool& all_single, int& max_drivers) {
  DriverRow row;
  std::map<std::string, bool> strong_for;
  for (const auto& p : g.paths) {
    const int value = source_value(p.source, m, vars);
    bool all_on = true;
    for (const auto& e : p.elems) {
      if (!switch_on(e.kind, literal_value(e.lit, m, vars), value)) {
        all_on = false;
        break;
      }
    }
    if (all_on && p.strong.size() > m && p.strong[m])
      strong_for[p.source] = true;
  }
  for (int s : g.source_ids) {
    if (!reach.test(offset + static_cast<std::size_t>(s),
                    offset + static_cast<std::size_t>(g.y)))
      continue;
    const std::string& name = g.names[static_cast<std::size_t>(s)];
    row.source_ids.push_back(s);
    row.sources.push_back(name);
    row.values.push_back(source_value(name, m, vars));
    row.strong.push_back(strong_for.count(name) != 0);
  }
  max_drivers = std::max(max_drivers, static_cast<int>(row.sources.size()));
  if (row.sources.size() != 1) {
    all_single = false;
    valid = false;
  } else if (row.values[0] != target[m] || !row.strong[0]) {
    valid = false;
  }
  return row;
}

}  // namespace

DriverProfile analyze_design_drivers_simple(
    const Design& d, const std::vector<std::string>& vars,
    const std::vector<int>& target) {
  DriverProfile out;
  out.simple = true;
  if (d.branches.empty() || vars.empty()) {
    out.valid = false;
    return out;
  }
  const std::uint32_t rows = std::uint32_t{1} << vars.size();
  if (target.size() != rows) {
    out.valid = false;
    return out;
  }
  out.rows.resize(rows);
  out.driver_count.resize(rows, 0);
  for (std::uint32_t m = 0; m < rows; ++m) {
    std::map<std::string, bool> strong;
    std::set<std::string> sources;
    for (const auto& b : d.branches) {
      if (!b.behavior.at(m).conducts) continue;
      sources.insert(b.source);
      strong[b.source] = strong[b.source] || b.behavior.at(m).strong;
    }
    DriverRow row;
    for (const auto& s : sources) {
      row.sources.push_back(s);
      row.values.push_back(source_value(s, m, vars));
      row.strong.push_back(strong[s]);
    }
    out.max_drivers = std::max(out.max_drivers, static_cast<int>(row.sources.size()));
    if (row.sources.size() != 1) {
      out.all_single = false;
      out.valid = false;
    } else if (row.values[0] != target[m] || !row.strong[0]) {
      out.valid = false;
    }
    out.driver_count[m] = static_cast<int>(row.sources.size());
    out.rows[m] = std::move(row);
  }
  return out;
}

DriverProfile analyze_design_drivers(const Design& d,
                                    const std::vector<std::string>& vars,
                                    const std::vector<int>& target,
                                    bool force_matrix) {
  if (!force_matrix) return analyze_design_drivers_simple(d, vars, target);
  DriverProfile out;
  if (d.branches.empty() || vars.empty()) {
    out.valid = false;
    return out;
  }
  const std::uint32_t rows = std::uint32_t{1} << vars.size();
  if (target.size() != rows) {
    out.valid = false;
    return out;
  }
  Graph g = build_graph(d);
  out.rows.resize(rows);
  out.driver_count.resize(rows, 0);
  for (std::uint32_t m = 0; m < rows; ++m) {
    BoolMatrix c(g.names.size());
    fill_matrix(c, g, m, vars, 0);
    out.power_steps += static_cast<int>(g.names.size());
    BoolMatrix reach = power_closure(c, g.names.size(), out.power_steps);
    out.rows[m] = extract_row(g, reach, 0, m, vars, target, out.valid,
                              out.all_single, out.max_drivers);
    out.driver_count[m] = static_cast<int>(out.rows[m].sources.size());
  }
  return out;
}

// Tensor/batched path closure.  For each minterm all candidate graphs are
// placed on one block diagonal; one Boolean closure handles the whole batch.
// Blocks are independent, so cross-candidate paths cannot leak.
std::vector<DriverProfile> analyze_designs_drivers_batch(
    const std::vector<Design>& designs, const std::vector<std::string>& vars,
    const std::vector<std::vector<int>>& targets, bool force_matrix) {
  std::vector<DriverProfile> out(designs.size());
  if (designs.empty() || vars.empty()) return out;
  const std::uint32_t rows = std::uint32_t{1} << vars.size();
  if (!force_matrix) {
    for (std::size_t i = 0; i < designs.size(); ++i) {
      const auto& target = targets.size() == 1 ? targets[0] : targets.at(i);
      out[i] = analyze_design_drivers_simple(designs[i], vars, target);
    }
    return out;
  }
  std::vector<Graph> graphs;
  graphs.reserve(designs.size());
  std::vector<std::size_t> offsets;
  std::size_t total = 0;
  for (const auto& d : designs) {
    graphs.push_back(build_graph(d));
    offsets.push_back(total);
    total += graphs.back().names.size();
  }
  for (auto& p : out) {
    p.rows.resize(rows);
    p.driver_count.resize(rows, 0);
  }
  for (std::uint32_t m = 0; m < rows; ++m) {
    BoolMatrix c(total);
    for (std::size_t k = 0; k < designs.size(); ++k)
      fill_matrix(c, graphs[k], m, vars, offsets[k]);
    int steps = 0;
    BoolMatrix reach = power_closure(c, total, steps);
    for (std::size_t k = 0; k < designs.size(); ++k) {
      const auto& target = targets.size() == 1 ? targets[0] : targets.at(k);
      out[k].rows[m] = extract_row(graphs[k], reach, offsets[k], m, vars,
                                   target, out[k].valid, out[k].all_single,
                                   out[k].max_drivers);
      out[k].driver_count[m] =
          static_cast<int>(out[k].rows[m].sources.size());
      out[k].power_steps += steps;
    }
  }
  return out;
}

}  // namespace dcstudy
