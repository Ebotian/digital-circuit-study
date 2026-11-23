#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "dcstudy/bits.hpp"
#include "dcstudy/boolfunc.hpp"
#include "dcstudy/boolmatrix.hpp"
#include "dcstudy/cdl.hpp"
#include "dcstudy/cmos.hpp"
#include "dcstudy/designspace.hpp"
#include "dcstudy/driver_profile.hpp"
#include "dcstudy/limits.hpp"
#include "dcstudy/parallel.hpp"
#include "dcstudy/switchlevel.hpp"
#include "dcstudy/synthesis.hpp"
#include "dcstudy/verilog.hpp"
#include "dcstudy/z3_protocol.hpp"

using namespace dcstudy;

namespace {
std::vector<int> target_of(const FuncSpec& s) {
  std::vector<int> t;
  for (std::uint32_t m = 0; m < s.func.num_minterms(); ++m)
    t.push_back(s.func.value(m) ? 1 : 0);
  return t;
}
}  // namespace

TEST_CASE("series-chain enumeration is order-canonical") {
  // A series stack conducts the same way in any order, so all_chains keeps one
  // representative per multiset of atoms.  For n=4 that is 58,890 chains
  // (down from 1,082,401 permutations) times 10 sources.  A regression here
  // means the candidate explosion is back.
  SpaceOptions opt;
  opt.max_chain = 4;
  opt.max_cost = 8;
  opt.allow_tg = true;
  const auto branches = build_branches({"A", "B", "C", "D"}, opt);
  REQUIRE(branches.size() == 588890);
}

