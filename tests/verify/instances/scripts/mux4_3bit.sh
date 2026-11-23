#!/usr/bin/env bash
# Instance: 3-bit 4-to-1 mux, composed from nine 1-bit 2:1 mux atoms.
. "$(dirname "$0")/../../lib.sh"
D="$(dirname "$0")/../rtl"
J=$("$BIN" multi synth --rtl "$D/mux4_3bit.v" --atoms "$D/mux2.v" \
    --ref "$D/gold_mux4_3bit.v" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "mux4_3bit verify"
[ "$(jq -r .total <<<"$J")" = "108" ] || fail "mux4_3bit cost"
[ "$(jq -r '.atoms[0].instances' <<<"$J")" = "9" ] || fail "mux4_3bit mux2 count"
[ "$(jq -r '.atoms[0].transistors' <<<"$J")" = "12" ] || fail "mux4_3bit mux2 cost"
pass "mux4_3bit   3-bit 4:1 mux, 9x mux2(12T) = 108T, miter PASS"
