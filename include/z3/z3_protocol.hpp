// Request/response types for the out-of-process Z3 worker.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/cdl.hpp"
#include "core/json.hpp"
#include "switch/switchlevel.hpp"

namespace dcstudy {

struct SynthProblem {
  int n_rows = 0;
  int n_inverters = 0;
  int max_cost = 0;
  int limit = 1;
  std::vector<int> costs;               // per branch
  std::vector<std::uint32_t> req;       // per branch, inverter bitmask
  std::vector<std::vector<int>> covers; // per row, strongly-correct branch ids
  std::vector<std::vector<int>> conducts; // per row, all conducting branch ids
  std::vector<int> groups;                // per branch, source-net group id
  // Optional input-automorphism action on branch indices.  When non-empty,
  // the worker blocks the whole orbit of every solution it emits, so the
  // enumeration only produces one representative per symmetry class.
  std::vector<std::vector<int>> symmetries;
};

struct SynthSolution {
  bool feasible = false;
  int optimum = 0;
  std::vector<std::vector<int>> designs; // branch id sets
  std::string message;
};

struct SwitchProblem {
  std::vector<Device> devices;
  std::string vdd = "VDD";
  std::string vss = "VSS";
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::vector<std::uint32_t> rows;
  // Optional, parallel to rows: nets the fixed point forces to 0/1.  Fixing
  // them leaves only the genuinely free (stored) gates as variables, so the
  // solver can answer "can both rails reach this net?" in one query instead
  // of enumerating stored-charge combinations.
  std::vector<std::vector<std::string>> forced0;
  std::vector<std::vector<std::string>> forced1;
};

struct SwitchSolutionRow {
  std::vector<Logic> values;
  bool conflict = false;
  bool unsat = false;
};

struct SwitchSolution {
  std::vector<SwitchSolutionRow> rows;
};

Json encode_synth_problem(const SynthProblem &p);
SynthProblem decode_synth_problem(const Json &j);
Json encode_synth_solution(const SynthSolution &s);
SynthSolution decode_synth_solution(const Json &j);

Json encode_switch_problem(const SwitchProblem &p);
SwitchProblem decode_switch_problem(const Json &j);
Json encode_switch_solution(const SwitchSolution &s);
SwitchSolution decode_switch_solution(const Json &j);

} // namespace dcstudy
