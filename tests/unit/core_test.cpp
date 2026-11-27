#include "helpers.hpp"

#include "synthesis/compose.hpp"

TEST_CASE("series-chain enumeration is order-canonical", "[core]") {
  // A series stack conducts the same way in any order, so all_chains keeps one
  // representative per multiset of atoms.  For n=4 that is 58,890 chains
  // (down from 1,082,401 permutations) times 10 sources.  A regression here
  // means the candidate explosion is back.
  SpaceOptions opt;
  opt.max_chain = 4;
  opt.max_cost = kDefaultMaxCost;
  opt.allow_tg = true;
  const auto branches = build_branches({"A", "B", "C", "D"}, opt);
  REQUIRE(branches.size() == 588890);
}

TEST_CASE("memory limit values parse strictly", "[core]") {
  REQUIRE(parse_mem_limit_mb("0") == 0);
  REQUIRE(parse_mem_limit_mb("512") == 512);
  REQUIRE(parse_mem_limit_mb("1048576") == 1048576);
  REQUIRE_THROWS_AS(parse_mem_limit_mb(""), std::invalid_argument);
  REQUIRE_THROWS_AS(parse_mem_limit_mb("-1"), std::invalid_argument);
  REQUIRE_THROWS_AS(parse_mem_limit_mb(" 64"), std::invalid_argument);
  REQUIRE_THROWS_AS(parse_mem_limit_mb("2G"), std::invalid_argument);
  REQUIRE_THROWS_AS(parse_mem_limit_mb("1048577"), std::invalid_argument);
  // 0 MiB means "the hard rlimit"; when that is unlimited it stays unlimited.
  if (current_mem_limit_mb() == 0) {
    REQUIRE(apply_mem_limit(0) == 0);
    REQUIRE(current_mem_limit_mb() == 0);
  }
}

TEST_CASE("default memory cap keeps exactly 2 GiB free", "[core]") {
  // Unknown machine: no cap (nothing to subtract the reserve from).
  REQUIRE(mem_limit_from_available(0) == 0);
  // Everything past the 2 GiB reserve is fair game, and nothing more.
  REQUIRE(mem_limit_from_available(30000) == 27952);
  REQUIRE(mem_limit_from_available(4096) == 2048);
  REQUIRE(mem_limit_from_available(3000) == 952);
  // At or below the reserve there is nothing to hand out: fail, do not eat it.
  REQUIRE(mem_limit_from_available(2048) == 1);
  REQUIRE(mem_limit_from_available(100) == 1);
  // The host can be inspected on Linux.
  REQUIRE(available_memory_mb() > 0);
}

TEST_CASE("parallel_for covers every index", "[core]") {
  // Odd, non-power-of-two count exercises the uneven chunk split.
  constexpr std::size_t kOddIndexCount = 37;
  std::vector<int> seen(kOddIndexCount, 0);
  parallel_for(seen.size(), 4, [&](std::size_t i) { seen[i] = 1; });
  for (int v : seen)
    REQUIRE(v == 1);
}

TEST_CASE("parallel_for propagates a task exception", "[core]") {
  REQUIRE_THROWS_AS(parallel_for(16, 4,
                                 [](std::size_t i) {
                                   if (i == 7)
                                     throw std::runtime_error("task failed");
                                 }),
                    std::runtime_error);
}

TEST_CASE("truth tables use big-endian minterms", "[core]") {
  const FuncSpec s = spec_from_expr("A^B");
  REQUIRE(s.vars == std::vector<std::string>{"A", "B"});
  REQUIRE(s.func.truth_string() == "0110");
  REQUIRE(!s.func.is_monotone());
  REQUIRE_FALSE(s.func.is_self_dual());
  REQUIRE((~s.func).is_monotone() == false);

  const FuncSpec nand = spec_from_expr("~(A&B)");
  REQUIRE(nand.func.truth_string() == "1110");
  REQUIRE((~nand.func).is_monotone());
  REQUIRE_FALSE(nand.func.is_anf_affine());
  REQUIRE(s.func.is_anf_affine());
}

TEST_CASE("Dedekind numbers", "[core]") {
  REQUIRE(count_monotone(1) == 3);
  REQUIRE(count_monotone(2) == 6);
  REQUIRE(count_monotone(3) == 20);
  REQUIRE(count_monotone(4) == 168);
  REQUIRE(count_monotone(5) == 7581);
}

TEST_CASE("Boolean matrix closure", "[core]") {
  BoolMatrix m = BoolMatrix::undirected(4, {{0, 1}, {1, 2}});
  m.closure(true);
  REQUIRE(m.test(0, 2));
  REQUIRE(m.test(2, 0));
  REQUIRE_FALSE(m.test(0, 3));
}

