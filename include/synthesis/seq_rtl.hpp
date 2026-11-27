// Sequential RTL emission and verification (design doc S1).
//
// An FSM becomes flops plus the combinational next/output logic.  The emitted
// top is mapped through Yosys/dfflibmap/ABC; the library cells are priced from
// the CDL and split into dff_cost and comb_cost.  Verification is a bounded
// model check against a state-table golden model plus a Verilator run with a
// seeded stimulus; the combinational half can still go through the existing
// backends because it is a plain MultiProblem.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "synthesis/fsm.hpp"

namespace dcstudy {

struct SeqSynthOptions {
  std::string out_dir;  // empty: temporary directory only
  int cycles = 16;      // Verilator cycles; BMC covers the same count
  std::string liberty;  // empty: default_liberty_path()
  std::string cdl;      // empty: default_cdl_path()
  bool simulate = true; // also build and run the Verilator testbench
  std::uint32_t seed = 0x12345678u;
};

struct SeqCellUse {
  std::string cell;
  int instances = 0;
  int transistors = 0;
  bool sequential = false;
};

struct SeqSynthResult {
  bool ok = false;
  bool verified = false;
  std::string reason;
  std::string rtl;     // top.v
  std::string gold;    // golden state-table FSM
  std::string miter;   // BMC miter
  std::string tb;      // Verilator testbench
  std::string netlist; // mapped netlist
  std::string netlist_json;
  std::vector<SeqCellUse> cells;
  int dffs = 0;
  int dff_unit = 0; // unit price when every flop uses one cell type, else 0
  int dff_cost = 0;
  int comb_cost = 0;
  int total = 0;
  bool bmc = false;
  bool sim = false;
  std::string proof_method; // bmc+sim | bmc | sim
  int cycles = 0;
  std::string log;
  std::vector<std::string> files;
};

// Emitted Verilog.  emit_fsm_gold throws when state bits plus inputs exceed the
// state-table cap (12), and load_fsm_spec rejects reserved names, so the three
// modules always compose.
std::string emit_fsm_rtl(const FsmSpec &fsm);
std::string emit_fsm_gold(const FsmSpec &fsm);
std::string emit_seq_miter(const FsmSpec &fsm, int cycles);
std::string emit_seq_tb(const FsmSpec &fsm, int cycles, std::uint32_t seed);

struct SeqProof {
  bool ok = false;
  bool bmc = false;
  bool sim = false;
  std::string reason;
  std::string log;
};

// Prove a candidate sequential top against the golden model: BMC over
// (out !== gold) after reset, plus an optional Verilator run.  Used by
// synthesize_fsm and directly by the negative-control tests.
SeqProof verify_seq_rtl(const FsmSpec &fsm, const std::string &top_rtl,
                        int cycles, bool simulate = true,
                        std::uint32_t seed = 0x12345678u);

SeqSynthResult synthesize_fsm(const FsmSpec &fsm,
                              const SeqSynthOptions &options = {});

} // namespace dcstudy
