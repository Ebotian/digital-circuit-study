#include "helpers.hpp"

#include "synthesis/fsm.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using u32 = std::uint32_t;

fs::path write_spec(const std::string &name, const std::string &json) {
  const fs::path dir = fs::temp_directory_path() / "dcstudy-fsm-test";
  fs::create_directories(dir);
  const fs::path path = dir / (name + ".json");
  std::ofstream(path) << json;
  return path;
}

const char *kCounter = R"JSON({
 "name":"counter2","state_bits":2,"reset":"S0","inputs":["en"],
 "state":{"S0":0,"S1":1,"S2":2,"S3":3},
 "next":{"s1":"s1 ^ (s0 & en)","s0":"s0 ^ en"},
 "outputs":[{"name":"Q1","expr":"s1"},{"name":"Q0","expr":"s0"}]
})JSON";

// "10" sequence detector: S0=00, S1=01, S2=10, code 11 is unused.
const char *kSeqdet = R"JSON({
 "name":"seqdet10","state_bits":2,"reset":"S0","inputs":["A"],
 "state":{"S0":0,"S1":1,"S2":2,"S3":3},
 "next":{"s1":"~s1 & s0 & ~A","s0":"A"},
 "outputs":[{"name":"Y","expr":"s1 & ~s0"}]
})JSON";

const char *kSeqdetMut = R"JSON({
 "name":"seqdet10_mut","state_bits":2,"reset":"S0","inputs":["A"],
 "state":{"S0":0,"S1":1,"S2":2,"S3":3},
 "next":{"s1":"~s1 & s0","s0":"A"},
 "outputs":[{"name":"Y","expr":"s1 & ~s0"}]
})JSON";

std::string load_error(const fs::path &path) {
  try {
    (void)load_fsm_spec(path.string());
  } catch (const std::exception &e) {
    return e.what();
  }
  return {};
}

} // namespace

TEST_CASE("FSM specs load with the documented shape", "[fsm]") {
  const FsmSpec fsm = load_fsm_spec(write_spec("counter", kCounter).string());
  REQUIRE(fsm.name == "counter2");
  REQUIRE(fsm.state_bits == 2);
  REQUIRE(fsm.state_vars == std::vector<std::string>{"s1", "s0"});
  REQUIRE(fsm.inputs == std::vector<std::string>{"en"});
  REQUIRE(fsm.outputs.size() == 2);
  REQUIRE(fsm.reset_code() == 0);
  REQUIRE(fsm.state_count() == 4);
  REQUIRE(fsm.rows() == 8);
  REQUIRE(fsm.reset_mode == "sync");
  REQUIRE(fsm.reset_active == "low");

  // next-state and output tables are plain Boolean functions over s1,s0,en.
  const FsmAnalysis analysis = analyze_fsm(fsm);
  REQUIRE(analysis.reachable.size() == 4);
  REQUIRE(analysis.unreachable.empty());
  REQUIRE(analysis.dc_rows == 0);
  const MultiProblem problem = fsm_problem(fsm, analysis);
  REQUIRE(problem.vars == std::vector<std::string>{"s1", "s0", "en"});
  REQUIRE(problem.names == std::vector<std::string>{"s1", "s0", "Q1", "Q0"});
  REQUIRE(problem.outputs.size() == 4);
  // state 01 (s1=0,s0=1) with en=1 -> 10: s1'=1, s0'=0.
  const u32 row = (1u << 1) | 1u;
  REQUIRE(problem.outputs[0].func.value(row));
  REQUIRE_FALSE(problem.outputs[1].func.value(row));
}

