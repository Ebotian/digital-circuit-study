#include "dcstudy/z3_solvers.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <z3++.h>

namespace dcstudy {
namespace {

z3::expr sum_expr(z3::context& ctx, const std::vector<z3::expr>& terms) {
  z3::expr acc = ctx.int_val(0);
  for (const auto& t : terms) acc = acc + t;
  return acc;
}

z3::check_result checked(z3::solver& s) {
  const auto r = s.check();
  if (r == z3::unknown) throw std::runtime_error("Z3 returned unknown");
  return r;
}

z3::check_result checked(z3::optimize& o) {
  const auto r = o.check();
  if (r == z3::unknown) throw std::runtime_error("Z3 returned unknown");
  return r;
}

// Exactly one source net may drive the output per input row.  Branches from
// the same source are one driver (notably the two halves of a TG), while two
// different sources are a structural conflict even if they happen to carry
// the same value.
std::vector<z3::expr> driver_constraints(
    z3::context& ctx, const SynthProblem& p,
    const std::vector<z3::expr>& x) {
  std::vector<z3::expr> out;
  if (p.conducts.empty()) return out;
  if (p.conducts.size() != static_cast<std::size_t>(p.n_rows))
    throw std::runtime_error("synth: conducts size mismatch");
  if (p.groups.size() != x.size())
    throw std::runtime_error("synth: groups size mismatch");
  for (int m = 0; m < p.n_rows; ++m) {
    std::map<int, std::vector<int>> by_group;
    for (int id : p.conducts[static_cast<std::size_t>(m)]) {
      if (id < 0 || static_cast<std::size_t>(id) >= x.size())
        throw std::runtime_error("synth: conducts index out of range");
      const int g = p.groups[static_cast<std::size_t>(id)];
      if (g < 0) throw std::runtime_error("synth: branch has no source group");
      by_group[g].push_back(id);
    }
    std::vector<z3::expr> ys;
    for (const auto& [g, ids] : by_group) {
      z3::expr y = ctx.int_const(("y_" + std::to_string(m) + "_" +
                                  std::to_string(g)).c_str());
      out.push_back(y >= 0);
      out.push_back(y <= 1);
      std::vector<z3::expr> terms;
      for (int id : ids) {
        terms.push_back(x[static_cast<std::size_t>(id)]);
        out.push_back(y >= x[static_cast<std::size_t>(id)]);
      }
      out.push_back(y <= sum_expr(ctx, terms));
      ys.push_back(std::move(y));
    }
    if (ys.empty())
      throw std::runtime_error("synth: no conducting source for a row");
    out.push_back(sum_expr(ctx, ys) == 1);
  }
  return out;
}

}  // namespace

SynthSolution z3_solve_synth(const SynthProblem& p) {
  const std::size_t nb = p.costs.size();
  if (p.req.size() != nb ||
      p.covers.size() != static_cast<std::size_t>(p.n_rows))
    throw std::runtime_error("synth: size mismatch");
  if (p.n_rows < 1 || p.limit < 1 || p.max_cost < 0 || p.n_inverters < 0)
    throw std::runtime_error("synth: bad dimensions");

  SynthSolution out;
  if (nb == 0) {
    out.message = "no usable branch exists";
    return out;
  }

  std::vector<std::size_t> inv_bits;
  for (int j = 0; j < p.n_inverters; ++j) {
    const std::uint32_t bit = std::uint32_t{1} << j;
    for (auto req : p.req)
      if (req & bit) {
        inv_bits.push_back(static_cast<std::size_t>(j));
        break;
      }
  }

  z3::context ctx;
  z3::optimize optz(ctx);
  std::vector<z3::expr> x;
  x.reserve(nb);
  for (std::size_t i = 0; i < nb; ++i)
    x.push_back(ctx.int_const(("x_" + std::to_string(i)).c_str()));
  std::vector<z3::expr> inv;
  inv.reserve(inv_bits.size());
  for (std::size_t j = 0; j < inv_bits.size(); ++j)
    inv.push_back(ctx.int_const(("inv_" + std::to_string(j)).c_str()));

  for (auto& v : x) {
    optz.add(v >= 0);
    optz.add(v <= 1);
  }
  for (auto& v : inv) {
    optz.add(v >= 0);
    optz.add(v <= 1);
  }

  for (int m = 0; m < p.n_rows; ++m) {
    std::vector<z3::expr> terms;
    for (int id : p.covers[m]) {
      if (id < 0 || static_cast<std::size_t>(id) >= nb)
        throw std::runtime_error("synth: cover index out of range");
      terms.push_back(x[static_cast<std::size_t>(id)]);
    }
    if (terms.empty()) {
      out.message = "no branch drives minterm " + std::to_string(m) +
                    " correctly and strongly";
      return out;
    }
    optz.add(sum_expr(ctx, terms) >= 1);
  }
  for (const auto& c : driver_constraints(ctx, p, x)) optz.add(c);

  for (std::size_t i = 0; i < nb; ++i) {
    for (std::size_t j = 0; j < inv_bits.size(); ++j)
      if (p.req[i] & (std::uint32_t{1} << inv_bits[j]))
        optz.add(x[i] <= inv[j]);
  }
  for (std::size_t j = 0; j < inv_bits.size(); ++j) {
    std::vector<z3::expr> terms;
    for (std::size_t i = 0; i < nb; ++i)
      if (p.req[i] & (std::uint32_t{1} << inv_bits[j])) terms.push_back(x[i]);
    optz.add(inv[j] <= sum_expr(ctx, terms));
  }

  std::vector<z3::expr> cost_terms;
  for (std::size_t i = 0; i < nb; ++i)
    cost_terms.push_back(ctx.int_val(p.costs[i]) * x[i]);
  for (std::size_t j = 0; j < inv.size(); ++j)
    cost_terms.push_back(ctx.int_val(2) * inv[j]);
  z3::expr cost = sum_expr(ctx, cost_terms);
  optz.add(cost <= p.max_cost);
  optz.minimize(cost);

  if (checked(optz) == z3::unsat) {
    out.message = "no design fits --max-cost " + std::to_string(p.max_cost);
    return out;
  }
  const z3::model model = optz.get_model();
  out.optimum = static_cast<int>(model.eval(cost, true).get_numeral_int64());

  z3::solver s(ctx);
  for (auto& v : x) {
    s.add(v >= 0);
    s.add(v <= 1);
  }
  for (auto& v : inv) {
    s.add(v >= 0);
    s.add(v <= 1);
  }
  for (int m = 0; m < p.n_rows; ++m) {
    std::vector<z3::expr> terms;
    for (int id : p.covers[m]) terms.push_back(x[static_cast<std::size_t>(id)]);
    s.add(sum_expr(ctx, terms) >= 1);
  }
  for (const auto& c : driver_constraints(ctx, p, x)) s.add(c);
  for (std::size_t i = 0; i < nb; ++i)
    for (std::size_t j = 0; j < inv_bits.size(); ++j)
      if (p.req[i] & (std::uint32_t{1} << inv_bits[j])) s.add(x[i] <= inv[j]);
  for (std::size_t j = 0; j < inv_bits.size(); ++j) {
    std::vector<z3::expr> terms;
    for (std::size_t i = 0; i < nb; ++i)
      if (p.req[i] & (std::uint32_t{1} << inv_bits[j])) terms.push_back(x[i]);
    s.add(inv[j] <= sum_expr(ctx, terms));
  }
  s.add(cost <= out.optimum);

  while (static_cast<int>(out.designs.size()) < p.limit) {
    if (checked(s) != z3::sat) break;
    const z3::model m = s.get_model();
    std::vector<int> selected;
    std::vector<z3::expr> block;
    for (std::size_t i = 0; i < nb; ++i) {
      const int v = static_cast<int>(m.eval(x[i], true).get_numeral_int64());
      if (v == 1) {
        selected.push_back(static_cast<int>(i));
        block.push_back(x[i]);
      } else if (v != 0) {
        throw std::runtime_error("synth: non-integral branch variable");
      }
    }
    if (selected.empty()) break;
    out.designs.push_back(selected);

    auto block_set = [&](std::vector<int> set) {
      std::sort(set.begin(), set.end());
      set.erase(std::unique(set.begin(), set.end()), set.end());
      if (set.empty()) return;
      std::vector<z3::expr> terms;
      for (int id : set) {
        if (id < 0 || static_cast<std::size_t>(id) >= nb)
          throw std::runtime_error("synth: symmetry image out of range");
        terms.push_back(x[static_cast<std::size_t>(id)]);
      }
      s.add(sum_expr(ctx, terms) <= static_cast<int>(terms.size()) - 1);
    };
    block_set(selected);
    // Block the whole automorphism orbit.  This is symmetry breaking in the
    // solver, not an output filter: the next model is already a representative
    // of a different equivalence class.
    for (const auto& sym : p.symmetries) {
      std::vector<int> image;
      image.reserve(selected.size());
      for (int id : selected) {
        if (id < 0 || static_cast<std::size_t>(id) >= sym.size())
          throw std::runtime_error("synth: bad symmetry permutation");
        image.push_back(sym[static_cast<std::size_t>(id)]);
      }
      if (image == selected) continue;
      block_set(std::move(image));
    }
  }

  out.feasible = !out.designs.empty();
  if (!out.feasible) out.message = "no design found";
  return out;
}

}  // namespace dcstudy
