// Multi-stage static complementary CMOS synthesis via Yosys/ABC and the
// Nangate45 standard-cell library.  Each mapped cell is a static CMOS cell; the
// resulting cell netlist is flattened to transistors from the PDK CDL and
// checked with the exact switch-level analyzer.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "dcstudy/boolfunc.hpp"
#include "dcstudy/partial.hpp"

namespace dcstudy {

struct GateInstance {
  std::string type;
  std::map<std::string, std::string> ports;  // signal ports only
};

struct MultiStageDesign {
  bool ok = false;
  std::string reason;
  int total = 0;
  int nfet = 0;
  int pfet = 0;
  std::vector<GateInstance> cells;
  std::string verilog;
  std::string json;
  std::string cdl;
  std::string truth;
  bool verified = false;
};

// General multi-output variant: one gate-level netlist with shared logic.
struct MultiStageMultiDesign {
  bool ok = false;
  std::string reason;
  int total = 0;
  int nfet = 0;
  int pfet = 0;
  std::vector<std::string> outputs;
  std::vector<GateInstance> cells;
  std::string verilog;
  std::string json;
  std::string cdl;
  std::vector<std::string> truths;  // parallel to outputs
  bool verified = false;
};

std::string default_liberty_path();
MultiStageDesign synthesize_multistage_static(const BoolFunc& f,
                                               const std::vector<std::string>& vars,
                                               int max_fanin = 0);
// Don't-care rows are completed with the cheapest static-gate extension
// before mapping, and verification accepts any 0/1 on those rows.
MultiStageMultiDesign synthesize_multistage_static_multi(
    const MultiProblem& problem, int max_fanin = 0);

}  // namespace dcstudy
