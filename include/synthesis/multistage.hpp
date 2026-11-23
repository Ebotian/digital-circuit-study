// Multi-stage static complementary CMOS synthesis via Yosys/ABC and the
// Nangate45 standard-cell library.  Each mapped cell is a static CMOS cell; the
// resulting cell netlist is flattened to transistors from the PDK CDL and
// checked with the exact switch-level analyzer.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/boolfunc.hpp"
#include "core/partial.hpp"

namespace dcstudy {

struct GateInstance {
  std::string type;
  std::map<std::string, std::string> ports; // signal ports only
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
  std::string rtl; // behavioural module fed to the mapper (reusable as atom)
  std::vector<std::string> truths; // parallel to outputs
  bool verified = false;
};

std::string default_liberty_path();
MultiStageDesign synthesize_multistage_static(
    const BoolFunc &f, const std::vector<std::string> &vars, int max_fanin = 0);
// Don't-care rows are completed with the cheapest static-gate extension
// before mapping, and verification accepts any 0/1 on those rows.
MultiStageMultiDesign
synthesize_multistage_static_multi(const MultiProblem &problem,
                                   int max_fanin = 0);

// A hierarchical RTL design that instantiates user atoms (e.g. a 4-2
// priority encoder).  Atoms are read as blackboxes for mapping, measured
// separately, and the composition is proved against the spec per output.
struct RtlAtomUse {
  std::string module;
  int instances = 0;
  int transistors = 0;    // mapped cost of one instance
  std::string parameters; // rendered instance parameters, empty = defaults
};

struct RtlSynthResult {
  bool ok = false;
  std::string reason;
  int total = 0;
  int atom_cost = 0;
  int cell_cost = 0;
  std::vector<std::string> outputs;
  std::vector<RtlAtomUse> atoms;
  bool verified = false;
  std::string verilog;
  std::string json;
};

// Structural mode: when ref_path is non-empty, the mapped design is proven
// against the reference module instead of a truth-table spec (spec may be
// empty; ports come from the mapped netlist, no 2^n table is built).
RtlSynthResult synthesize_rtl_atoms(const std::string &rtl_path,
                                    const std::string &atoms_path,
                                    const MultiProblem &spec,
                                    const std::string &ref_path = "",
                                    int max_fanin = 0);

} // namespace dcstudy
