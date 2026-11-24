#include "cli/cli_common.hpp"
#include "cli/cli_includes.hpp"

using namespace dcstudy;

namespace dcstudy {

void help() {
  std::cout << R"(dcstudy - Boolean circuits, exact synthesis, formal checks

Usage:
  dcstudy [--json] <command> [options]

Commands:
  bool info      --expr EXPR | --tt BITS [--vars A,B]
  bool count     --n N
  cmos synth     --expr EXPR | --tt BITS [--vars A,B]
  math closure   --n N --edges 0-1,1-2 [--json]
  math gf2       --rows '1,1;0,1' [--ncols N] [--json]
  cdl list       [--cdl FILE]
  cdl solve      --cell NAME [--cdl FILE] [--inputs A,B] [--exact]
                 [--graphblas] [--expect BITS]
  design bdd     --expr EXPR | --tt BITS [--vars A,B]
  design template --expr EXPR | --tt BITS [--vars A,B]
  design path    --expr EXPR | --tt BITS [--max-cost N] [--max-chain N] [--tri-state]
  design min     --expr EXPR | --tt BITS [--max-cost N] [--max-chain N]
                 [--all] [--limit N] [--canonical] [--emit DIR]
                 [--verify none|yosys|sim|both|static] [--static-cmos] [--keep]
  verify equiv   --dut FILE --expr EXPR | --tt BITS [--vars A,B] [--keep]
  verify sim     --dut FILE --expr EXPR | --tt BITS [--vars A,B] [--keep]
  pipeline       --funcs 'name=EXPR,...' [--max-cost N] [--out DIR]
                 [--verify none|yosys|sim|both] [--canonical]
  multi synth    --spec FILE [--out DIR] [--png] [--max-fanin N]
                 [--rtl TOP.v --atoms ATOMS.v] [--json]
  multi exact    --spec FILE [--out DIR] [--json]
  multi compose  --spec FILE [--out DIR] [--json]  (BDD -> 2:1 mux network)
  multi path     --spec FILE [--max-cost N] [--max-chain N] [--max-ops N] [--tri-state] [--json]
  multi template --spec FILE [--json]
  render         --expr EXPR | --tt BITS | --netlist FILE [--atoms FILE]
                 [--top NAME] [--max-cost N] [--limit N]
                 [--canonical] [--out DIR] [--png] [--no-logic]
                 [--static-cmos]

Options:
  --json         machine-readable output (also on errors)
  --vars         comma-separated variable order, e.g. A,B
  --max-cost     total transistor budget for a design (default 8)
  --max-chain    maximum switches in a series chain (default 4)
  --all          enumerate all optimal designs (up to --limit)
  --limit        maximum enumerated designs (default 8)
  --canonical    quotient designs by input permutations that preserve f
  --png          also rasterise rendered SVGs (needs rsvg-convert)
  --no-logic     skip the logic-abstraction panel (and its LaTeX compile)
  --static-cmos  restrict to static complementary CMOS (kappa-linear; rejects
                 pass/TG; single-stage, with automatic multi-stage fallback)
  --graphblas    use the GraphBLAS worker for switch-level reachability closure
  --tri-state    allow high-Z outputs; a 0 in a one-hot output becomes high-Z
  --spec         JSON circuit spec: {"inputs":[...],"outputs":[{"name":...,"expr":...}]}
  --max-fanin N  static CMOS cell fan-in limit (e.g. 2); 0 = library default
  --rtl FILE     hierarchical Verilog (top module `top`) to map instead of spec RTL
  --atoms FILE   atom modules read as blackboxes; "-" pipes in a previous stage
  --ref FILE     verify structurally against module FILE (spec optional)
  --emit-atom N  emit the generated module (state RTL) as atom N on stdout
  --heavy        allow the n=6 Dedekind count (about 40 s and 10 GB)
  --mem-limit    per-process memory cap in MiB, inherited by every child
                 (not a shared budget: N workers can each reach it). Default:
                 all of MemAvailable except 2 GiB. DCSTUDY_MEM_LIMIT_MB sets
                 the same cap; 0 = hard rlimit

Exit codes: 0 success, 1 verification/feasibility failure, 2 usage/runtime error.
)";
}

// Freed before reporting an allocation failure, so the error path does not
// have to allocate while the cap is already reached.

} // namespace dcstudy
