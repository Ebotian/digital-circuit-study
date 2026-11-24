#!/usr/bin/env bash
# Instance: 4-to-2 priority encoder (pe42), synth + library match.
. "$(dirname "$0")/../../lib.sh"
J=$("$BIN" multi synth --spec "$(dirname "$0")/../specs/pe42.json" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "pe42 verify"
[ "$(jq -r .total <<<"$J")" = "16" ] || fail "pe42 cost"
C=$("$BIN" multi compose --spec "$(dirname "$0")/../specs/pe42.json" \
      --library "$(dirname "$0")/../specs/library.json" --json)
[ "$(jq -r .verified <<<"$C")" = "true" ] || fail "pe42 library verify"
[ "$(jq -r .total <<<"$C")" = "16" ] || fail "pe42 library cost"
[ "$(jq -r '.atoms[0].module' <<<"$C")" = "pe42" ] || fail "pe42 library atom"
pass "pe42        4->2 priority encoder, synth 16T + library match 1x pe42"
