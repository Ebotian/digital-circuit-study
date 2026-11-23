#!/usr/bin/env bash
# Automatic composition: 3-bit 4-to-1 mux, directly from its 14-input spec.
# The BDD is lowered to mux2 atoms; the mux2 network is mapped and proved
# against the spec's rows (exhaustive self-check + structural miter).
. "$(dirname "$0")/../../lib.sh"
J=$("$BIN" multi compose --spec "$(dirname "$0")/../specs/mux4_3bit.json" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "compose mux4_3bit verify"
[ "$(jq -r .nodes <<<"$J")" = "9" ] || fail "compose mux4_3bit nodes"
[ "$(jq -r .total <<<"$J")" = "108" ] || fail "compose mux4_3bit cost"
[ "$(jq -r '.atoms[0].instances' <<<"$J")" = "9" ] || fail "compose mux4_3bit atoms"
[ "$(jq -r '.atoms[0].transistors' <<<"$J")" = "12" ] || fail "compose mux4_3bit atom cost"
pass "compose_mux4_3bit  14-input spec -> 9x mux2 = 108T, miter PASS"
