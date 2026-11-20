// Mathematical kernels that live in the worker:
//   * GraphBLAS Boolean-semiring reachability closure
//   * FLINT GF(2) rank and nullspace
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace dcstudy {

struct Gf2Result {
  int rank = 0;
  int nullity = 0;
  int ncols = 0;
  std::vector<std::vector<int>> nullspace;  // one vector per basis column
};

std::vector<std::vector<int>> graphblas_closure(
    int n, const std::vector<std::pair<int, int>>& edges);

Gf2Result gf2_analysis(const std::vector<std::vector<int>>& rows, int ncols);

}  // namespace dcstudy
