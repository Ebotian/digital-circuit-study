#!/usr/bin/env bash
# Instance: sequential FSM analysis at S0 (REQ-SEQ-001..013, 040, 044, 057).
# Known answers: the 2-bit counter is fully reachable; the "10" detector leaves
# unused code 11 unreachable (2 don't-care rows); a mutated transition must be
# rejected by the product machine with a counterexample.
. "$(dirname "$0")/../../lib.sh"
S="$(dirname "$0")/../specs"

C=$("$BIN" seq check --spec "$S/fsm_counter2.json" --json)
[ "$(jq -r .ok <<<"$C")" = "true" ] || fail "counter2 ok"
[ "$(jq -r .state_bits <<<"$C")" = "2" ] || fail "counter2 state bits"
[ "$(jq -r .reachable_states <<<"$C")" = "4" ] || fail "counter2 reachable"
[ "$(jq -r .dc_rows <<<"$C")" = "0" ] || fail "counter2 dc rows"
[ "$(jq -r '.dff.bits' <<<"$C")" = "2" ] || fail "counter2 dff bits"
T=$(jq -r '.dff.transistors // empty' <<<"$C")
if [ -n "$T" ]; then
  [ "$(jq -r '.dff.cost' <<<"$C")" = "$((T * 2))" ] || fail "counter2 dff cost"
fi

D=$("$BIN" seq check --spec "$S/fsm_seqdet.json" --json)
[ "$(jq -r .reachable_states <<<"$D")" = "3" ] || fail "seqdet reachable"
[ "$(jq -r .dc_rows <<<"$D")" = "2" ] || fail "seqdet dc rows"
[ "$(jq -r '.unreachable_states | join(",")' <<<"$D")" = "S3" ] || fail "seqdet unreachable"

Q=$("$BIN" seq check --spec "$S/fsm_seqdet.json" --dut "$S/fsm_seqdet.json" --json)
[ "$(jq -r '.dut.equivalent' <<<"$Q")" = "true" ] || fail "seqdet self equivalence"

MUT=$(mktemp)
trap 'rm -f "$MUT"' EXIT
rc=0
"$BIN" seq check --spec "$S/fsm_seqdet.json" --dut "$S/fsm_seqdet_mut.json" \
  --json > "$MUT" 2>/dev/null || rc=$?
[ "$rc" -eq 1 ] || fail "mutated FSM accepted ($rc)"
[ "$(jq -r '.dut.counterexample.steps | length' "$MUT")" -gt 0 ] \
  || fail "mutation has no counterexample"

pass "fsm_seq     counter2 4/4 reachable; 10-detector 3/4 + 2 DC rows; mutation exit 1"

# S1: mapped RTL with the dff/comb split, BMC and Verilator (REQ-SEQ-020..024,
# 030..033, 041..043).  Needs the whole mapping stack, so it skips cleanly.
if command -v yosys >/dev/null 2>&1 && command -v verilator >/dev/null 2>&1 \
   && [ -f "${NANGATE45_CDL:-$HOME/yosys-sta/pdk/nangate45/cdl/NangateOpenCellLibrary.cdl}" ]; then
  W=$(mktemp -d)
  Y=$("$BIN" seq synth --spec "$S/fsm_counter2.json" --out "$W" --cycles 8 --json)
  [ "$(jq -r .verified <<<"$Y")" = "true" ] || fail "seq synth counter verify"
  [ "$(jq -r .bmc <<<"$Y")" = "true" ] || fail "seq synth bmc"
  [ "$(jq -r .sim <<<"$Y")" = "true" ] || fail "seq synth sim"
  [ "$(jq -r .dffs <<<"$Y")" = "2" ] || fail "seq synth dffs"
  [ "$(jq -r .dff_cost <<<"$Y")" = "56" ] || fail "seq synth dff cost"
  [ "$(jq -r .total <<<"$Y")" = "$(( $(jq -r .dff_cost <<<"$Y") + $(jq -r .comb_cost <<<"$Y") ))" ] \
    || fail "seq synth cost split"
  [ -f "$W/top.v" ] && [ -f "$W/gold.v" ] && [ -f "$W/net.v" ] || fail "seq synth files"
  rm -rf "$W"
  pass "fsm_seq     seq synth counter2: 2 DFF (56T) + comb, bmc+sim PASS"
else
  echo "skip fsm_seq seq synth (yosys/verilator/Nangate45 missing)"
fi
