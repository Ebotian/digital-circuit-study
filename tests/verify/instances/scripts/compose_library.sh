#!/usr/bin/env bash
# Library interface: exact whole-function match against a supplied atom library.
. "$(dirname "$0")/../../lib.sh"
J=$("$BIN" multi compose --spec "$(dirname "$0")/../specs/pe42.json" \
      --library "$(dirname "$0")/../specs/library.json" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "library match verify"
[ "$(jq -r .total <<<"$J")" = "16" ] || fail "library match cost"
[ "$(jq -r '.atoms[0].module' <<<"$J")" = "pe42" ] || fail "library atom"
pass "compose_library  pe42 spec + library -> 1x pe42 = 16T, miter PASS"
