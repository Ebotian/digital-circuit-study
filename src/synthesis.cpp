#include "dcstudy/synthesis.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "dcstudy/z3_client.hpp"
#include "dcstudy/z3_protocol.hpp"

namespace dcstudy {
namespace {

std::vector<std::size_t> automorphisms(const std::vector<std::string>& vars,
                                       const std::vector<int>& target) {
  const std::size_t n = vars.size();
  const std::uint32_t rows = std::uint32_t{1} << n;
  std::vector<std::size_t> perm(n);
  for (std::size_t i = 0; i < n; ++i) perm[i] = i;
  std::vector<std::size_t> good;
  do {
    bool ok = true;
    for (std::uint32_t m = 0; m < rows && ok; ++m) {
      std::uint32_t mapped = 0;
      for (std::size_t i = 0; i < n; ++i) {
        const std::size_t src_pos = n - 1 - i;
        const int bit = int((m >> src_pos) & 1U);
        mapped |= std::uint32_t(bit) << (n - 1 - perm[i]);
      }
      if (target[m] != target[mapped]) ok = false;
    }
    if (ok) {
      std::size_t code = 0;
      for (auto p : perm) code = code * 10 + p;
      good.push_back(code);
    }
  } while (std::next_permutation(perm.begin(), perm.end()));
  return good;
}

std::vector<std::size_t> decode_perm(std::size_t code, std::size_t n) {
  std::vector<std::size_t> perm(n);
  for (std::size_t i = n; i-- > 0;) {
    perm[i] = code % 10;
    code /= 10;
  }
  return perm;
}

std::string permute_lit(const std::string& lit,
                        const std::vector<std::size_t>& perm,
                        const std::vector<std::string>& vars) {
  bool inv = !lit.empty() && lit.back() == 'b';
  std::string base = inv ? lit.substr(0, lit.size() - 1) : lit;
  auto it = std::find(vars.begin(), vars.end(), base);
  if (it == vars.end()) return lit;
  std::size_t idx = std::size_t(it - vars.begin());
  return vars.at(perm.at(idx)) + (inv ? "b" : "");
}

std::string canonical_key(const Design& d, const std::vector<std::size_t>& perm,
                          const std::vector<std::string>& vars) {
  std::vector<std::string> parts;
  parts.reserve(d.branches.size());
  for (const auto& b : d.branches) {
    std::string s = permute_lit(b.source, perm, vars) + "|";
    std::vector<std::pair<int, std::string>> chain;
    for (const auto& el : b.chain)
      chain.emplace_back(static_cast<int>(el.kind), permute_lit(el.lit, perm, vars));
    std::sort(chain.begin(), chain.end());
    for (const auto& [kind, lit] : chain) {
      s += switch_name(static_cast<SwKind>(kind));
      s += "(";
      s += lit;
      s += ");";
    }
    parts.push_back(std::move(s));
  }
  std::sort(parts.begin(), parts.end());
  std::string out;
  for (const auto& p : parts) out += p + "/";
  return out;
}

Design design_from_indices(const std::vector<Branch>& branches,
                           const std::vector<std::size_t>& selected,
                           const std::vector<std::string>& vars) {
  Design d;
  d.branches.reserve(selected.size());
  std::uint32_t req = 0;
  for (auto i : selected) {
    d.branches.push_back(branches.at(i));
    d.cost += branches.at(i).cost;
    req |= branches.at(i).req_mask;
  }
  for (std::size_t i = 0; i < vars.size(); ++i)
    if (req & (std::uint32_t{1} << i)) d.inv_vars.push_back(i);
  d.cost += 2 * static_cast<int>(d.inv_vars.size());
  return d;
}

bool valid_design(const Design& d, const std::vector<int>& target, int optimum) {
  if (d.branches.empty() || d.cost != optimum) return false;
  for (std::size_t m = 0; m < target.size(); ++m) {
    bool ok = false;
    for (const auto& b : d.branches) {
      if (b.covers_strong(m, target[m])) {
        ok = true;
        break;
      }
    }
    if (!ok) return false;
  }
  return true;
}

}  // namespace

std::string Design::topologies(const std::vector<std::string>& vars) const {
  std::string out;
  for (const auto& b : branches) {
    if (!out.empty()) out += "; ";
    out += b.topo(vars);
  }
  return out;
}

std::string Design::inverter_names(const std::vector<std::string>& vars) const {
  std::string out;
  for (auto i : inv_vars) {
    if (!out.empty()) out += ",";
    out += vars.at(i);
  }
  return out.empty() ? "none" : out;
}

SynthesisResult synthesize_min(const std::vector<std::string>& vars,
                               const std::vector<int>& target,
                               const SpaceOptions& opt,
                               std::size_t max_solutions, bool canonical) {
  if (vars.empty() || vars.size() > 4)
    throw std::invalid_argument("synthesis supports 1..4 variables");
  if (max_solutions == 0) throw std::invalid_argument("limit must be positive");
  const std::uint32_t rows = std::uint32_t{1} << vars.size();
  if (target.size() != rows)
    throw std::invalid_argument("target width does not match variables");

  const auto branches = usable_branches(vars, target, opt);
  SynthesisResult res;
  res.branch_count = branches.size();
  if (branches.empty()) {
    res.message = "no usable branch exists in the requested space";
    return res;
  }

  SynthProblem problem;
  problem.n_rows = static_cast<int>(rows);
  problem.n_inverters = static_cast<int>(vars.size());
  problem.max_cost = opt.max_cost;
  // Canonicalisation happens here, so ask for a few extra raw optima.
  const std::size_t raw_limit =
      canonical ? std::min<std::size_t>(max_solutions, 64) * 8 : max_solutions;
  problem.limit = static_cast<int>(raw_limit);
  problem.covers.assign(rows, {});
  for (std::size_t i = 0; i < branches.size(); ++i) {
    problem.costs.push_back(branches[i].cost);
    problem.req.push_back(branches[i].req_mask);
    for (std::uint32_t m = 0; m < rows; ++m)
      if (branches[i].covers_strong(m, target[m]))
        problem.covers[m].push_back(static_cast<int>(i));
  }

  const SynthSolution sol =
      decode_synth_solution(z3_call(encode_synth_problem(problem)));
  res.optimum = sol.optimum;
  res.feasible = sol.feasible;
  if (!sol.message.empty()) res.message = sol.message;
  if (!sol.feasible) return res;

  const auto autos = automorphisms(vars, target);
  std::set<std::string> seen;
  for (const auto& ids : sol.designs) {
    std::vector<std::size_t> selected;
    selected.reserve(ids.size());
    for (int id : ids) {
      if (id < 0 || static_cast<std::size_t>(id) >= branches.size())
        throw std::runtime_error("Z3 helper returned an out-of-range branch id");
      selected.push_back(static_cast<std::size_t>(id));
    }
    std::sort(selected.begin(), selected.end());
    if (std::adjacent_find(selected.begin(), selected.end()) != selected.end())
      throw std::runtime_error("Z3 helper returned a duplicate branch id");
    Design d = design_from_indices(branches, selected, vars);
    if (!valid_design(d, target, sol.optimum)) continue;

    std::string key;
    if (canonical) {
      std::string best_key;
      for (auto code : autos) {
        const auto perm = decode_perm(code, vars.size());
        std::string k = canonical_key(d, perm, vars);
        if (best_key.empty() || k < best_key) best_key = std::move(k);
      }
      key = best_key;
      d.canonical = true;
    } else {
      std::vector<std::string> parts;
      for (const auto& b : d.branches) parts.push_back(b.topo(vars));
      std::sort(parts.begin(), parts.end());
      for (const auto& p : parts) key += p + "/";
    }
    if (seen.insert(key).second) {
      res.designs.push_back(std::move(d));
      if (res.designs.size() >= max_solutions) break;
    }
  }

  res.feasible = !res.designs.empty();
  if (!res.feasible) res.message = "no design found";
  return res;
}

}  // namespace dcstudy
