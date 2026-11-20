// Multi-stage static complementary CMOS synthesis via Yosys/ABC and the
// Nangate45 standard-cell library.  Each mapped cell is a static CMOS cell; the
// resulting cell netlist is flattened to transistors from the PDK CDL and
// checked with the exact switch-level analyzer.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "dcstudy/boolfunc.hpp"

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

std::string default_liberty_path();
MultiStageDesign synthesize_multistage_static(const BoolFunc& f,
                                               const std::vector<std::string>& vars,
                                               int max_fanin = 0);

}  // namespace dcstudy
