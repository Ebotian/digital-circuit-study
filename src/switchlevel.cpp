#include "dcstudy/switchlevel.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "dcstudy/boolmatrix.hpp"
#include "dcstudy/math_client.hpp"
#include "dcstudy/z3_client.hpp"
#include "dcstudy/z3_protocol.hpp"

namespace dcstudy {
namespace {

struct NetIndex {
  std::vector<std::string> names;
  std::unordered_map<std::string, std::size_t> index;

  std::size_t add(const std::string& n) {
    auto it = index.find(n);
    if (it != index.end()) return it->second;
    const std::size_t id = names.size();
    names.push_back(n);
    index.emplace(n, id);
    return id;
  }
  std::size_t get(const std::string& n) const {
    auto it = index.find(n);
    if (it == index.end()) throw std::runtime_error("unknown net: " + n);
    return it->second;
  }
  bool has(const std::string& n) const { return index.count(n) != 0; }
};

NetIndex build_index(const Subckt& cell) {
  NetIndex ni;
  for (const auto& p : cell.ports) ni.add(p);
  for (const auto& d : cell.devices) {
    ni.add(d.drain);
    ni.add(d.gate);
    ni.add(d.source);
  }
  return ni;
}

struct FixedPoint {
  std::vector<char> f0, f1;
  std::vector<std::string> floating_gates;
  bool conflict = false;
  bool invariant_ok = true;
};

FixedPoint run_fixpoint(const Subckt& cell, const NetIndex& ni,
                        const std::vector<std::size_t>& input_idx,
                        std::uint32_t minterm, std::size_t vdd, std::size_t vss,
                        bool graphblas) {
  const std::size_t n = ni.names.size();
  FixedPoint fp;
  fp.f0.assign(n, 0);
  fp.f1.assign(n, 0);
  fp.f1[vdd] = 1;
  fp.f0[vss] = 1;
  for (std::size_t i = 0; i < input_idx.size(); ++i) {
    const bool bit = ((minterm >> (input_idx.size() - 1 - i)) & 1U) != 0;
    (bit ? fp.f1 : fp.f0)[input_idx[i]] = 1;
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& d : cell.devices) {
      const std::size_t di = ni.get(d.drain);
      const std::size_t gi = ni.get(d.gate);
      const std::size_t si = ni.get(d.source);
      if (!d.pmos) {
        if (!fp.f1[gi]) continue;
        if (fp.f0[di] && !fp.f0[si]) {
          fp.f0[si] = 1;
          changed = true;
        }
        if (fp.f0[si] && !fp.f0[di]) {
          fp.f0[di] = 1;
          changed = true;
        }
      } else {
        if (!fp.f0[gi]) continue;
        if (fp.f1[di] && !fp.f1[si]) {
          fp.f1[si] = 1;
          changed = true;
        }
        if (fp.f1[si] && !fp.f1[di]) {
          fp.f1[di] = 1;
          changed = true;
        }
      }
    }
  }

  for (std::size_t i = 0; i < n; ++i)
    if (fp.f0[i] && fp.f1[i]) fp.conflict = true;

  // Connectivity closure over devices that are on in the fixed point.
  std::vector<std::pair<std::size_t, std::size_t>> edges;
  for (const auto& d : cell.devices) {
    const std::size_t di = ni.get(d.drain);
    const std::size_t gi = ni.get(d.gate);
    const std::size_t si = ni.get(d.source);
    const bool on = d.pmos ? (fp.f0[gi] && (fp.f1[di] || fp.f1[si]))
                           : (fp.f1[gi] && (fp.f0[di] || fp.f0[si]));
    if (on) edges.emplace_back(di, si);
  }
  std::vector<char> fixed(n, 0);
  fixed[vdd] = fixed[vss] = 1;
  for (auto i : input_idx) fixed[i] = 1;
  if (graphblas) {
    std::vector<std::pair<int, int>> ge;
    ge.reserve(edges.size());
    for (auto [u, v] : edges) {
      ge.emplace_back(static_cast<int>(u), static_cast<int>(v));
      ge.emplace_back(static_cast<int>(v), static_cast<int>(u));
    }
    const auto reach = graph_closure(static_cast<int>(n), ge);
    auto reaches = [&](std::size_t src, std::size_t dst) {
      const auto& row = reach.at(src);
      return std::find(row.begin(), row.end(), static_cast<int>(dst)) != row.end();
    };
    for (std::size_t i = 0; i < n; ++i) {
      if (fixed[i]) continue;
      if (fp.f0[i] && !reaches(vss, i)) fp.invariant_ok = false;
      if (fp.f1[i] && !reaches(vdd, i)) fp.invariant_ok = false;
    }
  } else {
    BoolMatrix m = BoolMatrix::undirected(n, edges);
    m.closure(true);
    for (std::size_t i = 0; i < n; ++i) {
      if (fixed[i]) continue;
      if (fp.f0[i] && !m.test(vss, i)) fp.invariant_ok = false;
      if (fp.f1[i] && !m.test(vdd, i)) fp.invariant_ok = false;
    }
  }

