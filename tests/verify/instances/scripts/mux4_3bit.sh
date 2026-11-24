#!/usr/bin/env bash
# Instance: 3-bit 4-to-1 mux, composed from nine 1-bit 2:1 mux atoms.
# The mux2 atom is generated from its spec and piped into the RTL stage, so
# both the atom and its cost are automatic (same pattern as the 16-4 encoder).
. "$(dirname "$0")/../../lib.sh"
D="$(dirname "$0")/../rtl"
J=$("$BIN" multi synth --spec "$(dirname "$0")/../specs/mux2.json" \
      --emit-atom mux2 --json \
    | "$BIN" multi synth --rtl "$D/mux4_3bit.v" --atoms - \
        --ref "$D/gold_mux4_3bit.v" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "mux4_3bit verify"
[ "$(jq -r .total <<<"$J")" = "108" ] || fail "mux4_3bit cost"
[ "$(jq -r '.atoms[0].instances' <<<"$J")" = "9" ] || fail "mux4_3bit mux2 count"
[ "$(jq -r '.atoms[0].transistors' <<<"$J")" = "12" ] || fail "mux4_3bit mux2 cost"
C=$("$BIN" multi compose --spec "$(dirname "$0")/../specs/mux4_3bit.json" --json)
[ "$(jq -r .verified <<<"$C")" = "true" ] || fail "mux4_3bit compose verify"
[ "$(jq -r .total <<<"$C")" = "108" ] || fail "mux4_3bit compose cost"
pass "mux4_3bit   3-bit 4:1 mux, 9x auto-mux2 = 108T (RTL + compose), miter PASS"
