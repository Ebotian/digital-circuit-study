// Main-process client for the mathematical worker operations.
#pragma once

#include <utility>
#include <vector>

namespace dcstudy {

struct MathGf2 {
  int rank = 0;
  int nullity = 0;
  int ncols = 0;
  std::vector<std::vector<int>> nullspace;
};

std::vector<std::vector<int>> graph_closure(
    int n, const std::vector<std::pair<int, int>>& edges);

MathGf2 gf2_analysis(const std::vector<std::vector<int>>& rows, int ncols);

}  // namespace dcstudy