TEST_CASE("unreachable FSM states become don't-care rows", "[fsm]") {
  const FsmSpec fsm = load_fsm_spec(write_spec("seqdet", kSeqdet).string());
  const FsmAnalysis analysis = analyze_fsm(fsm);
  REQUIRE(analysis.reachable == std::vector<u32>{0, 1, 2});
  REQUIRE(analysis.unreachable == std::vector<u32>{3});
  REQUIRE(analysis.rows == 8);
  REQUIRE(analysis.dc_rows == 2); // unused state 3 x two input values

  const MultiProblem problem = fsm_problem(fsm, analysis);
  REQUIRE(problem.outputs.size() == 3); // s1', s0', Y
  for (const auto &out : problem.outputs) {
    // rows 6,7 are state 3; care rows must not be masked.
    REQUIRE(out.any.test(6));
    REQUIRE(out.any.test(7));
    REQUIRE_FALSE(out.any.test(0));
    REQUIRE_FALSE(out.any.test(5));
  }
  // Y is Moore: high only in state 2 (s1=1,s0=0).
  REQUIRE(problem.outputs[2].func.value(4u));       // state 2, A=0
  REQUIRE_FALSE(problem.outputs[2].func.value(0u)); // state 0, A=0
}

TEST_CASE("product machine checks sequential equivalence", "[fsm]") {
  const FsmSpec spec = load_fsm_spec(write_spec("seqdet", kSeqdet).string());
  const FsmSpec same = load_fsm_spec(write_spec("seqdet", kSeqdet).string());
  const FsmEquiv ok = compare_fsm(spec, same);
  REQUIRE(ok.equivalent);
  REQUIRE(ok.reason.empty());

  const FsmSpec mut =
      load_fsm_spec(write_spec("seqdet_mut", kSeqdetMut).string());
  const FsmEquiv bad = compare_fsm(spec, mut);
  REQUIRE_FALSE(bad.equivalent);
  REQUIRE_FALSE(bad.reason.empty());
  REQUIRE(!bad.cex.spec_states.empty());
  REQUIRE(bad.cex.spec_states.front() == spec.reset_code());
  REQUIRE(bad.cex.dut_states.front() == mut.reset_code());
  // One input per state entry: transitions taken, then the mismatch input.
  REQUIRE(bad.cex.inputs.size() == bad.cex.spec_states.size());
  REQUIRE(bad.cex.depth() >= 1);
}

TEST_CASE("FSM outputs may be Moore or Mealy", "[fsm]") {
  const FsmSpec mealy = load_fsm_spec(write_spec("mealy", R"JSON({
    "name":"mealy","state_bits":1,"reset":"S0","inputs":["A"],
    "state":{"S0":0,"S1":1},
    "next":{"s0":"A"},
    "outputs":[{"name":"Y","expr":"s0 & A"}]})JSON")
                                          .string());
  const FsmAnalysis analysis = analyze_fsm(mealy);
  const MultiProblem problem = fsm_problem(mealy, analysis);
  REQUIRE(problem.outputs.size() == 2); // s0', Y
  // state 1 (s0=1): A=0 -> 0, A=1 -> 1, i.e. the output depends on the input.
  REQUIRE_FALSE(problem.outputs[1].func.value((1u << 1) | 0u));
  REQUIRE(problem.outputs[1].func.value((1u << 1) | 1u));
}

TEST_CASE("FSM arity and encoding limits are enforced", "[fsm]") {
  REQUIRE(load_error(write_spec("wide", R"JSON({
    "name":"wide","state_bits":16,"reset":"S0","inputs":["A"],
    "state":{"S0":0},"next":{"s15":"A","s14":"A","s13":"A","s12":"A",
      "s11":"A","s10":"A","s9":"A","s8":"A","s7":"A","s6":"A","s5":"A",
      "s4":"A","s3":"A","s2":"A","s1":"A","s0":"A"},"outputs":[]})JSON"))
              .find("limit") != std::string::npos);

  REQUIRE(load_error(write_spec("dup", R"JSON({
    "name":"dup","state_bits":2,"reset":"S0","inputs":["A"],
    "state":{"S0":0,"S1":0},"next":{"s1":"A","s0":"A"},"outputs":[]})JSON"))
              .find("share code") != std::string::npos);
}

