# 时序逻辑 RVTM（Requirements Verification Traceability Matrix）

> 本文件由 `tools/rvtm.py` 生成，请勿手改：改 `requirements/rvtm.json` 后运行 `make rvtm-gen`。
> 设计基线：`docs/sequential-design.md`；基线日期：2026-11-27；需求数：52。
> 状态：verified（有证据）/ in_progress / waived（有理由）/ not_implemented（默认）。

## 汇总

| 状态 | 数量 |
|---|---|
| not_implemented | 26 |
| in_progress | 15 |
| verified | 10 |
| waived | 1 |

## 未实现清单（按阶段）

| 阶段 | 需求 |
|---|---|
| S0 | REQ-SEQ-012 |
| S1 | REQ-SEQ-020、REQ-SEQ-021、REQ-SEQ-022、REQ-SEQ-024、REQ-SEQ-030、REQ-SEQ-031、REQ-SEQ-032、REQ-SEQ-033、REQ-SEQ-034、REQ-SEQ-041、REQ-SEQ-042、REQ-SEQ-043 |
| S2 | REQ-SEQ-014、REQ-SEQ-025、REQ-SEQ-045、REQ-SEQ-046、REQ-SEQ-053、REQ-SEQ-054、REQ-SEQ-055 |
| S3 | REQ-SEQ-023、REQ-SEQ-056 |
| S4 | REQ-SEQ-070、REQ-SEQ-071、REQ-SEQ-072、REQ-SEQ-073 |

## 明细

| REQ | 阶段 | 状态 | 证据 | 说明 |
|---|---|---|---|---|
| REQ-SEQ-001 | S0 | verified | tests/unit/fsm_test.cpp（FSM specs load with the documented shape）；tests/verify/instances/scripts/fsm_seq.sh | — |
| REQ-SEQ-002 | S0 | in_progress | tests/unit/fsm_test.cpp（FSM spec validation rejects malformed machines） | spec encoding is explicit and validated; --encoding candidates are S3 (REQ-SEQ-023) |
| REQ-SEQ-003 | S0 | verified | tests/unit/fsm_test.cpp（FSM spec validation rejects malformed machines） | — |
| REQ-SEQ-004 | S0 | verified | tests/unit/fsm_test.cpp（FSM outputs may be Moore or Mealy） | — |
| REQ-SEQ-005 | S1 | in_progress | tests/unit/fsm_test.cpp（FSM spec validation rejects malformed machines） | S0 rejects registered outputs with a REQ-SEQ-005 message; semantics land in S1 |
| REQ-SEQ-006 | S0 | verified | tests/unit/fsm_test.cpp（FSM arity and encoding limits are enforced） | — |
| REQ-SEQ-010 | S0 | verified | tests/unit/fsm_test.cpp（FSM specs load with the documented shape）；tests/verify/instances/scripts/fsm_seq.sh | — |
| REQ-SEQ-011 | S0 | verified | tests/unit/fsm_test.cpp（unreachable FSM states become don't-care rows） | — |
| REQ-SEQ-012 | S0 | not_implemented | — | S0 does not emit a netlist yet; the deterministic write-out is S1 |
| REQ-SEQ-013 | S0 | in_progress | tests/verify/instances/scripts/fsm_seq.sh | counts are reported; the state diagram (grey unreachable states) is S2 |
| REQ-SEQ-014 | S2 | not_implemented | — | — |
| REQ-SEQ-020 | S1 | not_implemented | — | — |
| REQ-SEQ-021 | S1 | not_implemented | — | — |
| REQ-SEQ-022 | S1 | not_implemented | — | — |
| REQ-SEQ-023 | S3 | not_implemented | — | — |
| REQ-SEQ-024 | S1 | not_implemented | — | — |
| REQ-SEQ-025 | S2 | not_implemented | — | — |
| REQ-SEQ-030 | S1 | not_implemented | — | — |
| REQ-SEQ-031 | S1 | not_implemented | — | — |
| REQ-SEQ-032 | S1 | not_implemented | — | — |
| REQ-SEQ-033 | S1 | not_implemented | — | — |
| REQ-SEQ-034 | S1 | not_implemented | — | — |
| REQ-SEQ-040 | S0 | verified | tests/unit/fsm_test.cpp（product machine checks sequential equivalence）；tests/verify/instances/scripts/fsm_seq.sh | — |
| REQ-SEQ-041 | S1 | not_implemented | — | — |
| REQ-SEQ-042 | S1 | not_implemented | — | — |
| REQ-SEQ-043 | S1 | not_implemented | — | — |
| REQ-SEQ-044 | S0 | in_progress | tests/unit/fsm_test.cpp（FSM spec validation rejects malformed machines）；tests/verify/instances/scripts/fsm_seq.sh | NC-03 (missing transition) and NC-08 (bad spec) covered; reset value/mode, latch, undefined init, DC misuse and output delay need S1 |
| REQ-SEQ-045 | S2 | not_implemented | — | — |
| REQ-SEQ-046 | S2 | not_implemented | — | — |
| REQ-SEQ-050 | S1 | in_progress | tests/integration.sh | seq check exists; seq exact/synth are S1 |
| REQ-SEQ-051 | S1 | in_progress | tests/integration.sh | seq check returns 0/1/2 (success, mismatch, usage); other seq commands S1 |
| REQ-SEQ-052 | S1 | in_progress | tests/integration.sh | analysis and dff fields exist; comb_cost and proof fields arrive with S1/S2 |
| REQ-SEQ-053 | S2 | not_implemented | — | — |
| REQ-SEQ-054 | S2 | not_implemented | — | — |
| REQ-SEQ-055 | S2 | not_implemented | — | — |
| REQ-SEQ-056 | S3 | not_implemented | — | — |
| REQ-SEQ-057 | S0 | verified | tests/integration.sh；tests/verify/instances/scripts/fsm_seq.sh | — |
| REQ-SEQ-060 | 全程 | in_progress | — | the S0 commit cites REQ-SEQ ids; ongoing for every later commit |
| REQ-SEQ-061 | 全程 | in_progress | tests/verify/instances/scripts/fsm_seq.sh | FSM assertions pin reachability/DC and the CDL price; mapping bounds are S1 |
| REQ-SEQ-062 | 全程 | in_progress | — | no benchmark logic in src/; review-based, rechecked per commit |
| REQ-SEQ-063 | S0 | in_progress | tests/unit/fsm_test.cpp（FSM arity and encoding limits are enforced） | width cap tested; the product-machine pair budget path is untested |
| REQ-SEQ-064 | 全程 | in_progress | — | S0 adds no external process; limits are inherited for free, to re-verify at S1 |
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
