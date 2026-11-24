#!/usr/bin/env bash
# Instance: 4-bit ripple-carry adder with two hex input displays, a hex sum
# display, and a carry LED.  Built from four fa1 and three hex7seg atoms.
. "$(dirname "$0")/../../lib.sh"
D="$(dirname "$0")/../rtl"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$BIN" multi synth --spec "$(dirname "$0")/../specs/fa1.json" \
  --emit-atom fa1 --json | jq -r .atom_verilog >"$tmp/atoms.v"
"$BIN" multi synth --spec "$(dirname "$0")/../specs/hex7seg.json" \
  --emit-atom hex7seg --json | jq -r .atom_verilog >>"$tmp/atoms.v"
J=$("$BIN" multi synth --rtl "$D/rca4_7seg.v" --atoms "$tmp/atoms.v" \
      --ref "$D/gold_rca4_7seg.v" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "rca4_7seg verify"
[ "$(jq -r .total <<<"$J")" = "638" ] || fail "rca4_7seg cost"
[ "$(jq -r .cell_cost <<<"$J")" = "0" ] || fail "rca4_7seg glue"
[ "$(jq -r '[.atoms[] | select(.module=="fa1") | .instances] | add' <<<"$J")" = "4" ] \
  || fail "rca4_7seg fa1 count"
[ "$(jq -r '[.atoms[] | select(.module=="hex7seg") | .instances] | add' <<<"$J")" = "3" ] \
  || fail "rca4_7seg hex7seg count"
pass "rca4_7seg   4-bit RCA + 3x hex7seg + carry LED: 4x38T + 3x162T = 638T"
