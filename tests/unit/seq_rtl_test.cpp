#include "helpers.hpp"

#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/cdl.hpp"
#include "synthesis/fsm.hpp"
#include "synthesis/multistage.hpp"
#include "synthesis/seq_rtl.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace {

namespace fs = std::filesystem;

bool tool_available(const char *name) {
  const std::string cmd =
      std::string("command -v ") + name + " >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0; // NOLINT(bugprone-command-processor)
}

// The synthesis tests need the whole mapping stack, not just the parser.
#define REQUIRE_SEQ_TOOLS()                                                    \
  do {                                                                         \
    if (!tool_available("yosys") || !tool_available("verilator") ||            \
        !fs::exists(default_cdl_path()) ||                                     \
        !fs::exists(default_liberty_path()))                                   \
      SKIP("yosys/verilator/Nangate45 missing");                               \
  } while (0)

fs::path write_spec(const std::string &name, const std::string &json) {
  const fs::path dir = fs::temp_directory_path() / "dcstudy-seq-test";
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

const char *kCounterAsync = R"JSON({
 "name":"counter2_async","state_bits":2,"reset":"S2","inputs":["en"],
 "reset_mode":"async","reset_active":"high",
 "state":{"S0":0,"S1":1,"S2":2,"S3":3},
 "next":{"s1":"s1 ^ (s0 & en)","s0":"s0 ^ en"},
 "outputs":[{"name":"Q1","expr":"s1"}]
})JSON";

FsmSpec counter() {
  return load_fsm_spec(write_spec("counter", kCounter).string());
}

} // namespace

TEST_CASE("sequential RTL is emitted with flops and reset", "[seqrtl]") {
  const FsmSpec fsm = counter();
  const std::string rtl = emit_fsm_rtl(fsm);
  REQUIRE(rtl.find("module top(") != std::string::npos);
  REQUIRE(rtl.find("always_ff @(posedge clk) begin") != std::string::npos);
  REQUIRE(rtl.find("if (!rst_n) begin") != std::string::npos);
  REQUIRE(rtl.find("s1 <= nxt_s1;") != std::string::npos);
  REQUIRE(rtl.find("assign Q1 = ") != std::string::npos);
  // No X/Z literals: the spec functions are total, so the netlist is defined
  // on every (state, input) row (REQ-SEQ-012).
  REQUIRE(rtl.find("'bx") == std::string::npos);
  REQUIRE(rtl.find("'bz") == std::string::npos);

  const FsmSpec async =
      load_fsm_spec(write_spec("counter_async", kCounterAsync).string());
  const std::string async_rtl = emit_fsm_rtl(async);
  REQUIRE(async_rtl.find("or posedge rst") != std::string::npos);
  REQUIRE(async_rtl.find("if (rst) begin") != std::string::npos);
  REQUIRE(async_rtl.find("s1 <= 1'b1;") != std::string::npos); // S2 = 10
}

TEST_CASE("golden model, miter and testbench follow the spec", "[seqrtl]") {
  const FsmSpec fsm = counter();
  const std::string gold = emit_fsm_gold(fsm);
  REQUIRE(gold.find("module gold(") != std::string::npos);
  REQUIRE(gold.find("case (st)") != std::string::npos);
  REQUIRE(gold.find("case ({en})") != std::string::npos);
  // 4 state codes x 2 input values = 8 next-state entries.
  std::size_t entries = 0;
  for (std::size_t pos = 0;
       (pos = gold.find("st <= 2'd", pos)) != std::string::npos; ++pos)
    ++entries;
  REQUIRE(entries >= 8);

  const std::string miter = emit_seq_miter(fsm, 8);
  REQUIRE(miter.find("localparam logic [3:0] LAST = 4'd9;") !=
          std::string::npos);
  REQUIRE(miter.find("wire valid = (cyc > 4'd1);") != std::string::npos);
  REQUIRE(miter.find("top u_dut(") != std::string::npos);
  REQUIRE(miter.find("gold u_gold(") != std::string::npos);
  REQUIRE(miter.find("output logic mismatch") != std::string::npos);

  const std::string tb = emit_seq_tb(fsm, 8, 0x12345678u);
  REQUIRE(tb.find("seed = seed * 32'd1664525 + 32'd1013904223;") !=
          std::string::npos);
  REQUIRE(tb.find("$display(\"PASS\")") != std::string::npos);
}

