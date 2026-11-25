#!/usr/bin/env bash
# Instance: 4-bit two's complement adder with overflow flag.
#   S = (A + B) mod 16, V = 1 iff the signed sum leaves [-8, 7].
#   V = C3 ^ C4: overflow when the carry into and out of the MSB disagree,
#   equivalently when the operands share a sign and the sum's sign differs.
# Two implementation paths: BDD -> mux2 composition from the spec, and
# 4x fa1 atoms plus one XOR of the two top carries (structural miter against
# a behavioural reference).  The flat `multi synth --spec` path is not exercised:
# its exact switch-level Z3 proof over 5 outputs x 256 rows runs the worker out
# of memory, while compose and the structural miter are both seconds and exact.
. "$(dirname "$0")/../../lib.sh"
S="$(dirname "$0")/../specs/add4s.json"
D="$(dirname "$0")/../rtl"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Known answer for the overflow output: 256 rows in A3..A0,B3..B0 order.
V_TRUTH=0000000000000000000000010000000000000011000000000000011100000000000011110000000000011111000000000011111100000000011111110000000000000000111111110000000011111110000000001111110000000000111110000000000011110000000000001110000000000000110000000000000010000000
[ "$("$BIN" bool info --expr "$(jq -r '.outputs[] | select(.name=="V") | .expr' "$S")" \
      --vars "$(jq -r '.inputs | join(",")' "$S")" | sed -n 's/^truth: //p')" = "$V_TRUTH" ] \
  || fail "add4s overflow truth table"

C=$("$BIN" multi compose --spec "$S" --json)
[ "$(jq -r .verified <<<"$C")" = "true" ] || fail "add4s compose verify"
[ "$(jq -r .total <<<"$C")" = "1124" ] || fail "add4s compose cost"
[ "$(jq -r .nodes <<<"$C")" = "93" ] || fail "add4s compose nodes"

"$BIN" multi synth --spec "$(dirname "$0")/../specs/fa1.json" \
  --emit-atom fa1 --json | jq -r .atom_verilog >"$tmp/atoms.v"
J=$("$BIN" multi synth --rtl "$D/add4s.v" --atoms "$tmp/atoms.v" \
      --ref "$D/gold_add4s.v" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "add4s rtl verify"
[ "$(jq -r .total <<<"$J")" = "166" ] || fail "add4s rtl cost"
[ "$(jq -r .atom_cost <<<"$J")" = "152" ] || fail "add4s atom cost"
[ "$(jq -r .cell_cost <<<"$J")" = "14" ] || fail "add4s glue cost"
[ "$(jq -r '[.atoms[] | select(.module=="fa1") | .instances] | add' <<<"$J")" = "4" ] \
  || fail "add4s fa1 count"
pass "add4s       4-bit two's complement adder + overflow: 4x38T + 14T = 166T; compose 93 mux2 = 1124T"
