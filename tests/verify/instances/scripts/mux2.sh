#!/usr/bin/env bash
# Instance: 1-bit 2-to-1 multiplexer.
. "$(dirname "$0")/../../lib.sh"
S="$(dirname "$0")/../specs/mux2.json"
E=$("$BIN" multi exact --spec "$S" --json)
[ "$(jq -r .verified <<<"$E")" = "true" ] || fail "mux2 exact verify"
[ "$(jq -r .total <<<"$E")" = "14" ] || fail "mux2 exact cost"
Y=$("$BIN" multi synth --spec "$S" --json)
[ "$(jq -r .verified <<<"$Y")" = "true" ] || fail "mux2 synth verify"
[ "$(jq -r .total <<<"$Y")" = "12" ] || fail "mux2 synth cost"
P=$("$BIN" multi path --spec "$S" --json)
[ "$(jq -r .verified <<<"$P")" = "true" ] || fail "mux2 path verify"
[ "$(jq -r .total <<<"$P")" = "14" ] || fail "mux2 path cost"
pass "mux2        1-bit 2:1 mux, exact 14T / synth 12T / path 14T"
