#!/usr/bin/env bash
# Instance: 4-to-2 priority encoder, spec only.
. "$(dirname "$0")/../../lib.sh"
J=$("$BIN" multi synth --spec "$(dirname "$0")/../specs/pe42.json" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "pe42 verify"
[ "$(jq -r .total <<<"$J")" = "16" ] || fail "pe42 cost"
pass "pe42        4->2 priority encoder, synth 16T, verified"
