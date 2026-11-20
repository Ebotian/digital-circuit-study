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
  std::vector<int> nodes;  // source, internal boundaries..., Y
  std::vector<ChainElem> elems;
  std::vector<bool> strong;
};

}  // namespace

DriverProfile analyze_design_drivers(const Design& d,
                                    const std::vector<std::string>& vars,
                                    const std::vector<int>& target) {
  (void)target;
  DriverProfile out;
  if (d.branches.empty() || vars.empty()) return out;

  std::vector<std::string> names;
  std::map<std::string, int> ids;
  auto node = [&](const std::string& n) {
    auto it = ids.find(n);
    if (it != ids.end()) return it->second;
    const int id = static_cast<int>(names.size());
    names.push_back(n);
    ids.emplace(n, id);
    return id;
  };
  const int y = node("Y");
  std::vector<Path> paths;
  std::set<std::string> source_names;
  for (const auto& b : d.branches) {
    source_names.insert(b.source);
    Path p;
    p.source = b.source;
    p.nodes.push_back(node(b.source));
    for (std::size_t j = 0; j < b.chain.size(); ++j) {
      p.nodes.push_back(node("n" + std::to_string(paths.size()) + "_" +
                             std::to_string(j)));
      p.elems.push_back(b.chain[j]);
    }
    p.nodes.push_back(y);
    p.strong.resize(b.behavior.size());
    for (std::size_t m = 0; m < b.behavior.size(); ++m)
      p.strong[m] = b.behavior[m].strong;
    paths.push_back(std::move(p));
  }
  std::vector<int> source_ids;
  for (const auto& s : source_names) source_ids.push_back(node(s));

  const std::uint32_t rows = std::uint32_t{1} << vars.size();
  out.rows.resize(rows);
  out.driver_count.resize(rows, 0);
  out.power_steps = static_cast<int>(names.size());

  for (std::uint32_t m = 0; m < rows; ++m) {
    BoolMatrix c(names.size());
    std::map<std::string, bool> strong_for;
    for (const auto& p : paths) {
      int value = source_value(p.source, m, vars);
      bool all_on = true;
      for (std::size_t j = 0; j < p.elems.size(); ++j) {
        const auto& e = p.elems[j];
        const int gate = literal_value(e.lit, m, vars);
        if (!switch_on(e.kind, gate, value)) {
          all_on = false;
          break;
        }
        c.set(static_cast<std::size_t>(p.nodes[j]),
              static_cast<std::size_t>(p.nodes[j + 1]));
        c.set(static_cast<std::size_t>(p.nodes[j + 1]),
              static_cast<std::size_t>(p.nodes[j]));
      }
      if (all_on) {
        const int last = p.nodes[p.nodes.size() - 2];
        c.set(static_cast<std::size_t>(last),
              static_cast<std::size_t>(p.nodes.back()));
        c.set(static_cast<std::size_t>(p.nodes.back()),
              static_cast<std::size_t>(last));
      }
      if (all_on && p.strong.size() > m && p.strong[m])
        strong_for[p.source] = true;
    }

    // C_m^* = I | C | C^2 | ... | C^(N-1), explicitly over the Boolean
    // semiring.  This is the matrix-power form of the dynamic driver rule.
    BoolMatrix reach = BoolMatrix::identity(names.size());
    BoolMatrix power = c;
    for (std::size_t k = 1; k < names.size(); ++k) {
      for (std::size_t i = 0; i < names.size(); ++i)
        reach.row(i) |= power.row(i);
      power = power * c;
      ++out.power_steps;
    }

    DriverRow row;
    for (int s : source_ids) {
      if (!reach.test(static_cast<std::size_t>(s), static_cast<std::size_t>(y)))
        continue;
      row.source_ids.push_back(s);
      row.sources.push_back(names[static_cast<std::size_t>(s)]);
      row.values.push_back(source_value(names[static_cast<std::size_t>(s)], m, vars));
      row.strong.push_back(strong_for.count(names[static_cast<std::size_t>(s)]) != 0);
    }
    out.driver_count[m] = static_cast<int>(row.sources.size());
    out.max_drivers = std::max(out.max_drivers, out.driver_count[m]);
    if (row.sources.size() != 1) out.all_single = false;
    out.rows[m] = std::move(row);
  }
  return out;
}

}  // namespace dcstudy
