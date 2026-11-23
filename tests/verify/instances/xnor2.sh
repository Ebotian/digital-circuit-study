#!/usr/bin/env bash
# Instance: XNOR2, same non-realizable case as XOR2.
. "$(dirname "$0")/../lib.sh"
S="$(dirname "$0")/xnor2.json"
[ "$("$BIN" bool info --expr '~(A^B)' --json | jq -r .realizable)" = "false" ] \
  || fail "xnor2 should not be directly realizable"
E=$("$BIN" multi exact --spec "$S" --json)
[ "$(jq -r .verified <<<"$E")" = "true" ] || fail "xnor2 exact verify"
[ "$(jq -r .total <<<"$E")" = "12" ] || fail "xnor2 exact cost"
[ "$(jq -r '.inverted | length' <<<"$E")" -gt 0 ] || fail "xnor2 needs input inverters"
[ "$(jq -r .total <<<"$("$BIN" multi synth --spec "$S" --json)")" = "14" ] || fail "xnor2 synth cost"
[ "$(jq -r .total <<<"$("$BIN" multi path  --spec "$S" --json)")" = "12" ] || fail "xnor2 path cost"
pass "xnor2       not directly realizable; exact 12T / synth 14T / path 12T, verified"
