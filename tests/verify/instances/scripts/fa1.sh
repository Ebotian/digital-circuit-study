#!/usr/bin/env bash
# Instance: 1-bit full adder.
#   S = A^B^CIN, COUT = majority(A,B,CIN).
. "$(dirname "$0")/../../lib.sh"
S="$(dirname "$0")/../specs/fa1.json"
J=$("$BIN" multi synth --spec "$S" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "fa1 synth verify"
[ "$(jq -r .total <<<"$J")" = "38" ] || fail "fa1 synth cost"
[ "$(jq -r .cells <<<"$J")" = "7" ] || fail "fa1 synth cells"
E=$("$BIN" multi exact --spec "$S" --json)
[ "$(jq -r .verified <<<"$E")" = "true" ] || fail "fa1 exact verify"
[ "$(jq -r .total <<<"$E")" = "42" ] || fail "fa1 exact cost"
D="$(dirname "$0")/../rtl"
H=$("$BIN" multi synth --spec "$(dirname "$0")/../specs/ha.json" \
      --emit-atom ha --json \
    | "$BIN" multi synth --rtl "$D/fa_ha.v" --atoms - \
        --ref "$D/gold_fa.v" --json)
[ "$(jq -r .verified <<<"$H")" = "true" ] || fail "fa1 half-adder verify"
[ "$(jq -r .total <<<"$H")" = "34" ] || fail "fa1 half-adder cost"
[ "$(jq -r '.atoms[0].module' <<<"$H")" = "ha" ] || fail "fa1 ha atom"
[ "$(jq -r '.atoms[0].instances' <<<"$H")" = "2" ] || fail "fa1 ha count"
pass "fa1         1-bit full adder: synth 38T, exact 42T, 2x ha + OR = 34T"
