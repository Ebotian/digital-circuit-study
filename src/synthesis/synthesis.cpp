#include "synthesis/synthesis.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/driver_profile.hpp"
#include "cli/z3_client.hpp"
#include "z3/z3_protocol.hpp"

namespace dcstudy {
namespace {

std::vector<std::vector<std::size_t>> enumerate_automorphisms(
    const std::vector<std::string>& vars, const std::vector<int>& target) {
  const std::size_t n = vars.size();
  const std::uint32_t rows = std::uint32_t{1} << n;
  std::vector<std::size_t> perm(n);
  for (std::size_t i = 0; i < n; ++i) perm[i] = i;
  std::vector<std::vector<std::size_t>> good;
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
    if (ok) good.push_back(perm);
  } while (std::next_permutation(perm.begin(), perm.end()));
  return good;
}

std::vector<std::vector<std::size_t>> automorphisms(
    const std::vector<std::string>& vars, const std::vector<int>& target) {
  try {
    Json req;
    req["op"] = "symmetry";
    req["n"] = static_cast<std::int64_t>(vars.size());
    std::string truth;
    for (int b : target) truth.push_back(b ? '1' : '0');
    req["truth"] = truth;
    const Json resp = z3_call(req);
    if (resp.contains("permutations")) {
      std::vector<std::vector<std::size_t>> out;
      for (const auto& pj : resp.at("permutations").array()) {
        std::vector<std::size_t> p;
        for (const auto& v : pj.array()) p.push_back(static_cast<std::size_t>(v.as_int()));
        if (p.size() == vars.size()) out.push_back(std::move(p));
      }
      if (!out.empty()) return out;
    }
  } catch (const std::exception&) {
    // Fall through to the n! enumeration below (also works without a helper).
  }
  return enumerate_automorphisms(vars, target);
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

std::string branch_key(const Branch& b) {
  std::vector<std::pair<int, std::string>> chain;
  for (const auto& el : b.chain)
    chain.emplace_back(static_cast<int>(el.kind), el.lit);
  std::sort(chain.begin(), chain.end());
  std::string s = b.source + "|" + std::to_string(b.req_mask) + "|";
  for (const auto& [kind, lit] : chain) {
    s += switch_name(static_cast<SwKind>(kind));
    s += "(" + lit + ");";
  }
  return s;
}

Branch permute_branch(const Branch& b, const std::vector<std::size_t>& perm,
                      const std::vector<std::string>& vars) {
  Branch out = b;
  out.source = permute_lit(b.source, perm, vars);
  for (auto& el : out.chain) el.lit = permute_lit(el.lit, perm, vars);
  std::uint32_t req = 0;
  for (std::size_t i = 0; i < vars.size(); ++i)
    if (b.req_mask & (std::uint32_t{1} << i))
      req |= std::uint32_t{1} << perm[i];
  out.req_mask = req;
  return out;
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
    std::set<std::string> drivers;
    for (const auto& b : d.branches)
      if (b.behavior.at(m).conducts) drivers.insert(b.source);
    if (drivers.size() != 1) return false;
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

  const auto autos = canonical ? automorphisms(vars, target)
                              : std::vector<std::vector<std::size_t>>{};
  SynthProblem problem;
  problem.n_rows = static_cast<int>(rows);
  problem.n_inverters = static_cast<int>(vars.size());
  problem.max_cost = opt.max_cost;
  problem.limit = static_cast<int>(max_solutions);

  // Pass the action of the input automorphism group on branch indices to the
  // worker.  It blocks the whole orbit of each emitted design, so the solver
  // enumerates one representative per symmetry class directly.
  if (canonical && autos.size() > 1) {
    std::map<std::string, int> index;
    for (std::size_t i = 0; i < branches.size(); ++i)
      index.emplace(branch_key(branches[i]), static_cast<int>(i));
    for (const auto& perm : autos) {
      std::vector<int> image;
      image.reserve(branches.size());
      bool complete = true;
      for (const auto& b : branches) {
        const auto it = index.find(branch_key(permute_branch(b, perm, vars)));
        if (it == index.end()) {
          complete = false;
          break;
        }
        image.push_back(it->second);
      }
      if (complete) problem.symmetries.push_back(std::move(image));
    }
  }

  problem.covers.assign(rows, {});
  problem.conducts.assign(rows, {});
  std::map<std::string, int> group_of;
  for (std::size_t i = 0; i < branches.size(); ++i) {
    problem.costs.push_back(branches[i].cost);
    problem.req.push_back(branches[i].req_mask);
    auto [it, inserted] =
        group_of.emplace(branches[i].source, static_cast<int>(group_of.size()));
    (void)inserted;
    problem.groups.push_back(it->second);
    for (std::uint32_t m = 0; m < rows; ++m) {
      if (branches[i].covers_strong(m, target[m]))
        problem.covers[m].push_back(static_cast<int>(i));
      if (branches[i].behavior.at(m).conducts)
        problem.conducts[m].push_back(static_cast<int>(i));
    }
  }

  const SynthSolution sol =
      decode_synth_solution(z3_call(encode_synth_problem(problem)));
  res.optimum = sol.optimum;
  res.feasible = sol.feasible;
  if (!sol.message.empty()) res.message = sol.message;
  if (!sol.feasible) return res;

  // Materialize the solver candidates first, then path-check the whole batch.
  // The block-diagonal C_m closure is a tensor over candidates; it is used
  // whenever more than one candidate survived the cheap behavioral check.
  auto design_key = [&](const Design& d) {
    if (canonical) {
      std::string best_key;
      for (const auto& perm : autos) {
        std::string k = canonical_key(d, perm, vars);
        if (best_key.empty() || k < best_key) best_key = std::move(k);
      }
      return best_key;
    }
    std::vector<std::string> parts;
    for (const auto& b : d.branches) parts.push_back(b.topo(vars));
    std::sort(parts.begin(), parts.end());
    std::string key;
    for (const auto& p : parts) key += p + "/";
    return key;
  };
  std::vector<Design> candidates;
  std::set<std::string> candidate_keys;
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
    const std::string key = design_key(d);
    if (!candidate_keys.insert(key).second) continue;
    d.canonical = canonical;
    candidates.push_back(std::move(d));
  }
  std::vector<DriverProfile> profiles;
  if (candidates.size() == 1) {
    profiles.push_back(analyze_design_drivers(candidates[0], vars, target));
  } else if (!candidates.empty()) {
    std::vector<std::vector<int>> targets(candidates.size(), target);
    profiles = analyze_designs_drivers_batch(candidates, vars, targets);
  }

  for (std::size_t ci = 0; ci < candidates.size(); ++ci) {
    if (ci >= profiles.size() || !profiles[ci].valid) continue;
    res.designs.push_back(std::move(candidates[ci]));
    if (res.designs.size() >= max_solutions) break;
  }

  res.feasible = !res.designs.empty();
  if (!res.feasible) res.message = "no design found";
  return res;
}

}  // namespace dcstudy
