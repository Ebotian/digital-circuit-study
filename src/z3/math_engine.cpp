#include "z3/math_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <GraphBLAS.h>
#include <flint/nmod_mat.h>

namespace dcstudy {
namespace {

void check(GrB_Info info) {
  if (info != GrB_SUCCESS)
    throw std::runtime_error("GraphBLAS error " + std::to_string(info));
}

void init_graphblas() {
  static bool once = [] {
    check(GrB_init(GrB_NONBLOCKING));
    return true;
  }();
  (void)once;
}

} // namespace

std::vector<std::vector<int>>
graphblas_closure(int n, const std::vector<std::pair<int, int>> &edges) {
  if (n < 0)
    throw std::invalid_argument("closure: negative n");
  if (n == 0)
    return {};
  init_graphblas();

  GrB_Matrix A = nullptr;
  check(GrB_Matrix_new(&A, GrB_BOOL, n, n));
  for (int i = 0; i < n; ++i)
    check(GrB_Matrix_setElement_BOOL(A, true, i, i));
  for (auto [u, v] : edges) {
    if (u < 0 || v < 0 || u >= n || v >= n)
      throw std::invalid_argument("closure: edge out of range");
    check(GrB_Matrix_setElement_BOOL(A, true, u, v));
  }

  GrB_Matrix R = nullptr, P = nullptr, C = nullptr;
  check(GrB_Matrix_dup(&R, A));
  check(GrB_Matrix_dup(&P, A));
  check(GrB_Matrix_new(&C, GrB_BOOL, n, n));
  for (int step = 1; step < n; ++step) {
    // C = P * A over (OR, AND): paths one edge longer.
    check(GrB_mxm(C, nullptr, nullptr, GxB_LOR_LAND_BOOL, P, A, nullptr));
    // R = R OR C, without destroying R while it is an input.
    GrB_Matrix R2 = nullptr;
    check(GrB_Matrix_new(&R2, GrB_BOOL, n, n));
    check(GrB_Matrix_eWiseAdd_BinaryOp(R2, nullptr, nullptr, GxB_LOR_BOOL, R, C,
                                       nullptr));
    GrB_Matrix_free(&R);
    R = R2;
    GrB_Matrix_free(&P);
    P = C;
    C = nullptr;
    check(GrB_Matrix_new(&C, GrB_BOOL, n, n));
  }

  GrB_Index nvals = 0;
  check(GrB_Matrix_nvals(&nvals, R));
  std::vector<GrB_Index> rows(nvals), cols(nvals);
  std::unique_ptr<bool[]> vals(new bool[nvals]);
  if (nvals > 0)
    check(GrB_Matrix_extractTuples_BOOL(rows.data(), cols.data(), vals.get(),
                                        &nvals, R));
  std::vector<std::vector<int>> out(n);
  for (std::size_t i = 0; i < nvals; ++i)
    if (vals.get()[i])
      out[static_cast<std::size_t>(rows[i])].push_back(
          static_cast<int>(cols[i]));
  for (auto &row : out)
    std::sort(row.begin(), row.end());

  GrB_Matrix_free(&A);
  GrB_Matrix_free(&R);
  GrB_Matrix_free(&P);
  GrB_Matrix_free(&C);
  return out;
}

Gf2Result gf2_analysis(const std::vector<std::vector<int>> &rows, int ncols) {
  if (ncols < 0)
    throw std::invalid_argument("gf2: negative ncols");
  const int nrows = static_cast<int>(rows.size());
  nmod_mat_t A, X;
  nmod_mat_init(A, nrows, ncols, 2);
  nmod_mat_init(X, ncols, ncols, 2);
  for (int i = 0; i < nrows; ++i) {
    if (static_cast<int>(rows[i].size()) != ncols)
      throw std::invalid_argument("gf2: row width mismatch");
    for (int j = 0; j < ncols; ++j)
      nmod_mat_set_entry(A, i, j, rows[i][j] ? 1 : 0);
  }
  Gf2Result r;
  r.ncols = ncols;
  r.rank = static_cast<int>(nmod_mat_rank(A));
  r.nullity = static_cast<int>(nmod_mat_nullspace(X, A));
  for (int j = 0; j < r.nullity; ++j) {
    std::vector<int> v(ncols);
    for (int i = 0; i < ncols; ++i)
      v[i] = static_cast<int>(nmod_mat_get_entry(X, i, j));
    r.nullspace.push_back(std::move(v));
  }
  nmod_mat_clear(A);
  nmod_mat_clear(X);
  return r;
}

} // namespace dcstudy
