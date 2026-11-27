#include "helpers.hpp"

#include "core/verify.hpp"
#include "synthesis/abc_backend.hpp"

#include <cstdlib>
#include <string>

namespace {

bool tool_available(const char *name) {
  const std::string cmd =
      std::string("command -v ") + name + " >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0; // NOLINT(bugprone-command-processor)
}

// Both tools are external: skip locally and in CI when they are absent, the
// same way the Z3-worker tests do.
#define REQUIRE_ABC_TOOLS()                                                    \
  do {                                                                         \
    if (find_abc_binary().empty() || !tool_available("yosys"))                 \
      SKIP("abc or yosys not installed");                                      \
  } while (0)

const char *kFourInput =
    "module abc_dut(input logic A, B, C, D, output logic Y);\n"
    "  assign Y = (A & B) | (C & D);\n"
    "endmodule\n";

// prove_yosys speaks the dut/ok convention: wrap the rewritten module in a
// dut with the expected ports instead of teaching the miter a second interface.
std::string wrap_dut(const std::string &module_src, const std::string &top) {
  std::string out = module_src;
  out += "\nmodule dut(input logic A, B, C, D, output logic Y, output logic "
         "ok);\n";
  out += "  logic y;\n";
  out += "  " + top + " u(.A(A), .B(B), .C(C), .D(D), .Y(y));\n";
  out += "  assign Y = y;\n";
  out += "  assign ok = 1'b1;\n";
  out += "endmodule\n";
  return out;
}

} // namespace

TEST_CASE("missing ABC binary fails closed", "[abc]") {
  AbcOptions opt;
  opt.binary = "/nonexistent/dcstudy-abc";
  const AbcResult r = abc_rewrite(kFourInput, "abc_dut", opt);
  REQUIRE_FALSE(r.ok);
  REQUIRE_FALSE(r.reason.empty());
}

TEST_CASE("ABC round-trip keeps the function", "[abc]") {
  REQUIRE_ABC_TOOLS();
  AbcOptions opt;
  opt.optimize = false; // M0: plumbing only, no rewrite commands
  const AbcResult r = abc_rewrite(kFourInput, "abc_dut", opt);
  REQUIRE(r.ok);
  REQUIRE(r.commands.empty());
  // ABC normalises buffers and constants even on a pure round-trip, so the
  // contract is "never larger", not "identical".
  REQUIRE(r.nodes_after <= r.nodes_before);
  REQUIRE(r.verilog.find("module abc_dut") != std::string::npos);
  REQUIRE(r.version.find("ABC") != std::string::npos);
  // The truth table is the contract: a round-trip that changes it must fail.
  const FuncSpec spec = spec_from_expr("(A&B)|(C&D)", {"A", "B", "C", "D"});
  const ToolResult proof =
      prove_yosys(wrap_dut(r.verilog, "abc_dut"), spec.func, spec.vars);
  REQUIRE(proof.ok);
}

TEST_CASE("ABC balance does not grow the netlist", "[abc]") {
  REQUIRE_ABC_TOOLS();
  AbcOptions opt;
  opt.commands = "balance";
  const AbcResult r = abc_rewrite(kFourInput, "abc_dut", opt);
  REQUIRE(r.ok);
  REQUIRE(r.commands == "balance");
  REQUIRE(r.nodes_after <= r.nodes_before);
  const FuncSpec spec = spec_from_expr("(A&B)|(C&D)", {"A", "B", "C", "D"});
  const ToolResult proof =
      prove_yosys(wrap_dut(r.verilog, "abc_dut"), spec.func, spec.vars);
  REQUIRE(proof.ok);
}

TEST_CASE("ABC equivalence rejects a changed function", "[abc]") {
  REQUIRE_ABC_TOOLS();
  const std::string and2 = "module g(input logic A, B, output logic Y);\n"
                           "  assign Y = A & B;\nendmodule\n";
  const std::string or2 = "module g(input logic A, B, output logic Y);\n"
                          "  assign Y = A | B;\nendmodule\n";
  const AbcEquivResult bad = abc_equivalent(and2, or2, "g", {"A", "B"}, {"Y"});
  REQUIRE_FALSE(bad.ok);
  REQUIRE_FALSE(bad.reason.empty());
  // Reordering the expression is still the same function.
  const std::string same = "module g(input logic A, B, output logic Y);\n"
                           "  assign Y = B & A;\nendmodule\n";
  const AbcEquivResult good =
      abc_equivalent(and2, same, "g", {"A", "B"}, {"Y"});
  REQUIRE(good.ok);
}
