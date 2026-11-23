// Square Boolean matrices over the semiring ({0,1}, OR, AND).
// Multiplication and Warshall closure are used for switch-level reachability
// and for the cover relation in exact SOP minimisation.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "core/bits.hpp"

namespace dcstudy {

class BoolMatrix {
public:
  BoolMatrix() = default;
  explicit BoolMatrix(std::size_t n) : n_(n), rows_(n, Bits(n)) {}

  std::size_t size() const { return n_; }

  Bits &row(std::size_t i) { return rows_.at(i); }
  const Bits &row(std::size_t i) const { return rows_.at(i); }

  void set(std::size_t i, std::size_t j, bool v = true) {
    if (v)
      rows_.at(i).set(j);
    else
      rows_.at(i).reset(j);
  }
  bool test(std::size_t i, std::size_t j) const { return rows_.at(i).test(j); }

  static BoolMatrix identity(std::size_t n) {
    BoolMatrix m(n);
    for (std::size_t i = 0; i < n; ++i)
      m.set(i, i);
    return m;
  }

  // Undirected relation (both directions set).
  static BoolMatrix
  undirected(std::size_t n,
             const std::vector<std::pair<std::size_t, std::size_t>> &edges);

  // Boolean semiring product C[i][j] = OR_k A[i][k] AND B[k][j].
  BoolMatrix operator*(const BoolMatrix &rhs) const;

  // In-place reflexive-transitive closure (Warshall over bitsets).
  void closure(bool reflexive = true);

  BoolMatrix reachability(bool reflexive = true) const {
    BoolMatrix m = *this;
    m.closure(reflexive);
    return m;
  }

private:
  std::size_t n_ = 0;
  std::vector<Bits> rows_;
};

inline BoolMatrix BoolMatrix::undirected(
    std::size_t n,
    const std::vector<std::pair<std::size_t, std::size_t>> &edges) {
  BoolMatrix m(n);
  for (auto [a, b] : edges) {
    m.set(a, b);
    m.set(b, a);
  }
  return m;
}

inline BoolMatrix BoolMatrix::operator*(const BoolMatrix &rhs) const {
  BoolMatrix out(n_);
  for (std::size_t i = 0; i < n_; ++i) {
    Bits acc(n_);
    for (std::size_t j = 0; j < n_; ++j)
      if (rows_[i].test(j))
        acc |= rhs.rows_[j];
    out.rows_[i] = std::move(acc);
  }
  return out;
}

inline void BoolMatrix::closure(bool reflexive) {
  if (reflexive)
    for (std::size_t i = 0; i < n_; ++i)
      rows_[i].set(i);
  for (std::size_t k = 0; k < n_; ++k)
    for (std::size_t i = 0; i < n_; ++i)
      if (rows_[i].test(k))
        rows_[i] |= rows_[k];
}

} // namespace dcstudy
