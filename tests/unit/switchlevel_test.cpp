#include "helpers.hpp"

TEST_CASE("joint switch-graph check rejects outputs sharing a node",
          "[switchlevel]") {
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
    auto add = [&](int from, int to, SwKind kind, const std::string &lit) {
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
  for (const auto &p : shared)
    REQUIRE_FALSE(p.valid);
  const auto split =
      analyze_switch_graph_outputs(build(false), {"A"}, targets, false);
  REQUIRE(split.size() == 2);
  for (const auto &p : split)
    REQUIRE(p.valid);
}

TEST_CASE("minimum pass-network designs match the lecture", "[switchlevel]") {
  REQUIRE_Z3_HELPER();
  SpaceOptions opt;
  opt.max_cost = 6;
  opt.max_chain = 3;
  struct Case {
    const char *expr;
    int cost;
  };
  for (const auto c : {Case{"~(A&B)", 4}, Case{"A&B", 5}, Case{"A^B", 6}}) {
    const FuncSpec s = spec_from_expr(c.expr);
    const auto r = synthesize_min(s.vars, target_of(s), opt, 8, false);
    REQUIRE(r.feasible);
    REQUIRE(r.optimum == c.cost);
  }
}

TEST_CASE("don't-care rows drive exact synthesis and verification",
          "[switchlevel]") {
  REQUIRE_Z3_HELPER();
  // 4-to-2 encoder: only the four one-hot inputs are care rows.
  const std::vector<std::string> vars{"A3", "A2", "A1", "A0"};
  const FuncSpec y0 = spec_from_expr("A1|A3", vars);
  const FuncSpec y1 = spec_from_expr("A2|A3", vars);
  Bits any(16);
  for (std::size_t m = 0; m < 16; ++m)
    if (m != 1 && m != 2 && m != 4 && m != 8)
      any.set(m);
  MultiProblem p;
  p.vars = vars;
  p.names = {"Y0", "Y1"};
  p.outputs = {{y0.func, any}, {y1.func, any}};
  const auto r = synthesize_static_exact_multi(p);
  REQUIRE(r.ok);
  REQUIRE(r.verified);
  // Don't-cares let the PDNs become A0|A2 and A0|A1: 4T + 4T, no inverters.
  REQUIRE(r.total == 8);
  REQUIRE(r.inverter_cost == 0);
  REQUIRE(truth_satisfies(r.truths[0], y0.func.truth_string(), any));
  REQUIRE(truth_satisfies(r.truths[1], y1.func.truth_string(), any));
  // Same expressions without don't-cares need inverters and cost more.
  MultiProblem plain = p;
  plain.outputs = {{y0.func, Bits::zeros(16)}, {y1.func, Bits::zeros(16)}};
  const auto total = synthesize_static_exact_multi(plain);
  REQUIRE(total.total > r.total);
  // A Z/X on a don't-care row is not a valid extension of a static network.
  REQUIRE_FALSE(
      truth_satisfies("ZZZZZZZZZZZZZZZZ", y0.func.truth_string(), any));
}

TEST_CASE("don't-care completion does not depend on arbitrary values",
          "[switchlevel]") {
  REQUIRE_Z3_HELPER();
  // Only AB=01 is constrained (Y=1); the other three rows are free.  Both
  // expressions must complete to the same 2T gate (a single literal's PDN),
  // not fall back to whatever the expression happens to compute on free rows.
  const std::vector<std::string> vars{"A", "B"};
  Bits any(4);
  any.set(0);
  any.set(2);
  any.set(3); // "1011": only row 1 is a care row
  const FuncSpec x = spec_from_expr("A^B", vars);
  const FuncSpec notA = spec_from_expr("~A", vars);
  MultiProblem px;
  px.vars = vars;
  px.names = {"Y"};
  px.outputs = {{x.func, any}};
  MultiProblem pn;
  pn.vars = vars;
  pn.names = {"Y"};
  pn.outputs = {{notA.func, any}};
  const auto rx = synthesize_static_exact_multi(px);
  const auto rn = synthesize_static_exact_multi(pn);
  REQUIRE(rx.ok);
  REQUIRE(rn.ok);
  REQUIRE(rx.total == 2);
  REQUIRE(rn.total == 2);
  REQUIRE(rx.truths[0] == rn.truths[0]);
}

TEST_CASE("only-high don't-care constraint keeps the inverter realizable",
          "[switchlevel]") {
  REQUIRE_Z3_HELPER();
  // Y=~A with only "A=0 => Y=1" required: A=1 is free, so the 2T inverter is
  // a valid implementation.  The must-cover PDN set is empty; the search must
  // pick a cube inside the don't-care rows instead of calling it constant.
  const FuncSpec f = spec_from_expr("~A", {"A"});
  Bits any(2);
  any.set(1); // row A=1 is free
  MultiProblem p;
  p.vars = {"A"};
  p.names = {"Y"};
  p.outputs = {{f.func, any}};
  const auto r = synthesize_static_exact_multi(p);
  REQUIRE(r.ok);
  REQUIRE(r.verified);
  REQUIRE(r.total == 2);
  REQUIRE(r.inverter_cost == 0);
}

TEST_CASE("input-driven forcing still detects a contention", "[switchlevel]") {
  REQUIRE_Z3_HELPER();
  // A PMOS gated by A ties input B to Q, so at A=0 the fixed point forces Q
  // through an input rather than a rail: invariant_ok is false, but the
  // forcing data is still valid and must reach the solver.  Y's first row
  // (A=0,B=1) is then a contention (floating F can pull it down while the
  // pull-up is on), not a definite 1.
  Subckt c;
  c.name = "RACEQ";
  c.ports = {"A", "B", "Y", "Q", "VDD", "VSS"};
  c.pin_dir = {{"A", PinDir::Input},   {"B", PinDir::Input},
               {"Y", PinDir::Output},  {"Q", PinDir::Output},
               {"VDD", PinDir::Power}, {"VSS", PinDir::Ground}};
  auto n = [](const std::string &name, const std::string &d,
              const std::string &g, const std::string &s) {
    return Device{name, d, g, s, "NMOS", false};
  };
  auto p = [](const std::string &name, const std::string &d,
              const std::string &g, const std::string &s) {
    return Device{name, d, g, s, "PMOS", true};
  };
  c.devices = {p("P1", "Y", "A", "VDD"), n("N1", "Y", "F", "VSS"),
               n("N2", "F", "A", "VSS"), p("P2", "Q", "A", "B")};
  const auto r = analyze_switch(c, {"A", "B"}, {"Y"}, true);
  REQUIRE(r.rows.size() == 4);
  REQUIRE(logic_symbol(r.rows[0].values[0]) == 'X');
  REQUIRE(logic_symbol(r.rows[1].values[0]) == 'X');
  REQUIRE(logic_symbol(r.rows[2].values[0]) == 'Z');
  REQUIRE(logic_symbol(r.rows[3].values[0]) == 'Z');
}

TEST_CASE("floating gate on a channel terminal is still enumerated",
          "[switchlevel]") {
  REQUIRE_Z3_HELPER();
  // M1 pulls Y up when A=0; M2 is gated by F; M3 can pull F low, but it is off
  // at A=0, so F floats there.  F=1 then fights the pull-up: the row is X, not
  // a definite 1.  A gate that also sits on a channel terminal must not be
  // skipped by a topology filter.
  Subckt c;
  c.name = "RACE2";
  c.ports = {"A", "Y", "VDD", "VSS"};
  c.pin_dir = {{"A", PinDir::Input},
               {"Y", PinDir::Output},
               {"VDD", PinDir::Power},
               {"VSS", PinDir::Ground}};
  auto n = [](const std::string &name, const std::string &d,
              const std::string &g, const std::string &s) {
    return Device{name, d, g, s, "NMOS", false};
  };
  auto p = [](const std::string &name, const std::string &d,
              const std::string &g, const std::string &s) {
    return Device{name, d, g, s, "PMOS", true};
  };
  c.devices = {p("P1", "Y", "A", "VDD"), n("N1", "Y", "F", "VSS"),
               n("N2", "F", "A", "VSS")};
  const auto r = analyze_switch(c, {"A"}, {"Y"}, true);
  REQUIRE(r.rows.size() == 2);
  REQUIRE(logic_symbol(r.rows[0].values[0]) == 'X');
  REQUIRE(logic_symbol(r.rows[1].values[0]) == 'Z');
}

TEST_CASE("input aliases resolve in fixpoint and exact analysis",
          "[switchlevel]") {
  REQUIRE_Z3_HELPER();
  // The port A is an alias of the internal gate net G; both modes must feed
  // the same net, otherwise the exact path analyses an unconnected input.
  Subckt c;
  c.name = "INALIAS";
  c.ports = {"A", "Y", "VDD", "VSS"};
  c.pin_dir = {{"A", PinDir::Input},
               {"Y", PinDir::Output},
               {"VDD", PinDir::Power},
               {"VSS", PinDir::Ground}};
  c.aliases = {{"A", "G"}};
  auto n = [](const std::string &name, const std::string &d,
              const std::string &g, const std::string &s) {
    return Device{name, d, g, s, "NMOS", false};
  };
  auto p = [](const std::string &name, const std::string &d,
              const std::string &g, const std::string &s) {
    return Device{name, d, g, s, "PMOS", true};
  };
  c.devices = {p("P1", "Y", "G", "VDD"), n("N1", "Y", "G", "VSS")};
  const auto fp = analyze_switch(c, {"A"}, {"Y"}, false);
  const auto ex = analyze_switch(c, {"A"}, {"Y"}, true);
  for (std::size_t i = 0; i < 2; ++i)
    REQUIRE(fp.rows[i].values[0] == ex.rows[i].values[0]);
  REQUIRE(logic_symbol(ex.rows[0].values[0]) == '1');
  REQUIRE(logic_symbol(ex.rows[1].values[0]) == '0');
}

TEST_CASE("hex truth tables fill the table bit by bit", "[switchlevel]") {
  const std::vector<std::string> vars{"A", "B", "C", "D", "E", "F", "G"};
  const FuncSpec wide = spec_from_truth("0x1", vars);
  REQUIRE(wide.func.num_minterms() == 128);
  int ones = 0;
  for (std::uint32_t m = 0; m < 128; ++m)
    ones += wide.func.value(m) ? 1 : 0;
  REQUIRE(ones == 1);
  REQUIRE(wide.func.value(0));
  REQUIRE_FALSE(wide.func.value(64)); // used to be set by a 64-bit shift
  // A digit that spills past the table is an error, not a silent truncation.
  REQUIRE_THROWS_AS(spec_from_truth("0x10000000000000000", {"A", "B"}),
                    std::invalid_argument);
  const FuncSpec full = spec_from_truth("0xf", {"A", "B"});
  for (std::uint32_t m = 0; m < 4; ++m)
    REQUIRE(full.func.value(m));
  // 64 variables used to turn 1 << n into an undefined shift.
  std::vector<std::string> too_many(64);
  for (std::size_t i = 0; i < too_many.size(); ++i)
    too_many[i] = "V" + std::to_string(i);
  REQUIRE_THROWS_AS(spec_from_truth("0x1", too_many), std::invalid_argument);
}

TEST_CASE("switch-level exact marks an undriven gate as floating",
          "[switchlevel]") {
  REQUIRE_Z3_HELPER();
  // Two NMOS: N1 (x, gate F, VSS) has an undriven gate F, N2 (Y, gate A, x).
  // With A=1, Y is pulled low only when the floating node happens to be 1, so
  // the exact answer is Z, not a definite 0.
  Subckt c;
  c.name = "FLOATY";
  c.ports = {"A", "Y", "VDD", "VSS"};
  c.pin_dir = {{"A", PinDir::Input},
               {"Y", PinDir::Output},
               {"VDD", PinDir::Power},
               {"VSS", PinDir::Ground}};
  auto n = [](const std::string &name, const std::string &d,
              const std::string &g, const std::string &s) {
    return Device{name, d, g, s, "NMOS", false};
  };
  c.devices = {n("N1", "x", "F", "VSS"), n("N2", "Y", "A", "x")};
  const auto r = analyze_switch(c, {"A"}, {"Y"}, true);
  REQUIRE(r.rows.size() == 2);
  REQUIRE(logic_symbol(r.rows[0].values[0]) == 'Z');
  REQUIRE(logic_symbol(r.rows[1].values[0]) == 'Z');
  REQUIRE_FALSE(r.rows[1].floating_gates.empty());
}

TEST_CASE("switch-level fixed point on a synthetic NOR2", "[switchlevel]") {
  REQUIRE_Z3_HELPER();
  Subckt c;
  c.name = "NOR2_TEST";
  c.ports = {"A", "B", "Z", "VDD", "VSS"};
  c.pin_dir = {{"A", PinDir::Input},
               {"B", PinDir::Input},
               {"Z", PinDir::Output},
               {"VDD", PinDir::Power},
               {"VSS", PinDir::Ground}};
  auto n = [](const std::string &name, const std::string &d,
              const std::string &g, const std::string &s) {
    return Device{name, d, g, s, "NMOS", false};
  };
  auto p = [](const std::string &name, const std::string &d,
              const std::string &g, const std::string &s) {
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
  const auto rg =
      analyze_switch(c, {"A", "B"}, {"Z"}, false, "VDD", "VSS", true);
  REQUIRE(rg.mode == "fixpoint");
  for (std::size_t i = 0; i < r.rows.size(); ++i)
    REQUIRE(rg.rows[i].values[0] == r.rows[i].values[0]);
}

TEST_CASE("switch graph path closure handles shared internal nodes",
          "[switchlevel]") {
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
