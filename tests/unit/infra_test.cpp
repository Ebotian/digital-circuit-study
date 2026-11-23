#include "helpers.hpp"

TEST_CASE("JSON parser and Z3 protocol round-trip", "[infra]") {
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

TEST_CASE("batched driver profile matches single-design analysis", "[infra]") {
  const FuncSpec s = spec_from_expr("A^B");
  SpaceOptions opt;
  opt.max_cost = 6;
  opt.max_chain = 3;
  const auto r = synthesize_min(s.vars, target_of(s), opt, 4, false);
  REQUIRE(r.feasible);
  REQUIRE_FALSE(r.designs.empty());
  std::vector<std::vector<int>> targets(r.designs.size(), target_of(s));
  const auto batch = analyze_designs_drivers_batch(r.designs, s.vars, targets);
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
