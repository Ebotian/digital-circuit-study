#!/usr/bin/env bash
# Instance: 4-bit equality comparator.
#   EQ = 1 iff A3..A0 == B3..B0 (8 inputs, 256 rows).
. "$(dirname "$0")/../../lib.sh"
S="$(dirname "$0")/../specs/eq4.json"
J=$("$BIN" multi synth --spec "$S" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "eq4 verify"
[ "$(jq -r .total <<<"$J")" = "62" ] || fail "eq4 cost"
[ "$(jq -r .cells <<<"$J")" = "12" ] || fail "eq4 cells"
pass "eq4         4-bit equality comparator, 12 cells = 62T, verified"
