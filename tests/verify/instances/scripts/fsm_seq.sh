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
