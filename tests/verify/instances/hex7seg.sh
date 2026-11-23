#!/usr/bin/env bash
# Instance: 4-bit hex to 7-segment decoder.
#   default synth: 31 cells / 162T; --max-fanin 2: 44 cells / 192T;
#   exact (single-stage, exhaustive): 180T.
. "$(dirname "$0")/../lib.sh"
S="$ROOT/specs/hex7seg.json"
J=$("$BIN" multi synth --spec "$S" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "hex7seg synth verify"
[ "$(jq -r .total <<<"$J")" = "162" ] || fail "hex7seg synth cost"
[ "$(jq -r '.cells | length' <<<"$J")" = "31" ] || fail "hex7seg synth cells"
F=$("$BIN" multi synth --spec "$S" --max-fanin 2 --json)
[ "$(jq -r .verified <<<"$F")" = "true" ] || fail "hex7seg fanin2 verify"
[ "$(jq -r .total <<<"$F")" = "192" ] || fail "hex7seg fanin2 cost"
[ "$(jq -r '.cells | length' <<<"$F")" = "44" ] || fail "hex7seg fanin2 cells"
E=$("$BIN" multi exact --spec "$S" --json)
[ "$(jq -r .verified <<<"$E")" = "true" ] || fail "hex7seg exact verify"
[ "$(jq -r .total <<<"$E")" = "180" ] || fail "hex7seg exact cost"
pass "hex7seg     4->7 segment decoder, synth 31 cells/162T, fanin2 44/192T, exact 180T"