TEST_CASE("exact SOP of NAND2 and unrealisable XOR", "[core]") {
  REQUIRE_Z3_HELPER();
  const FuncSpec nand = spec_from_expr("~(A&B)");
  const CmosDesign d = synthesize_cmos(nand.func, nand.vars);
  REQUIRE(d.realizable);
  REQUIRE(d.total == 4);
  REQUIRE(d.pull_down.literals == 2);

  const FuncSpec nor = spec_from_expr("~(A|B)");
  const CmosDesign dn = synthesize_cmos(nor.func, nor.vars);
  REQUIRE(dn.realizable);
  REQUIRE(dn.total == 4);
  REQUIRE(dn.pull_down.expression(nor.vars).find('~') == std::string::npos);

  const FuncSpec x = spec_from_expr("A^B");
  const CmosDesign dx = synthesize_cmos(x.func, x.vars);
  REQUIRE_FALSE(dx.realizable);

  for (const char *e : {"~(A&B)", "~(A|B)"}) {
    const FuncSpec s = spec_from_expr(e);
    const CmosDesign ds = synthesize_cmos(s.func, s.vars);
    REQUIRE(ds.realizable);
    REQUIRE(ds.total == 4);
    const Subckt cell = build_static_cmos_subckt(ds, s.vars);
    const auto sw = analyze_switch(cell, s.vars, {"Y"}, true);
    std::string truth;
    for (const auto &row : sw.rows)
      truth.push_back(logic_symbol(row.values[0]));
    REQUIRE(truth == s.func.truth_string());
  }
}

TEST_CASE("exact SOP covers the onset for every realisable n<=3 function",
          "[core]") {
  for (std::size_t n = 1; n <= 3; ++n) {
    const std::size_t rows = std::size_t{1} << n;
    for (std::uint32_t code = 0; code < (std::uint32_t{1} << rows); ++code) {
      Bits bits(rows);
      for (std::uint32_t m = 0; m < rows; ++m)
        if ((code >> m) & 1U)
          bits.set(m);
      const BoolFunc f(n, bits);
      const BoolFunc pd = ~f;
      if (!pd.is_monotone())
        continue;
      const Sop sop = exact_min_sop(pd.bits(), n);
      Bits cover(rows);
      for (const auto &c : sop.terms)
        cover |= c.cover;
      REQUIRE(cover == pd.bits());
    }
  }
}

TEST_CASE("design space rejects single-phase transmission gates", "[core]") {
  SpaceOptions opt;
  opt.max_cost = 4;
  opt.max_chain = 2;
  const auto target = target_of(spec_from_expr("A&B"));
  const auto branches = usable_branches({"A", "B"}, target, opt);
  for (const auto &b : branches) {
    for (const auto &el : b.chain) {
      if (el.kind == SwKind::TGL || el.kind == SwKind::TGH) {
        REQUIRE((b.req_mask != 0));
      }
    }
  }
}

TEST_CASE("emitted dut is Verilog", "[core]") {
  REQUIRE_Z3_HELPER();
  const FuncSpec s = spec_from_expr("A^B");
  SpaceOptions opt;
  // XOR needs 6T in the static-gate model; bound the search exactly there.
  constexpr int kXorSearchCost = 6;
  opt.max_cost = kXorSearchCost;
  const auto r = synthesize_min(s.vars, target_of(s), opt, 1, false);
  REQUIRE(r.feasible);
  const std::string dut = emit_dut(r.designs[0], s.vars);
  REQUIRE(dut.find("module dut") != std::string::npos);
  REQUIRE(dut.find("assign ok") != std::string::npos);
  REQUIRE(emit_ref(s.func, s.vars).find("module ref") != std::string::npos);
}

TEST_CASE("glue table matches its declared signal order", "[core]") {
  MultiProblem spec;
  spec.vars = {"A", "B", "C"};
  spec.names = {"Y"};
  const FuncSpec f = spec_from_expr("(A&B)|(~A&C)", {"A", "B", "C"});
  spec.outputs.push_back({f.func, Bits{}});

  const std::size_t rows = std::size_t{1} << spec.vars.size();
  const std::size_t ns = 5;
  std::vector<std::vector<unsigned char>> sig(
      ns, std::vector<unsigned char>(rows, 0));
  for (std::size_t m = 0; m < rows; ++m) {
    const unsigned a = (m >> 2u) & 1u, b = (m >> 1u) & 1u, c = m & 1u;
    sig[0][m] = b; // s0
    sig[1][m] = 0; // s1
    sig[2][m] = c; // s2
    sig[3][m] = 0; // s3
    sig[4][m] = a; // s4
  }
  const auto h = glue_from_signals(spec, sig);
  REQUIRE(h.has_value());
  if (!h)
    return;
  REQUIRE(h->vars.size() == ns);
  // DC completion makes the glue independent of unreachable coordinates, so
  // the value is defined for every index, not only the care rows.
  const std::size_t hrows = std::size_t{1} << ns;
  for (std::size_t idx = 0; idx < hrows; ++idx) {
    int sv[ns] = {};
    for (std::size_t j = 0; j < ns; ++j) {
      const int k = std::stoi(h->vars[j].substr(1));
      sv[k] = static_cast<int>((idx >> (ns - 1 - j)) & 1u);
    }
    const bool expected = sv[4] ? (sv[0] != 0) : (sv[2] != 0);
    CHECK((h->outputs[0].func.value(static_cast<std::uint32_t>(idx)) != 0) ==
          expected);
  }
}

