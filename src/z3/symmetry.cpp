#include "z3/symmetry.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include <bliss/graph.hh>

namespace dcstudy {

std::vector<std::vector<std::size_t>> bliss_automorphisms(
    std::size_t n, const std::string& truth) {
  const std::size_t rows = std::size_t{1} << n;
  if (truth.size() != rows)
    throw std::runtime_error("symmetry: truth table length does not match n");

  bliss::Graph g;
  for (std::size_t i = 0; i < n; ++i) g.add_vertex(0);
  for (std::size_t m = 0; m < rows; ++m)
    g.add_vertex(truth[m] == '1' ? 2 : 1);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t m = 0; m < rows; ++m) {
      const std::size_t p = n - 1 - i;
      if ((m >> p) & 1U) g.add_edge(i, n + m);
    }
  }

  std::vector<std::vector<std::size_t>> generators;
  bliss::Stats stats;
  g.find_automorphisms(
      stats, [&](unsigned int total, const unsigned int* aut) {
        std::vector<std::size_t> perm(n);
        for (std::size_t i = 0; i < n; ++i) {
          if (aut[i] >= total) return;
          perm[i] = aut[i];
        }
        if (std::find(generators.begin(), generators.end(), perm) ==
            generators.end())
          generators.push_back(std::move(perm));
      });

  // bliss reports generators.  Close them into the full group (small for the
  // supported n <= 4) so callers get every symmetry class representative.
  std::vector<std::size_t> identity(n);
  for (std::size_t i = 0; i < n; ++i) identity[i] = i;
  std::vector<std::vector<std::size_t>> group{identity};
  for (std::size_t cursor = 0; cursor < group.size(); ++cursor) {
    const auto base = group[cursor];
    for (const auto& gen : generators) {
      std::vector<std::size_t> composed(n);
      for (std::size_t i = 0; i < n; ++i) composed[i] = gen[base[i]];
      if (std::find(group.begin(), group.end(), composed) == group.end()) {
        if (group.size() >= 4096)
          throw std::runtime_error("symmetry: automorphism group too large");
        group.push_back(std::move(composed));
      }
    }
  }
  std::sort(group.begin(), group.end());
  return group;
}

}  // namespace dcstudy
