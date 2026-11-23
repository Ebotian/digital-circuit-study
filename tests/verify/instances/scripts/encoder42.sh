#!/usr/bin/env bash
# Instance: 4-to-2 encoder with don't-care inputs ("any" mask).
. "$(dirname "$0")/../../lib.sh"
E=$("$BIN" multi exact --spec "$(dirname "$0")/../specs/encoder42.json" --json)
S=$("$BIN" multi synth --spec "$(dirname "$0")/../specs/encoder42.json" --json)
P=$("$BIN" multi path  --spec "$(dirname "$0")/../specs/encoder42.json" --json)
[ "$(jq -r .verified <<<"$E")" = "true" ] || fail "encoder42 exact verify"
[ "$(jq -r .total <<<"$E")" = "8" ] || fail "encoder42 exact cost"
[ "$(jq -r .verified <<<"$S")" = "true" ] || fail "encoder42 synth verify"
[ "$(jq -r .total <<<"$S")" = "8" ] || fail "encoder42 synth cost"
[ "$(jq -r .total <<<"$P")" = "7" ] || fail "encoder42 path cost"
pass "encoder42   4->2 encoder (any), exact 8T / synth 8T / path 7T"
