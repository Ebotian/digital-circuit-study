// Request/response types for the out-of-process Z3 worker.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dcstudy/cdl.hpp"
#include "dcstudy/json.hpp"
#include "dcstudy/switchlevel.hpp"

namespace dcstudy {

struct SynthProblem {
  int n_rows = 0;
  int n_inverters = 0;
  int max_cost = 0;
  int limit = 1;
  std::vector<int> costs;                 // per branch
  std::vector<std::uint32_t> req;         // per branch, inverter bitmask
  std::vector<std::vector<int>> covers;   // per row, strongly-correct branch ids
};

struct SynthSolution {
  bool feasible = false;
  int optimum = 0;
  std::vector<std::vector<int>> designs;  // branch id sets
  std::string message;
};

struct SwitchProblem {
  std::vector<Device> devices;
  std::string vdd = "VDD";
  std::string vss = "VSS";
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::vector<std::uint32_t> rows;
};

struct SwitchSolutionRow {
  std::vector<Logic> values;
  bool conflict = false;
  bool unsat = false;
};

struct SwitchSolution {
  std::vector<SwitchSolutionRow> rows;
};

Json encode_synth_problem(const SynthProblem& p);
SynthProblem decode_synth_problem(const Json& j);
Json encode_synth_solution(const SynthSolution& s);
SynthSolution decode_synth_solution(const Json& j);

Json encode_switch_problem(const SwitchProblem& p);
SwitchProblem decode_switch_problem(const Json& j);
Json encode_switch_solution(const SwitchSolution& s);
SwitchSolution decode_switch_solution(const Json& j);

}  // namespace dcstudy
