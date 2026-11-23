// Fil-C memory-safety smoke test for the Z3-free core.
//
// Build with: make smoke-filc
// This target exercises the pure Boolean/CMOS/design-space/Verilog layers.
// The full CLI (make filc) also builds under Fil-C and spawns the host-built
// dcstudy-z3 worker for Z3; that path is covered by the integration tests.
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "dcstudy/boolfunc.hpp"
#include "dcstudy/boolmatrix.hpp"
#include "dcstudy/cmos.hpp"
#include "dcstudy/designspace.hpp"
#include "dcstudy/verilog.hpp"

using namespace dcstudy;

namespace {
std::vector<int> target_of(const FuncSpec& s) {
  std::vector<int> t;
  for (std::uint32_t m = 0; m < s.func.num_minterms(); ++m)
    t.push_back(s.func.value(m) ? 1 : 0);
  return t;
}
}  // namespace

int main() {
  const FuncSpec x = spec_from_expr("A^B");
  assert(x.func.truth_string() == "0110");
  assert(!x.func.is_monotone());
  assert(count_monotone(5) == 7581);

  BoolMatrix a(2), b(2);
  a.set(0, 0);
  a.set(0, 1);
  a.set(1, 1);
  b.set(0, 0);
  b.set(1, 0);
  b.set(1, 1);
  const BoolMatrix c = a * b;
  assert(c.test(0, 0) && c.test(0, 1) && c.test(1, 0) && c.test(1, 1));

  const FuncSpec nand = spec_from_expr("~(A&B)");
  const CmosDesign d = synthesize_cmos(nand.func, nand.vars);
  assert(d.realizable && d.total == 4);

  const FuncSpec nor = spec_from_expr("~(A|B)");
  const CmosDesign dn = synthesize_cmos(nor.func, nor.vars);
  assert(dn.realizable && dn.total == 4);

  SpaceOptions opt;
  opt.max_cost = 4;
  opt.max_chain = 2;
  const auto branches = usable_branches({"A", "B"}, target_of(nand), opt);
  assert(!branches.empty());
  for (const auto& br : branches) {
    for (const auto& el : br.chain) {
      if (el.kind == SwKind::TGL || el.kind == SwKind::TGH)
        assert(br.req_mask != 0);
    }
  }

  const std::string ref = emit_ref(x.func, x.vars);
  assert(ref.find("module ref") != std::string::npos);

  std::cout << "filc smoke ok: " << x.func.truth_string() << "\n";
  return 0;
}
