# 时序逻辑 RVTM（Requirements Verification Traceability Matrix）

> 本文件由 `tools/rvtm.py` 生成，请勿手改：改 `requirements/rvtm.json` 后运行 `make rvtm-gen`。
> 设计基线：`docs/sequential-design.md`；基线日期：2026-11-27；需求数：52。
> 状态：verified（有证据）/ in_progress / waived（有理由）/ not_implemented（默认）。

## 汇总

| 状态 | 数量 |
|---|---|
| not_implemented | 13 |
| in_progress | 11 |
| verified | 27 |
| waived | 1 |

## 未实现清单（按阶段）

| 阶段 | 需求 |
|---|---|
| S1 | REQ-SEQ-034 |
| S2 | REQ-SEQ-005、REQ-SEQ-014、REQ-SEQ-025、REQ-SEQ-045、REQ-SEQ-054、REQ-SEQ-055 |
| S3 | REQ-SEQ-023、REQ-SEQ-056 |
| S4 | REQ-SEQ-070、REQ-SEQ-071、REQ-SEQ-072、REQ-SEQ-073 |

## 明细

| REQ | 阶段 | 状态 | 证据 | 说明 |
|---|---|---|---|---|
| REQ-SEQ-001 | S0 | verified | tests/unit/fsm_test.cpp（FSM specs load with the documented shape）；tests/verify/instances/scripts/fsm_seq.sh | — |
| REQ-SEQ-002 | S0 | in_progress | tests/unit/fsm_test.cpp（FSM spec validation rejects malformed machines） | spec encoding is explicit and validated; --encoding candidates are S3 (REQ-SEQ-023) |
| REQ-SEQ-003 | S0 | verified | tests/unit/fsm_test.cpp（FSM spec validation rejects malformed machines） | — |
| REQ-SEQ-004 | S0 | verified | tests/unit/fsm_test.cpp（FSM outputs may be Moore or Mealy） | — |
| REQ-SEQ-005 | S2 | not_implemented | tests/unit/fsm_test.cpp（FSM spec validation rejects malformed machines） | S2 (registered outputs); S0 and S1 reject them with a REQ-SEQ-005 message |
| REQ-SEQ-006 | S0 | verified | tests/unit/fsm_test.cpp（FSM arity and encoding limits are enforced） | — |
| REQ-SEQ-010 | S0 | verified | tests/unit/fsm_test.cpp（FSM specs load with the documented shape）；tests/verify/instances/scripts/fsm_seq.sh | — |
| REQ-SEQ-011 | S0 | verified | tests/unit/fsm_test.cpp（unreachable FSM states become don't-care rows） | — |
| REQ-SEQ-012 | S0 | verified | tests/unit/seq_rtl_test.cpp（sequential RTL is emitted with flops and reset）；tests/integration.sh | emitted RTL uses the total spec functions (no X/Z); seq synth JSON reports dc_rows |
| REQ-SEQ-013 | S0 | in_progress | tests/verify/instances/scripts/fsm_seq.sh | counts are reported; the state diagram (grey unreachable states) is S2 |
| REQ-SEQ-014 | S2 | not_implemented | — | — |
| REQ-SEQ-020 | S1 | verified | tests/unit/seq_rtl_test.cpp（mapped FSM splits dff and comb cost and verifies）；tests/integration.sh | same Yosys/ABC + CDL pricing pipeline as the combinational flow |
| REQ-SEQ-021 | S1 | verified | tests/unit/seq_rtl_test.cpp（mapped FSM splits dff and comb cost and verifies）；tests/unit/seq_rtl_test.cpp（async reset maps to set/reset flops） | dfflibmap maps the flops; DFF_X1/DFFR_X1/DFFS_X1 appear as library cells |
| REQ-SEQ-022 | S1 | verified | tests/unit/seq_rtl_test.cpp（sequential RTL is emitted with flops and reset）；tests/unit/seq_rtl_test.cpp（async reset maps to set/reset flops） | sync and async reset emitted per spec; async maps to DFFR/DFFS cells |
| REQ-SEQ-023 | S3 | not_implemented | — | — |
| REQ-SEQ-024 | S1 | verified | tests/unit/seq_rtl_test.cpp（mapped FSM splits dff and comb cost and verifies）；tests/integration.sh | mapping + CDL cost + SAT/simulation; no proven_by_spec shortcut |
| REQ-SEQ-025 | S2 | not_implemented | — | — |
| REQ-SEQ-030 | S1 | verified | tests/unit/seq_rtl_test.cpp（mapped FSM splits dff and comb cost and verifies）；tests/verify/instances/scripts/fsm_seq.sh | — |
| REQ-SEQ-031 | S1 | verified | tests/unit/seq_rtl_test.cpp（mapped FSM splits dff and comb cost and verifies） | DFF_X1 = 28T measured from the Nangate45 CDL |
| REQ-SEQ-032 | S1 | verified | tests/integration.sh；tests/verify/instances/scripts/fsm_seq.sh | JSON separates dffs/dff_unit/dff_cost/comb_cost/total and lists cells |
| REQ-SEQ-033 | S1 | verified | tests/unit/seq_rtl_test.cpp（mapped FSM splits dff and comb cost and verifies）；tests/unit/seq_rtl_test.cpp（async reset maps to set/reset flops） | sequential cells flagged and priced into dff_cost; the FSM flow does not run static_cmos |
| REQ-SEQ-034 | S1 | not_implemented | — | — |
| REQ-SEQ-040 | S0 | verified | tests/unit/fsm_test.cpp（product machine checks sequential equivalence）；tests/verify/instances/scripts/fsm_seq.sh | — |
| REQ-SEQ-041 | S1 | verified | tests/unit/seq_rtl_test.cpp（mapped FSM splits dff and comb cost and verifies）；tests/unit/seq_rtl_test.cpp（BMC and the testbench reject a mutated netlist）；tests/integration.sh | sat -seq cycles+2 -prove mismatch 0; a wrong reset value is rejected |
| REQ-SEQ-042 | S1 | verified | tests/unit/seq_rtl_test.cpp（mapped FSM with simulation passes and prices files） | Verilator: 1 reset edge + cycles of seeded LCG stimulus, PASS/FAIL in the log |
| REQ-SEQ-043 | S1 | verified | tests/unit/seq_rtl_test.cpp（mapped FSM splits dff and comb cost and verifies） | the sequential path never sets proven_by_spec; proof_method is bmc / bmc+sim |
| REQ-SEQ-044 | S0 | in_progress | tests/unit/fsm_test.cpp（FSM spec validation rejects malformed machines）；tests/unit/seq_rtl_test.cpp（BMC and the testbench reject a mutated netlist）；tests/verify/instances/scripts/fsm_seq.sh | NC-01 (wrong reset), NC-03 (missing transition) and NC-08 (bad spec) covered; latch, undefined init, DC misuse and output delay remain |
| REQ-SEQ-045 | S2 | not_implemented | — | — |
| REQ-SEQ-046 | S2 | in_progress | — | product machine, BMC and simulation all exist; the product machine still compares spec-vs-spec, so a true three-way cross-check needs the S2 RTL->FSM extraction |
| REQ-SEQ-050 | S1 | verified | tests/integration.sh；tests/verify/instances/scripts/fsm_seq.sh | seq check (S0) and seq synth (S1) with --out/--cycles/--no-sim/--json |
| REQ-SEQ-051 | S1 | verified | tests/integration.sh | 0 verified, 1 mismatch/failed proof, 2 usage/spec error |
| REQ-SEQ-052 | S1 | verified | tests/integration.sh；tests/verify/instances/scripts/fsm_seq.sh | name/states/rows/dc_rows/dffs/dff_cost/comb_cost/total/bmc/sim/proof_method/cells/files |
| REQ-SEQ-053 | S2 | verified | tests/unit/seq_rtl_test.cpp（mapped FSM splits dff and comb cost and verifies）；tests/unit/seq_rtl_test.cpp（mapped FSM with simulation passes and prices files） | proof_method distinguishes bmc and bmc+sim; product-machine and induction arrive later |
| REQ-SEQ-054 | S2 | not_implemented | — | — |
| REQ-SEQ-055 | S2 | not_implemented | — | — |
| REQ-SEQ-056 | S3 | not_implemented | — | — |
| REQ-SEQ-057 | S0 | verified | tests/integration.sh；tests/verify/instances/scripts/fsm_seq.sh | — |
| REQ-SEQ-060 | 全程 | in_progress | — | the S0 commit cites REQ-SEQ ids; ongoing for every later commit |
| REQ-SEQ-061 | 全程 | verified | tests/verify/instances/scripts/fsm_seq.sh；tests/integration.sh | FSM assertions pin reachability/DC and the CDL flop price; combinational cost is only bounded by the split identity |
| REQ-SEQ-062 | 全程 | in_progress | — | no benchmark logic in src/; review-based, rechecked per commit |
| REQ-SEQ-063 | S0 | in_progress | tests/unit/fsm_test.cpp（FSM arity and encoding limits are enforced） | width cap tested; the product-machine pair budget path is untested |
| REQ-SEQ-064 | 全程 | in_progress | — | yosys/verilator/abc steps run under timeout (240s/600s) and inherit RLIMIT_AS; to be exercised by a capped run |
| REQ-SEQ-065 | 全程 | verified | tests/verify/README.md；tests/verify/instances/scripts/fsm_seq.sh | — |
| REQ-SEQ-066 | 全程 | in_progress | — | design doc, README and RVTM updated in the S0 commit; ongoing |
| REQ-SEQ-070 | S4 | not_implemented | — | — |
| REQ-SEQ-071 | S4 | not_implemented | — | — |
| REQ-SEQ-072 | S4 | not_implemented | — | — |
| REQ-SEQ-073 | S4 | not_implemented | — | — |
| REQ-SEQ-080 | 全程 | in_progress | — | the schema has no clock field, so multi-clock specs cannot be expressed; explicit rejection is S1 |
| REQ-SEQ-081 | 全程 | verified | docs/sequential-design.md | — |
| REQ-SEQ-082 | v1 | waived | — | 理由：v1 has no stateful-atom input path; revisit at S5 (REQ-SEQ-082 is a non-goal until then) |
| REQ-SEQ-083 | v1 | in_progress | — | no DFF reaches static_cmos yet; the split lands with S1 mapping |
