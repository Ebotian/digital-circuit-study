// Finite-state-machine support: load a sequential spec, analyse it, price the
// state register, and compare two machines with a product-machine traversal.
//
// The next-state and output functions are ordinary Boolean functions over
// (state, inputs), so the combinational backends stay untouched: an FSM is a
// MultiProblem plus a register.  Unreachable (state, input) rows are the only
// new information the analysis contributes, and they become don't-care masks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core/boolfunc.hpp"
#include "core/partial.hpp"

namespace dcstudy {

// BoolFunc's arity limit: state bits plus inputs must stay within it.
inline constexpr std::size_t kFsmMaxArity = 16;

struct FsmOutput {
  std::string name;
  BoolFunc func;
  bool registered = false;
};

struct FsmSpec {
  std::string name;
  std::size_t state_bits = 0;
  std::string reset; // reset state name
  std::vector<std::string> inputs;
  std::map<std::string, std::uint32_t> states; // state name -> code
  std::vector<std::string> state_vars;         // s{k-1} .. s0
  std::vector<BoolFunc> next;                  // same order as state_vars
  std::vector<FsmOutput> outputs;
  std::string reset_mode = "sync";  // sync | async
  std::string reset_active = "low"; // low | high

  std::size_t state_count() const { return std::size_t{1} << state_bits; }
  std::size_t rows() const;
  std::uint32_t reset_code() const;
  std::size_t input_count() const { return inputs.size(); }
  std::size_t output_count() const { return outputs.size(); }
};

// Load and validate an FSM spec (design doc §4.1).  Throws std::runtime_error
// with a diagnostic; the CLI maps that to exit code 2.
FsmSpec load_fsm_spec(const std::string &path);

struct FsmAnalysis {
  std::vector<std::uint32_t> reachable;   // ascending state codes
  std::vector<std::uint32_t> unreachable; // ascending state codes
  std::size_t rows = 0;                   // 2^(state_bits + inputs)
  std::size_t dc_rows = 0;                // unreachable * 2^inputs
};

FsmAnalysis analyze_fsm(const FsmSpec &fsm);

// next bits + outputs as one combinational problem.  The unreachable rows carry
// a full don't-care mask (one bit per output), so multi exact/synth/path can
// use them.
MultiProblem fsm_problem(const FsmSpec &fsm, const FsmAnalysis &analysis);

struct FsmCounterExample {
  std::vector<std::uint32_t> spec_states;
  std::vector<std::uint32_t> dut_states;
  // One input code per entry of spec_states: the transitions taken from the
  // reset state, then the input that exposes the mismatch.
  std::vector<std::uint32_t> inputs;
  std::size_t depth() const { return inputs.size(); }
};

struct FsmEquiv {
  bool equivalent = false;
  std::string reason;
  FsmCounterExample cex;
};

// Product machine from both reset states, BFS over (spec, dut) state pairs and
// every output on every reachable pair.  The machines must agree on input
// names/order and output names.
FsmEquiv compare_fsm(const FsmSpec &spec, const FsmSpec &dut);

struct FsmCost {
  bool ok = false;
  std::string reason;
  std::string cell;
  std::size_t dffs = 0;
  int unit = 0;  // transistors per flip-flop, measured from the CDL
  int total = 0; // dffs * unit
};

// Price the state register.  The cell must exist in the CDL; the default path
// comes from core/cdl.hpp (NANGATE45_CDL / PDK_ROOT / ~/yosys-sta/...).
FsmCost price_fsm_register(const FsmSpec &fsm, const std::string &cdl_path,
                           const std::string &cell);

} // namespace dcstudy