TEST_CASE("FSM spec validation rejects malformed machines", "[fsm]") {
  REQUIRE(load_error(write_spec("bad_reset", R"JSON({
    "name":"x","state_bits":1,"reset":"S9","inputs":["A"],
    "state":{"S0":0},"next":{"s0":"A"},"outputs":[]})JSON"))
              .find("reset state") != std::string::npos);

  REQUIRE(load_error(write_spec("bad_next", R"JSON({
    "name":"x","state_bits":2,"reset":"S0","inputs":["A"],
    "state":{"S0":0},"next":{"s0":"A"},"outputs":[]})JSON"))
              .find("exactly 2 entries") != std::string::npos);

  REQUIRE(load_error(write_spec("bad_var", R"JSON({
    "name":"x","state_bits":1,"reset":"S0","inputs":["A"],
    "state":{"S0":0},"next":{"s0":"Q"},"outputs":[]})JSON"))
              .find("variable not in") != std::string::npos);

  REQUIRE(load_error(write_spec("bad_code", R"JSON({
    "name":"x","state_bits":1,"reset":"S0","inputs":["A"],
    "state":{"S0":0,"S1":3},"next":{"s0":"A"},"outputs":[]})JSON"))
              .find("does not fit") != std::string::npos);

  // Registered outputs are S1 (REQ-SEQ-005); S0 must say so, not ignore them.
  REQUIRE(load_error(write_spec("bad_reg", R"JSON({
    "name":"x","state_bits":1,"reset":"S0","inputs":["A"],
    "state":{"S0":0},"next":{"s0":"A"},
    "outputs":[{"name":"Y","expr":"s0","registered":true}]})JSON"))
              .find("REQ-SEQ-005") != std::string::npos);

  // State names may not collide with the state bits.
  REQUIRE(load_error(write_spec("bad_name", R"JSON({
    "name":"x","state_bits":1,"reset":"s0","inputs":["A"],
    "state":{"s0":0},"next":{"s0":"A"},"outputs":[]})JSON"))
              .find("collides") != std::string::npos);
}

TEST_CASE("flip-flop price is measured from the CDL", "[fsm]") {
  const fs::path dir = fs::temp_directory_path() / "dcstudy-fsm-test";
  fs::create_directories(dir);
  const fs::path cdl = dir / "cells.cdl";
  std::ofstream(cdl) << ".SUBCKT DFF_X1 D Q CLK VDD VSS\n"
                        "*.PININFO D:I Q:O CLK:I VDD:P VSS:G\n"
                        "M0 a D VSS VSS NMOS_VTL W=1 L=1\n"
                        "M1 b a VSS VSS NMOS_VTL W=1 L=1\n"
                        "M2 c b VSS VSS NMOS_VTL W=1 L=1\n"
                        "M3 Q c VSS VSS NMOS_VTL W=1 L=1\n"
                        "M4 Q c VDD VDD PMOS_VTL W=1 L=1\n"
                        "M5 c b VDD VDD PMOS_VTL W=1 L=1\n"
                        "M6 b a VDD VDD PMOS_VTL W=1 L=1\n"
                        "M7 a D VDD VDD PMOS_VTL W=1 L=1\n"
                        ".ENDS\n";
  const FsmSpec fsm = load_fsm_spec(write_spec("counter", kCounter).string());
  const FsmCost cost = price_fsm_register(fsm, cdl.string(), "DFF_X1");
  REQUIRE(cost.ok);
  REQUIRE(cost.cell == "DFF_X1");
  REQUIRE(cost.dffs == 2);
  REQUIRE(cost.unit == 8);
  REQUIRE(cost.total == 16);

  const FsmCost missing_cell = price_fsm_register(fsm, cdl.string(), "DFFR_X1");
  REQUIRE_FALSE(missing_cell.ok);
  REQUIRE(missing_cell.reason.find("not in") != std::string::npos);

  const FsmCost missing_file =
      price_fsm_register(fsm, (dir / "absent.cdl").string(), "DFF_X1");
  REQUIRE_FALSE(missing_file.ok);
  REQUIRE(missing_file.reason.find("not found") != std::string::npos);
}
