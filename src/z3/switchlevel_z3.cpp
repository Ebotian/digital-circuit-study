#include "z3/z3_solvers.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <z3++.h>

namespace dcstudy {
namespace {

struct NetIndex {
  std::vector<std::string> names;
  std::unordered_map<std::string, std::size_t> index;
  std::size_t add(const std::string &n) {
    auto it = index.find(n);
    if (it != index.end())
      return it->second;
    const std::size_t id = names.size();
    names.push_back(n);
    index.emplace(n, id);
    return id;
  }
  std::size_t get(const std::string &n) const {
    auto it = index.find(n);
    if (it == index.end())
      throw std::runtime_error("unknown net: " + n);
    return it->second;
  }
};

NetIndex build_index(const SwitchProblem &p) {
  NetIndex ni;
  ni.add(p.vdd);
  ni.add(p.vss);
  for (const auto &s : p.inputs)
    ni.add(s);
  for (const auto &s : p.outputs)
    ni.add(s);
  for (const auto &d : p.devices) {
    ni.add(d.drain);
    ni.add(d.gate);
    ni.add(d.source);
  }
  return ni;
}

z3::expr or_all(z3::context &ctx, const std::vector<z3::expr> &v) {
  z3::expr acc = ctx.bool_val(false);
  for (const auto &e : v)
    acc = acc || e;
  return acc;
}

z3::check_result checked(z3::solver &s) {
  const auto r = s.check();
  if (r == z3::unknown)
    throw std::runtime_error("Z3 returned unknown");
  return r;
}

} // namespace

SwitchSolution z3_solve_switch(const SwitchProblem &p) {
  if (p.inputs.empty() || p.outputs.empty() || p.rows.empty())
    throw std::runtime_error("switch: empty problem");
  const NetIndex ni = build_index(p);
  const std::size_t N = ni.names.size();
  const std::size_t vdd = ni.get(p.vdd);
  const std::size_t vss = ni.get(p.vss);
  std::vector<std::size_t> input_idx;
  input_idx.reserve(p.inputs.size());
  for (const auto &s : p.inputs)
    input_idx.push_back(ni.get(s));
  std::vector<std::size_t> out_idx;
  out_idx.reserve(p.outputs.size());
  for (const auto &s : p.outputs)
    out_idx.push_back(ni.get(s));

  z3::context ctx;
  z3::solver solver(ctx);
  std::vector<z3::expr> val;
  val.reserve(N);
  for (std::size_t i = 0; i < N; ++i)
    val.push_back(ctx.bool_const(("n_" + std::to_string(i)).c_str()));
  solver.add(val[vdd]);
  solver.add(!val[vss]);

  std::vector<z3::expr> on;
  on.reserve(p.devices.size());
  for (const auto &d : p.devices) {
    const auto di = ni.get(d.drain);
    const auto gi = ni.get(d.gate);
    const auto si = ni.get(d.source);
    if (!d.pmos)
      on.push_back(val[gi] && (!val[di] || !val[si]));
    else
      on.push_back(!val[gi] && (val[di] || val[si]));
  }

  std::vector<std::vector<z3::expr>> edge(
      N, std::vector<z3::expr>(N, ctx.bool_val(false)));
  for (std::size_t u = 0; u < N; ++u) {
    for (std::size_t v = 0; v < N; ++v) {
      std::vector<z3::expr> terms;
      for (std::size_t k = 0; k < p.devices.size(); ++k) {
        const auto di = ni.get(p.devices[k].drain);
        const auto si = ni.get(p.devices[k].source);
        if ((di == u && si == v) || (di == v && si == u))
          terms.push_back(on[k]);
      }
      edge[u][v] = or_all(ctx, terms);
    }
  }

  auto reach = [&](std::size_t source) {
    std::vector<std::vector<z3::expr>> r(
        N + 1, std::vector<z3::expr>(N, ctx.bool_val(false)));
    r[0][source] = ctx.bool_val(true);
    for (std::size_t k = 0; k < N; ++k) {
      for (std::size_t v = 0; v < N; ++v) {
        std::vector<z3::expr> terms;
        terms.push_back(r[k][v]);
        for (std::size_t u = 0; u < N; ++u)
          terms.push_back(r[k][u] && edge[u][v]);
        r[k + 1][v] = or_all(ctx, terms);
      }
    }
    return r[N];
  };
  const auto r0 = reach(vss);
  const auto r1 = reach(vdd);

  // Value rules are added per row.  When the fixed point gives reliable
  // forcing data, fix those nets: the only remaining freedom is the stored
  // charge on gates it could not force, so "can both rails reach this output?"
  // is a single query rather than an enumeration of charge combinations.
  // Rows without forcing data keep the old "a net is never on both rails"
  // rule (no contention detection there).
  SwitchSolution out;
  out.rows.reserve(p.rows.size());
  for (std::size_t row_index = 0; row_index < p.rows.size(); ++row_index) {
    const std::uint32_t minterm = p.rows[row_index];
    solver.push();
    for (std::size_t i = 0; i < input_idx.size(); ++i) {
      const bool bit = ((minterm >> (input_idx.size() - 1 - i)) & 1U) != 0;
      solver.add(bit ? val[input_idx[i]] : !val[input_idx[i]]);
    }
    const bool have_forcing =
        row_index < p.forced0.size() &&
        !(p.forced0[row_index].empty() && p.forced1[row_index].empty());
    if (have_forcing) {
      for (const auto &n : p.forced0[row_index])
        solver.add(!val[ni.get(n)]);
      for (const auto &n : p.forced1[row_index])
        solver.add(val[ni.get(n)]);
      for (std::size_t v = 0; v < N; ++v) {
        solver.add(z3::implies(r0[v] && !r1[v], !val[v]));
        solver.add(z3::implies(r1[v] && !r0[v], val[v]));
      }
    } else {
      for (std::size_t v = 0; v < N; ++v) {
        solver.add(z3::implies(r0[v], !val[v]));
        solver.add(z3::implies(r1[v], val[v]));
        solver.add(!(r0[v] && r1[v]));
      }
    }
    SwitchSolutionRow row;
    row.values.assign(out_idx.size(), Logic::Float);
    if (checked(solver) != z3::sat) {
      row.values.assign(out_idx.size(), Logic::Race);
      row.conflict = true;
      row.unsat = true;
      out.rows.push_back(std::move(row));
      solver.pop();
      continue;
    }
    for (std::size_t oi = 0; oi < out_idx.size(); ++oi) {
      const std::size_t y = out_idx[oi];
      if (have_forcing) {
        // A stored gate can enable both rails at once: contention.
        solver.push();
        solver.add(r0[y] && r1[y]);
        const bool race = (checked(solver) == z3::sat);
        solver.pop();
        if (race) {
          row.values[oi] = Logic::Race;
          row.conflict = true;
          continue;
        }
      }
      // Definite 0 / 1: exactly one rail reaches the output.
      solver.push();
      solver.add(r0[y] && !r1[y]);
      const bool can0 = (checked(solver) == z3::sat);
      solver.pop();

      solver.push();
      solver.add(r1[y] && !r0[y]);
      const bool can1 = (checked(solver) == z3::sat);
      solver.pop();

      // Neither rail: the output is undriven (Z).  When that can happen
      // alongside a driven state, the driven value is not guaranteed.
      solver.push();
      solver.add(!r0[y] && !r1[y]);
      const bool can_float = (checked(solver) == z3::sat);
      solver.pop();

      if (can0 && can1)
        row.values[oi] = Logic::Race;
      else if ((can0 || can1) && can_float)
        row.values[oi] = Logic::Float; // NOLINT(bugprone-branch-clone)
      else if (can0)
        row.values[oi] = Logic::Zero;
      else if (can1)
        row.values[oi] = Logic::One;
      else
        row.values[oi] = Logic::Float; // NOLINT(bugprone-branch-clone)
    }
    out.rows.push_back(std::move(row));
    solver.pop();
  }
  return out;
}

} // namespace dcstudy