TEST_CASE("memory limit values parse strictly") {
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

TEST_CASE("default memory cap keeps exactly 2 GiB free") {
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

TEST_CASE("joint switch-graph check rejects outputs sharing a node") {
  // Y0 = ~A, Y1 = A.  Each output's own cone is correct, but both pull-downs
  // meet at the shared node u, so shaping one output drags the other.  A
  // per-output cone check cannot see that; the joint check must reject it.
  auto build = [](bool share) {
    SwitchGraph g;
    const int zero = g.add_net("0");
    const int one = g.add_net("1");
    const int y0 = g.add_net("Y0");
    const int y1 = g.add_net("Y1");
    const int u0 = share ? g.add_net("u") : g.add_net("u0");
    const int u1 = share ? u0 : g.add_net("u1");
    g.source_ids = {zero, one};
    g.outputs = {y0, y1};
    g.output = y0;
    auto add = [&](int from, int to, SwKind kind, const std::string& lit) {
      SwitchGraphEdge e;
      e.from = from;
      e.to = to;
      e.kind = kind;
      e.lit = lit;
      e.strong = true;
      e.cost = 1;
      g.edges.push_back(std::move(e));
    };
    add(one, y0, SwKind::P, "A");   // Y0 = 1 when A = 0
    add(one, y1, SwKind::P, "Ab");  // Y1 = 1 when A = 1
    add(zero, u0, SwKind::N, "A");  // pulls low when A = 1
    add(zero, u1, SwKind::N, "Ab"); // pulls low when A = 0
    add(u0, y0, SwKind::TGH, "1");
    add(u1, y1, SwKind::TGH, "1");
    return g;
  };
  const std::vector<std::vector<int>> targets{{1, 0}, {0, 1}};
  const auto shared =
      analyze_switch_graph_outputs(build(true), {"A"}, targets, false);
  REQUIRE(shared.size() == 2);
  for (const auto& p : shared) REQUIRE_FALSE(p.valid);
  const auto split =
      analyze_switch_graph_outputs(build(false), {"A"}, targets, false);
  REQUIRE(split.size() == 2);
  for (const auto& p : split) REQUIRE(p.valid);
}

TEST_CASE("parallel_for covers every index") {
  std::vector<int> seen(37, 0);
  parallel_for(seen.size(), 4, [&](std::size_t i) { seen[i] = 1; });
  for (int v : seen) REQUIRE(v == 1);
}

TEST_CASE("parallel_for propagates a task exception") {
  REQUIRE_THROWS_AS(
      parallel_for(16, 4, [](std::size_t i) {
        if (i == 7) throw std::runtime_error("task failed");
      }),
      std::runtime_error);
}

TEST_CASE("truth tables use big-endian minterms") {
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

TEST_CASE("Dedekind numbers") {
  REQUIRE(count_monotone(1) == 3);
  REQUIRE(count_monotone(2) == 6);
  REQUIRE(count_monotone(3) == 20);
  REQUIRE(count_monotone(4) == 168);
  REQUIRE(count_monotone(5) == 7581);
}

TEST_CASE("Boolean matrix closure") {
  BoolMatrix m = BoolMatrix::undirected(4, {{0, 1}, {1, 2}});
  m.closure(true);
  REQUIRE(m.test(0, 2));
  REQUIRE(m.test(2, 0));
  REQUIRE_FALSE(m.test(0, 3));
}

TEST_CASE("exact SOP of NAND2 and unrealisable XOR") {
  const FuncSpec nand = spec_from_expr("~(A&B)");
  const CmosDesign d = synthesize_cmos(nand.func, nand.vars);
  REQUIRE(d.realizable);
  REQUIRE(d.total == 4);
  REQUIRE(d.pull_down.literals == 2);

  const FuncSpec nor = spec_from_expr("~(A|B)");
  const CmosDesign dn = synthesize_cmos(nor.func, nor.vars);
  REQUIRE(dn.realizable);
  REQUIRE(dn.total == 4);
  REQUIRE(dn.pull_down.expression(nor.vars).find("~") == std::string::npos);

  const FuncSpec x = spec_from_expr("A^B");
  const CmosDesign dx = synthesize_cmos(x.func, x.vars);
  REQUIRE_FALSE(dx.realizable);

  for (const char* e : {"~(A&B)", "~(A|B)"}) {
    const FuncSpec s = spec_from_expr(e);
    const CmosDesign ds = synthesize_cmos(s.func, s.vars);
    REQUIRE(ds.realizable);
    REQUIRE(ds.total == 4);
    const Subckt cell = build_static_cmos_subckt(ds, s.vars);
    const auto sw = analyze_switch(cell, s.vars, {"Y"}, true);
    std::string truth;
    for (const auto& row : sw.rows) truth.push_back(logic_symbol(row.values[0]));
    REQUIRE(truth == s.func.truth_string());
  }
}

TEST_CASE("exact SOP covers the onset for every realisable n<=3 function") {
  for (std::size_t n = 1; n <= 3; ++n) {
    const std::size_t rows = std::size_t{1} << n;
    for (std::uint32_t code = 0; code < (std::uint32_t{1} << rows); ++code) {
      Bits bits(rows);
      for (std::uint32_t m = 0; m < rows; ++m)
        if ((code >> m) & 1U) bits.set(m);
      const BoolFunc f(n, bits);
      const BoolFunc pd = ~f;
      if (!pd.is_monotone()) continue;
      const Sop sop = exact_min_sop(pd.bits(), n);
      Bits cover(rows);
      for (const auto& c : sop.terms) cover |= c.cover;
      REQUIRE(cover == pd.bits());
    }
  }
}

TEST_CASE("design space rejects single-phase transmission gates") {
  SpaceOptions opt;
  opt.max_cost = 4;
  opt.max_chain = 2;
  const auto target = target_of(spec_from_expr("A&B"));
  const auto branches = usable_branches({"A", "B"}, target, opt);
  for (const auto& b : branches) {
    for (const auto& el : b.chain) {
      if (el.kind == SwKind::TGL || el.kind == SwKind::TGH) {
        REQUIRE((b.req_mask != 0));
      }
    }
  }
}

TEST_CASE("minimum pass-network designs match the lecture") {
  SpaceOptions opt;
  opt.max_cost = 6;
  opt.max_chain = 3;
  struct Case {
    const char* expr;
    int cost;
  };
  for (const auto c : {Case{"~(A&B)", 4}, Case{"A&B", 5}, Case{"A^B", 6}}) {
    const FuncSpec s = spec_from_expr(c.expr);
    const auto r = synthesize_min(s.vars, target_of(s), opt, 8, false);
    REQUIRE(r.feasible);
    REQUIRE(r.optimum == c.cost);
  }
}

TEST_CASE("emitted dut is Verilog") {
  const FuncSpec s = spec_from_expr("A^B");
  SpaceOptions opt;
  opt.max_cost = 6;
  const auto r = synthesize_min(s.vars, target_of(s), opt, 1, false);
  REQUIRE(r.feasible);
  const std::string dut = emit_dut(r.designs[0], s.vars);
  REQUIRE(dut.find("module dut") != std::string::npos);
  REQUIRE(dut.find("assign ok") != std::string::npos);
  REQUIRE(emit_ref(s.func, s.vars).find("module ref") != std::string::npos);
}

TEST_CASE("switch-level fixed point on a synthetic NOR2") {
  Subckt c;
  c.name = "NOR2_TEST";
  c.ports = {"A", "B", "Z", "VDD", "VSS"};
  c.pin_dir = {{"A", PinDir::Input}, {"B", PinDir::Input},
               {"Z", PinDir::Output}, {"VDD", PinDir::Power},
               {"VSS", PinDir::Ground}};
  auto n = [](const std::string& name, const std::string& d,
              const std::string& g, const std::string& s) {
    return Device{name, d, g, s, "NMOS", false};
  };
  auto p = [](const std::string& name, const std::string& d,
              const std::string& g, const std::string& s) {
    return Device{name, d, g, s, "PMOS", true};
  };
  c.devices = {
      n("N1", "Z", "A", "VSS"),
      n("N2", "Z", "B", "VSS"),
      p("P1", "Z", "A", "net"),
      p("P2", "net", "B", "VDD"),
  };
  const auto r = analyze_switch(c, {"A", "B"}, {"Z"}, false);
  REQUIRE(r.rows.size() == 4);
  REQUIRE(logic_symbol(r.rows[0].values[0]) == '1');
  REQUIRE(logic_symbol(r.rows[1].values[0]) == '0');
  REQUIRE(logic_symbol(r.rows[2].values[0]) == '0');
  REQUIRE(logic_symbol(r.rows[3].values[0]) == '0');
  const auto re = analyze_switch(c, {"A", "B"}, {"Z"}, true);
  REQUIRE(re.mode == "z3");
  for (std::size_t i = 0; i < r.rows.size(); ++i)
    REQUIRE(re.rows[i].values[0] == r.rows[i].values[0]);
  const auto rg = analyze_switch(c, {"A", "B"}, {"Z"}, false, "VDD", "VSS", true);
  REQUIRE(rg.mode == "fixpoint");
  for (std::size_t i = 0; i < r.rows.size(); ++i)
    REQUIRE(rg.rows[i].values[0] == r.rows[i].values[0]);
}

TEST_CASE("JSON parser and Z3 protocol round-trip") {
  const Json j = Json::parse(R"({"a":[1,true,null],"s":"x\n","o":{"k":-2}})");
  REQUIRE(j.at("a").array().size() == 3);
  REQUIRE(j.at("a").array()[1].as_bool());
  REQUIRE(j.at("s").as_string() == "x\n");
  REQUIRE(j.at("o").at("k").as_int() == -2);
  REQUIRE_THROWS(Json::parse("{bad}"));

  SynthProblem p;
  p.n_rows = 2;
  p.n_inverters = 1;
  p.max_cost = 6;
  p.limit = 3;
  p.costs = {1, 2};
  p.req = {0, 1};
  p.covers = {{0}, {1}};
  const SynthProblem q = decode_synth_problem(encode_synth_problem(p));
  REQUIRE(q.n_rows == p.n_rows);
  REQUIRE(q.costs == p.costs);
  REQUIRE(q.covers == p.covers);

  SynthSolution s;
  s.feasible = true;
  s.optimum = 3;
  s.designs = {{0, 1}};
  const SynthSolution s2 = decode_synth_solution(encode_synth_solution(s));
  REQUIRE(s2.designs == s.designs);

  SwitchProblem sp;
  sp.vdd = "VDD";
  sp.vss = "VSS";
  sp.inputs = {"A"};
  sp.outputs = {"Y"};
  sp.rows = {0, 1};
  sp.devices.push_back(Device{"M0", "Y", "A", "VSS", "NMOS", false});
  const SwitchProblem sp2 = decode_switch_problem(encode_switch_problem(sp));
  REQUIRE(sp2.devices.size() == 1);
  REQUIRE(sp2.devices[0].gate == "A");
  REQUIRE(sp2.rows == sp.rows);

  SwitchSolution ss;
  ss.rows.push_back(SwitchSolutionRow{{Logic::Zero}, false, false});
  const SwitchSolution ss2 = decode_switch_solution(encode_switch_solution(ss));
  REQUIRE(ss2.rows.size() == 1);
  REQUIRE(ss2.rows[0].values[0] == Logic::Zero);
}

TEST_CASE("batched driver profile matches single-design analysis") {
  const FuncSpec s = spec_from_expr("A^B");
  SpaceOptions opt;
  opt.max_cost = 6;
  opt.max_chain = 3;
  const auto r = synthesize_min(s.vars, target_of(s), opt, 4, false);
  REQUIRE(r.feasible);
  REQUIRE_FALSE(r.designs.empty());
  std::vector<std::vector<int>> targets(r.designs.size(), target_of(s));
  const auto batch =
      analyze_designs_drivers_batch(r.designs, s.vars, targets);
  const auto batch_matrix =
      analyze_designs_drivers_batch(r.designs, s.vars, targets, true);
  setenv("DCSTUDY_DRIVER_GRAPHBLAS", "1", 1);
  const auto batch_graphblas =
      analyze_designs_drivers_batch(r.designs, s.vars, targets, true);
  unsetenv("DCSTUDY_DRIVER_GRAPHBLAS");
  REQUIRE(batch.size() == r.designs.size());
  for (std::size_t i = 0; i < r.designs.size(); ++i) {
    const auto single =
        analyze_design_drivers(r.designs[i], s.vars, target_of(s));
    const auto matrix =
        analyze_design_drivers(r.designs[i], s.vars, target_of(s), true);
    REQUIRE(single.simple);
    REQUIRE_FALSE(matrix.simple);
    REQUIRE(matrix.valid == single.valid);
    REQUIRE(matrix.max_drivers == single.max_drivers);
    REQUIRE(matrix.driver_count == single.driver_count);
    REQUIRE(batch[i].valid == single.valid);
    REQUIRE(batch[i].all_single == single.all_single);
    REQUIRE(batch[i].max_drivers == single.max_drivers);
    REQUIRE(batch[i].driver_count == single.driver_count);
    REQUIRE(batch_graphblas[i].valid == single.valid);
    REQUIRE(batch_graphblas[i].driver_count == single.driver_count);
  }
}

TEST_CASE("switch graph path closure handles shared internal nodes") {
  SwitchGraph g;
  const int a = g.add_net("A");
  const int b = g.add_net("B");
  const int n = g.add_net("n");
  const int y = g.add_net("Y");
  g.output = y;
  g.source_ids = {a, b};
  auto tg = [&](int from, int to) {
    SwitchGraphEdge e;
    e.from = from;
    e.to = to;
    e.kind = SwKind::TGH;
    e.lit = "1";
    e.strong = true;
    g.edges.push_back(e);
  };
  tg(a, n);
  tg(b, n);
  tg(n, y);
  const auto p = analyze_switch_graph(g, {"A", "B"}, {0, 0, 0, 0});
  REQUIRE(p.max_drivers == 2);
  REQUIRE_FALSE(p.all_single);
  REQUIRE_FALSE(p.valid);
  REQUIRE(p.rows[0].sources == std::vector<std::string>{"A", "B"});
}

TEST_CASE("CDL parser handles a small subckt") {
  const auto dir = std::filesystem::temp_directory_path() / "dcstudy-test-cdl";
  std::filesystem::create_directories(dir);
  const auto path = dir / "x.cdl";
  std::ofstream(path) << R"(.SUBCKT INV A Y VDD VSS
*.PININFO A:I Y:O VDD:P VSS:G
M0 Y A VSS VSS NMOS_VTL W=1 L=1
M1 Y A VDD VDD PMOS_VTL W=1 L=1
.ENDS
)";
  const auto lib = CdlLibrary::load(path.string());
  const auto& c = lib.cell("INV");
  REQUIRE(c.transistor_count() == 2);
  REQUIRE(c.inputs() == std::vector<std::string>{"A"});
  REQUIRE(c.outputs() == std::vector<std::string>{"Y"});
  std::filesystem::remove_all(dir);
}
