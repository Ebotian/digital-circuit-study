// Pass-transistor design space D: a branch is a source net feeding a series
// chain of switches into the output; branches are wired in parallel.
//
// Switch costs: N/P = 1T, transmission gate = 2T.  A transmission gate is only
// legal when BOTH phases of its control literal are available (the old
// enumerate_xor.py forgot this and produced unsound 4T AND gates).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dcstudy {

enum class SwKind { N, P, TGL, TGH };

struct ChainElem {
  SwKind kind;
  std::string lit;
  int cost = 1;
};

struct Behavior {
  bool conducts = false;
  bool strong = false;
  int value = 0;
};

struct Branch {
  std::string source;
  std::vector<ChainElem> chain;
  int cost = 0;
  std::uint32_t req_mask = 0; // bit i => inverter for variable i is needed
  std::vector<Behavior> behavior;

  bool covers_strong(std::size_t minterm, int target) const;
  bool usable(const std::vector<int> &target) const;
  std::string topo(const std::vector<std::string> &vars) const;
};

struct SpaceOptions {
  int max_chain = 4;
  int max_cost = 8;
  bool allow_tg = true;
  // Work budget for search-based flows (0 = unlimited).  This bounds
  // 2^n inverter masks x outputs x chain candidates, not the input count.
  std::size_t max_ops = 20000000;
};

std::vector<Branch> build_branches(const std::vector<std::string> &vars,
                                   const SpaceOptions &opt);

std::vector<Branch> usable_branches(const std::vector<std::string> &vars,
                                    const std::vector<int> &target,
                                    const SpaceOptions &opt);

int switch_cost(SwKind k);
const char *switch_name(SwKind k);

// True when chain a is a prefix of chain b, i.e. every edge of a is also an
// edge of b.  Used to keep only candidates that are not strict edge-set
// supersets of a cheaper branch with the same behaviour.
bool chain_is_prefix(const std::vector<ChainElem> &a,
                     const std::vector<ChainElem> &b);

} // namespace dcstudy
