#!/usr/bin/env bash
# Automatic library composition: 8-3 priority encoder from pe42/or4/mux2.
. "$(dirname "$0")/../../lib.sh"
J=$("$BIN" multi compose --spec "$ROOT/bench/fixtures/prio8.json" \
      --library "$(dirname "$0")/../specs/library.json" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "compose prio8 verify"
[ "$(jq -r .total <<<"$J")" = "66" ] || fail "compose prio8 cost"
[ "$(jq -r '[.atoms[] | select(.module=="pe42") | .instances] | add' <<<"$J")" = "2" ] \
  || fail "compose prio8 pe42 count"
[ "$(jq -r '[.atoms[] | select(.module=="mux2") | .instances] | add' <<<"$J")" = "2" ] \
  || fail "compose prio8 mux2 count"
[ "$(jq -r '[.atoms[] | select(.module=="or4") | .instances] | add' <<<"$J")" = "1" ] \
  || fail "compose prio8 or4 count"
pass "compose_prio8  8-3 = 2x pe42 + or4 + 2x mux2 = 66T, miter PASS"
