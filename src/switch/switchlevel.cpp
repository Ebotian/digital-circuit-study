#include "switch/switchlevel.hpp"

#include "core/parallel.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/boolmatrix.hpp"
#include "cli/math_client.hpp"
#include "cli/z3_client.hpp"
#include "z3/z3_protocol.hpp"

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

Subckt switch_output_cone(const Subckt& cell,
                          const std::vector<std::string>& inputs,
                          const std::string& output,
                          const std::string& vdd,
                          const std::string& vss) {
  Subckt out;
  out.name = cell.name + "_" + output;
  for (const auto& v : inputs) {
    out.ports.push_back(v);
    out.pin_dir[v] = PinDir::Input;
  }
  out.ports.push_back(output);
  out.pin_dir[output] = PinDir::Output;
  out.ports.push_back(vdd);
  out.ports.push_back(vss);
  out.pin_dir[vdd] = PinDir::Power;
  out.pin_dir[vss] = PinDir::Ground;

  const auto is_input = [&](const std::string& n) {
    return std::find(inputs.begin(), inputs.end(), n) != inputs.end();
  };
  std::set<std::size_t> keep;
  std::set<std::string> seen;
  std::vector<std::string> work{output};
  while (!work.empty()) {
    const std::string n = std::move(work.back());
    work.pop_back();
    if (n == vdd || n == vss || is_input(n) || !seen.insert(n).second)
      continue;
    for (std::size_t i = 0; i < cell.devices.size(); ++i) {
      const Device& d = cell.devices[i];
      if (d.drain != n && d.source != n) continue;
      if (!keep.insert(i).second) continue;
      if (d.gate != vdd && d.gate != vss && !is_input(d.gate))
        work.push_back(d.gate);
      const std::string& other = (d.drain == n) ? d.source : d.drain;
      if (other != vdd && other != vss && !is_input(other))
        work.push_back(other);
    }
  }
  for (std::size_t i : keep) out.devices.push_back(cell.devices[i]);
  return out;
}

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
                              const std::string& vss, bool graphblas,
                              int jobs) {
  if (inputs.size() > 8)
    throw std::invalid_argument("switch-level analysis supports at most 8 inputs");
  if (outputs.empty()) throw std::invalid_argument("cell has no output port");

  const NetIndex ni = build_index(cell);
  if (!ni.has(vdd) || !ni.has(vss))
    throw std::invalid_argument("cell has no " + vdd + "/" + vss + " port");
  const std::size_t vdd_i = ni.get(vdd);
  const std::size_t vss_i = ni.get(vss);

  // A port may be declared as an alias of another net (two outputs defined
  // by the same expression).  Resolve it to the canonical net; the reported
  // inputs/outputs keep the requested names.
  const auto net_of = [&](const std::string& p) -> const std::string& {
    auto it = cell.aliases.find(p);
    return it == cell.aliases.end() ? p : it->second;
  };

  std::vector<std::size_t> input_idx;
  for (const auto& p : inputs) {
    if (!ni.has(net_of(p)))
      throw std::invalid_argument("unknown input port: " + p);
    input_idx.push_back(ni.get(net_of(p)));
  }
  std::vector<std::size_t> out_idx;
  for (const auto& p : outputs) {
    if (!ni.has(net_of(p)))
      throw std::invalid_argument("unknown output port: " + p);
    out_idx.push_back(ni.get(net_of(p)));
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
    SwitchProblem base;
    base.devices = cell.devices;
    base.vdd = vdd;
    base.vss = vss;
    base.inputs.clear();
    for (const auto& p : inputs) base.inputs.push_back(net_of(p));
    base.outputs.clear();
    for (const auto& p : outputs) base.outputs.push_back(net_of(p));
    base.rows = minterms;

    // Split the independent (row, output) checks across Z3 workers.  A single
    // worker checks an entire row/output matrix; for a seven-segment display
    // that is hundreds of solver queries and one core.  Tiling both dimensions
    // keeps each worker's formula small and uses all available cores.
    const std::size_t max_tasks = std::min<std::size_t>(
        static_cast<std::size_t>(std::max(1, jobs > 0 ? jobs : parallel_jobs())),
        rows * outputs.size());
    std::size_t out_groups = 1;
    std::size_t row_groups = 1;
    std::size_t best_tasks = 1;
    if (max_tasks > 1) {
      for (std::size_t og = std::min(outputs.size(), max_tasks); og >= 1; --og) {
        const std::size_t rg = std::min<std::size_t>(rows, max_tasks / og);
        if (rg == 0) continue;
        const std::size_t tasks = og * rg;
        if (tasks > best_tasks ||
            (tasks == best_tasks && og > out_groups)) {
          best_tasks = tasks;
          out_groups = og;
          row_groups = rg;
        }
      }
    }

    const auto split_range = [](std::size_t n, std::size_t parts) {
      std::vector<std::pair<std::size_t, std::size_t>> out;
      out.reserve(parts);
      const std::size_t base = n / parts;
      const std::size_t rem = n % parts;
      std::size_t at = 0;
      for (std::size_t i = 0; i < parts; ++i) {
        const std::size_t len = base + (i < rem ? 1 : 0);
        out.emplace_back(at, at + len);
        at += len;
      }
      return out;
    };
    const auto out_parts = split_range(outputs.size(), out_groups);
    const auto row_parts = split_range(rows, row_groups);
    const std::size_t tasks = out_groups * row_groups;
    std::vector<SwitchSolution> sols(tasks);
    parallel_for(tasks, static_cast<int>(tasks), [&](std::size_t ti) {
      const std::size_t oi = ti % out_groups;
      const std::size_t ri = ti / out_groups;
      SwitchProblem p = base;
      p.outputs.clear();
      p.rows.clear();
      for (std::size_t j = out_parts[oi].first; j < out_parts[oi].second; ++j)
        p.outputs.push_back(base.outputs[j]);
      for (std::size_t j = row_parts[ri].first; j < row_parts[ri].second; ++j) {
        p.rows.push_back(minterms[j]);
        // Hand the fixed point's forced values to the solver: fixing them
        // leaves only genuinely stored (floating) gates free, so a possible
        // contention can be found with one query instead of enumerating.
        const FixedPoint& fp = fps[minterms[j]];
        std::vector<std::string> f0, f1;
        // Values forced through an input (not a rail) make invariant_ok
        // false, but they are still forced; only a conflict is unusable.
        if (!fp.conflict) {
          for (std::size_t v = 0; v < fp.f0.size(); ++v) {
            if (fp.f0[v]) f0.push_back(ni.names[v]);
            if (fp.f1[v]) f1.push_back(ni.names[v]);
          }
        }
        p.forced0.push_back(std::move(f0));
        p.forced1.push_back(std::move(f1));
      }
      sols[ti] = decode_switch_solution(z3_call(encode_switch_problem(p)));
    });

    std::vector<SwitchRow> merged(rows);
    for (std::uint32_t m = 0; m < rows; ++m) {
      merged[m].inputs.resize(inputs.size());
      for (std::size_t i = 0; i < inputs.size(); ++i)
        merged[m].inputs[i] = int((m >> (inputs.size() - 1 - i)) & 1U);
      merged[m].values.assign(outputs.size(), Logic::Float);
      merged[m].floating_gates = fps[m].floating_gates;
    }
    for (std::size_t ti = 0; ti < tasks; ++ti) {
      const std::size_t oi = ti % out_groups;
      const std::size_t ri = ti / out_groups;
      const std::size_t rn = row_parts[ri].second - row_parts[ri].first;
      const std::size_t on = out_parts[oi].second - out_parts[oi].first;
      const SwitchSolution& sol = sols[ti];
      if (sol.rows.size() != rn)
        throw std::runtime_error("Z3 helper returned the wrong number of rows");
      for (std::size_t r = 0; r < rn; ++r) {
        const SwitchSolutionRow& src = sol.rows[r];
        if (src.values.size() != on)
          throw std::runtime_error(
              "Z3 helper returned the wrong number of outputs");
        SwitchRow& dst = merged[row_parts[ri].first + r];
        dst.unsat = dst.unsat || src.unsat;
        dst.conflict = dst.conflict || src.conflict || src.unsat;
        for (std::size_t j = 0; j < on; ++j)
          dst.values[out_parts[oi].first + j] = src.values[j];
      }
    }
    res.rows = std::move(merged);
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