TEST_CASE("generated miter wires cannot shadow input names", "[core]") {
  // A port named like a comparison wire is the same net, so the reference
  // would drive the input and a wrong design could still prove.
  const std::vector<std::string> vars = {"y_dut", "ok", "mismatch", "A"};
  const MiterNames n = miter_names(vars);
  REQUIRE(n.y_dut == "y_dut_");
  REQUIRE(n.ok == "ok_");
  REQUIRE(n.mismatch == "mismatch_");
  REQUIRE(n.y_ref == "y_ref"); // no input of that name
  const std::string src = emit_miter(vars);
  REQUIRE(src.find("output logic mismatch_)") != std::string::npos);
  REQUIRE(src.find("wire y_ref, y_dut_, ok_;") != std::string::npos);
  REQUIRE(src.find("wire y_dut;") == std::string::npos);
  REQUIRE(src.find("wire ok;") == std::string::npos);
}
TEST_CASE("function signatures ignore variable names", "[core]") {
  // A & ~B: the declared order is already canonical, so order stays 0,1.
  const FuncSignature s =
      canonical_signature(std::vector<unsigned char>{0, 0, 1, 0}, 2);
  REQUIRE(s.arity == 2);
  REQUIRE(s.exact);
  REQUIRE(s.table == std::vector<unsigned char>{0, 0, 1, 0});
  REQUIRE(s.order == std::vector<std::size_t>{0, 1});

  // The same function wired as B & ~A: same table, swapped order.  This is the
  // property the library search uses to match an atom output to a spec output.
  const FuncSignature t =
      canonical_signature(std::vector<unsigned char>{0, 1, 0, 0}, 2);
  REQUIRE(t.table == s.table);
  REQUIRE(t.order == std::vector<std::size_t>{1, 0});

  // Different functions never share a signature.
  const FuncSignature and2 =
      canonical_signature(std::vector<unsigned char>{0, 0, 0, 1}, 2);
  const FuncSignature or2 =
      canonical_signature(std::vector<unsigned char>{0, 1, 1, 1}, 2);
  REQUIRE(and2.table != or2.table);
}

TEST_CASE("function signatures strip unused inputs", "[core]") {
  // f(A,B,C) = A & B: C never matters, so the arity drops to 2.
  const FuncSignature s = canonical_signature(
      std::vector<unsigned char>{0, 0, 0, 0, 0, 0, 1, 1}, 3);
  REQUIRE(s.arity == 2);
  REQUIRE(s.exact);
  REQUIRE(s.table == std::vector<unsigned char>{0, 0, 0, 1});
  REQUIRE(s.order == std::vector<std::size_t>{0, 1});

  // A constant depends on nothing.
  const FuncSignature one =
      canonical_signature(std::vector<unsigned char>{1, 1, 1, 1}, 2);
  REQUIRE(one.arity == 0);
  REQUIRE(one.table == std::vector<unsigned char>{1});
  REQUIRE(one.order.empty());
}

TEST_CASE("signature search stops above its variable cap", "[core]") {
  // Parity of 7 inputs depends on every variable: past kMaxSignatureVars the
  // permutation search is skipped, and the signature reports that it is not
  // canonical so two such tables are only compared in declared order.
  std::vector<unsigned char> parity(std::size_t{1} << 7, 0);
  for (std::size_t m = 0; m < parity.size(); ++m) {
    bool odd = false;
    for (std::size_t v = 0; v < 7; ++v)
      odd = odd != (((m >> v) & 1u) != 0);
    parity[m] = odd ? 1 : 0;
  }
  const FuncSignature s = canonical_signature(parity, 7);
  REQUIRE(s.arity == 7);
  REQUIRE_FALSE(s.exact);
  REQUIRE(s.order == std::vector<std::size_t>{0, 1, 2, 3, 4, 5, 6});
}
