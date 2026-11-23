#!/usr/bin/env bash
# Instance: 16-to-4 priority encoder = 4 x pe42 + glue, proven structurally
# against a reference module (no 64K-row truth table).
. "$(dirname "$0")/../../lib.sh"
D="$(cd "$(dirname "$0")/.." && pwd)/rtl"
J=$("$BIN" multi synth --rtl "$D/top16.v" --atoms "$D/pe42.v" \
    --ref "$D/gold16.v" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "prio16 verify"
[ "$(jq -r .total <<<"$J")" = "172" ] || fail "prio16 cost"
[ "$(jq -r '.atoms[0].instances' <<<"$J")" = "4" ] || fail "prio16 atom count"
[ "$(jq -r '.atoms[0].transistors' <<<"$J")" = "16" ] || fail "prio16 atom cost"
pass "prio16      16->4 priority encoder, 4x16T + 108T glue = 172T, miter PASS"
