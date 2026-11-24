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
pass "fa1         1-bit full adder, synth 7 cells=38T, exact 42T"