  for (const auto& d : cell.devices) {
    const std::size_t gi = ni.get(d.gate);
    if (!fp.f0[gi] && !fp.f1[gi]) {
      const bool is_input =
          std::find(input_idx.begin(), input_idx.end(), gi) != input_idx.end();
      if (!is_input && gi != vdd && gi != vss) fp.floating_gates.push_back(d.gate);
    }
  }
  std::sort(fp.floating_gates.begin(), fp.floating_gates.end());
  fp.floating_gates.erase(
      std::unique(fp.floating_gates.begin(), fp.floating_gates.end()),
      fp.floating_gates.end());

  if (fp.conflict) fp.invariant_ok = false;
  return fp;
}

}  // namespace

char logic_symbol(Logic v) {
  switch (v) {
    case Logic::Zero: return '0';
    case Logic::One: return '1';
    case Logic::Float: return 'Z';
    case Logic::Race: return 'X';
  }
  return '?';
}

SwitchAnalysis analyze_switch(const Subckt& cell,
                              const std::vector<std::string>& inputs,
                              const std::vector<std::string>& outputs,
                              bool exact, const std::string& vdd,
                              const std::string& vss, bool graphblas) {
  if (inputs.size() > 8)
    throw std::invalid_argument("switch-level analysis supports at most 8 inputs");
  if (outputs.empty()) throw std::invalid_argument("cell has no output port");

  const NetIndex ni = build_index(cell);
  if (!ni.has(vdd) || !ni.has(vss))
    throw std::invalid_argument("cell has no " + vdd + "/" + vss + " port");
  const std::size_t vdd_i = ni.get(vdd);
  const std::size_t vss_i = ni.get(vss);

  std::vector<std::size_t> input_idx;
  for (const auto& p : inputs) {
    if (!ni.has(p)) throw std::invalid_argument("unknown input port: " + p);
    input_idx.push_back(ni.get(p));
  }
  std::vector<std::size_t> out_idx;
  for (const auto& p : outputs) {
    if (!ni.has(p)) throw std::invalid_argument("unknown output port: " + p);
    out_idx.push_back(ni.get(p));
  }

  const std::uint32_t rows = std::uint32_t{1} << inputs.size();
  SwitchAnalysis res;
  res.cell = cell.name;
  res.inputs = inputs;
  res.outputs = outputs;
  res.mode = exact ? "z3" : "fixpoint";

  std::vector<std::uint32_t> minterms;
  std::vector<FixedPoint> fps;
  bool need_exact = exact;
  for (std::uint32_t m = 0; m < rows; ++m) {
    minterms.push_back(m);
    fps.push_back(run_fixpoint(cell, ni, input_idx, m, vdd_i, vss_i, graphblas));
    if (!fps.back().floating_gates.empty() || !fps.back().invariant_ok)
      need_exact = true;
  }

  if (need_exact) {
    SwitchProblem p;
    p.devices = cell.devices;
    p.vdd = vdd;
    p.vss = vss;
    p.inputs = inputs;
    p.outputs = outputs;
    p.rows = minterms;
    const SwitchSolution sol =
        decode_switch_solution(z3_call(encode_switch_problem(p)));
    if (sol.rows.size() != rows)
      throw std::runtime_error("Z3 helper returned the wrong number of rows");
    for (std::uint32_t m = 0; m < rows; ++m) {
      SwitchRow row;
      row.inputs.resize(inputs.size());
      for (std::size_t i = 0; i < inputs.size(); ++i)
        row.inputs[i] = int((m >> (inputs.size() - 1 - i)) & 1U);
      if (sol.rows[m].values.size() != outputs.size())
        throw std::runtime_error("Z3 helper returned the wrong number of outputs");
      row.values = sol.rows[m].values;
      row.floating_gates = fps[m].floating_gates;
      row.unsat = sol.rows[m].unsat;
      row.conflict = sol.rows[m].conflict || sol.rows[m].unsat;
      res.rows.push_back(std::move(row));
    }
    res.exact = true;
    res.mode = exact ? "z3" : "z3 (fixed-point fallback)";
    res.assumptions_ok = true;
    return res;
  }

  for (std::uint32_t m = 0; m < rows; ++m) {
    const auto& fp = fps[m];
    SwitchRow row;
    row.inputs.resize(inputs.size());
    for (std::size_t i = 0; i < inputs.size(); ++i)
      row.inputs[i] = int((m >> (inputs.size() - 1 - i)) & 1U);
    for (std::size_t oi = 0; oi < out_idx.size(); ++oi) {
      const auto o = out_idx[oi];
      if (fp.f0[o] && fp.f1[o])
        row.values.push_back(Logic::Race);
      else if (fp.f0[o])
        row.values.push_back(Logic::Zero);
      else if (fp.f1[o])
        row.values.push_back(Logic::One);
      else
        row.values.push_back(Logic::Float);
    }
    row.floating_gates = fp.floating_gates;
    row.conflict = fp.conflict;
    row.unsat = fp.conflict;
    res.rows.push_back(std::move(row));
  }
  res.assumptions_ok = true;
  return res;
}

}  // namespace dcstudy
