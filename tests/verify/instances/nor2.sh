#!/usr/bin/env bash
# Instance: NOR2, the "directly realizable" case (one complementary gate).
. "$(dirname "$0")/../lib.sh"
[ "$("$BIN" bool info --expr '~(A|B)' --json | jq -r .realizable)" = "true" ] \
  || fail "nor2 realizable"
for m in exact synth path; do
  J=$("$BIN" multi "$m" --spec "$(dirname "$0")/nor2.json" --json)
  [ "$(jq -r .verified <<<"$J")" = "true" ] || fail "nor2 $m verify"
  [ "$(jq -r .total <<<"$J")" = "4" ] || fail "nor2 $m cost"
done
pass "nor2        single static gate, exact/synth/path 4T (realizable)"