TEST_CASE("golden state table has a width cap", "[seqrtl]") {
  // state_bits + inputs = 13 is past the 12-bit cap; the spec loader allows
  // wide machines, so emission must refuse instead of writing a gigantic case.
  const std::string json = R"JSON({
    "name":"wide","state_bits":9,"reset":"S0",
    "inputs":["a","b","c","d"],
    "state":{"S0":0},
    "next":{"s8":"0","s7":"0","s6":"0","s5":"0","s4":"0","s3":"0","s2":"0","s1":"0","s0":"0"},
    "outputs":[]})JSON";
  const FsmSpec fsm = load_fsm_spec(write_spec("wide", json).string());
  REQUIRE_THROWS_WITH(emit_fsm_gold(fsm),
                      Catch::Matchers::ContainsSubstring("S1 cap"));
}

TEST_CASE("mapped FSM splits dff and comb cost and verifies", "[seqrtl]") {
  REQUIRE_SEQ_TOOLS();
  const FsmSpec fsm = counter();
  SeqSynthOptions opt;
  opt.cycles = 6;
  opt.simulate = false; // the Verilator path is covered by the next case
  const SeqSynthResult r = synthesize_fsm(fsm, opt);
  REQUIRE(r.ok);
  REQUIRE(r.verified);
  REQUIRE(r.bmc);
  REQUIRE_FALSE(r.sim);
  REQUIRE(r.proof_method == "bmc");
  REQUIRE(r.dffs == 2);
  REQUIRE(r.dff_unit == 28); // DFF_X1 from the Nangate45 CDL
  REQUIRE(r.dff_cost == 56);
  REQUIRE(r.comb_cost > 0);
  REQUIRE(r.total == r.dff_cost + r.comb_cost);
  bool saw_dff = false;
  for (const auto &use : r.cells)
    if (use.cell == "DFF_X1") {
      saw_dff = true;
      REQUIRE(use.sequential);
      REQUIRE(use.instances == 2);
    }
  REQUIRE(saw_dff);
}

TEST_CASE("async reset maps to set/reset flops", "[seqrtl]") {
  REQUIRE_SEQ_TOOLS();
  const FsmSpec fsm =
      load_fsm_spec(write_spec("counter_async", kCounterAsync).string());
  SeqSynthOptions opt;
  opt.cycles = 6;
  opt.simulate = true;
  const SeqSynthResult r = synthesize_fsm(fsm, opt);
  REQUIRE(r.ok);
  REQUIRE(r.verified);
  REQUIRE(r.sim);
  // S2 = 10 needs one async-set and one async-reset flop: DFFS_X1 + DFFR_X1.
  REQUIRE(r.dffs == 2);
  REQUIRE(r.dff_unit == 0);  // mixed flop types
  REQUIRE(r.dff_cost == 64); // 32T + 32T from the CDL
  std::set<std::string> seen;
  for (const auto &use : r.cells)
    if (use.sequential)
      seen.insert(use.cell);
  REQUIRE(seen == std::set<std::string>{"DFFR_X1", "DFFS_X1"});
}

TEST_CASE("BMC and the testbench reject a mutated netlist", "[seqrtl]") {
  REQUIRE_SEQ_TOOLS();
  const FsmSpec fsm = counter();
  const std::string good = emit_fsm_rtl(fsm);
  // Wrong reset value: s0 comes out of reset at 1 instead of 0.
  std::string bad = good;
  const std::string needle = "s0 <= 1'b0;";
  const auto pos = bad.find(needle);
  REQUIRE(pos != std::string::npos);
  bad.replace(pos, needle.size(), "s0 <= 1'b1;");

  const SeqProof proof = verify_seq_rtl(fsm, bad, 6, false);
  REQUIRE_FALSE(proof.ok);
  REQUIRE(proof.reason.find("bmc") != std::string::npos);
}

TEST_CASE("mapped FSM with simulation passes and prices files", "[seqrtl]") {
  REQUIRE_SEQ_TOOLS();
  const FsmSpec fsm = counter();
  SeqSynthOptions opt;
  opt.cycles = 6;
  opt.simulate = true;
  opt.out_dir = (fs::temp_directory_path() / "dcstudy-seq-out").string();
  std::error_code ec;
  fs::remove_all(opt.out_dir, ec);
  const SeqSynthResult r = synthesize_fsm(fsm, opt);
  REQUIRE(r.ok);
  REQUIRE(r.verified);
  REQUIRE(r.bmc);
  REQUIRE(r.sim);
  REQUIRE(r.proof_method == "bmc+sim");
  REQUIRE(r.files.size() == 6);
  REQUIRE(fs::exists(fs::path(opt.out_dir) / "top.v"));
  REQUIRE(fs::exists(fs::path(opt.out_dir) / "net.v"));
}
