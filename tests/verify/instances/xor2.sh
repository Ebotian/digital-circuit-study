#!/usr/bin/env bash
# Instance: XOR2, the "not directly realizable" case: no single complementary
# gate without input inverters, yet the automatic design still finds one.
. "$(dirname "$0")/../lib.sh"
S="$(dirname "$0")/xor2.json"
[ "$("$BIN" bool info --expr 'A^B' --json | jq -r .realizable)" = "false" ] \
  || fail "xor2 should not be directly realizable"
E=$("$BIN" multi exact --spec "$S" --json)
[ "$(jq -r .verified <<<"$E")" = "true" ] || fail "xor2 exact verify"
[ "$(jq -r .total <<<"$E")" = "12" ] || fail "xor2 exact cost"
[ "$(jq -r '.inverted | length' <<<"$E")" -gt 0 ] || fail "xor2 needs input inverters"
[ "$(jq -r .total <<<"$("$BIN" multi synth --spec "$S" --json)")" = "14" ] || fail "xor2 synth cost"
[ "$(jq -r .total <<<"$("$BIN" multi path  --spec "$S" --json)")" = "12" ] || fail "xor2 path cost"
pass "xor2        not directly realizable; exact 12T / synth 14T / path 12T, verified"
