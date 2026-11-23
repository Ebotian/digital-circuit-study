#!/usr/bin/env bash
# Instance: 3-to-8 decoder, spec only.
. "$(dirname "$0")/../lib.sh"
J=$("$BIN" multi synth --spec "$ROOT/specs/decoder38.json" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "decoder38 verify"
[ "$(jq -r .total <<<"$J")" = "54" ] || fail "decoder38 cost"
pass "decoder38   3->8 decoder, synth 54T, verified"
