#!/usr/bin/env bash
# Instance: 4-bit data + SEL -> 7-segment.
#   SEL=0: hex 0-F; SEL=1: decimal 0-9, blank for 10-15.
#   Automatic composition (spec -> BDD -> mux2) plus a structural miter
#   against an independently written case-statement reference.
. "$(dirname "$0")/../../lib.sh"
S="$(dirname "$0")/../specs/hex7seg_sel.json"
G="$(dirname "$0")/../rtl/hex7seg_sel_golden.v"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
C=$("$BIN" multi compose --spec "$S" --out "$TMP" --json)
[ "$(jq -r .verified <<<"$C")" = "true" ] || fail "hex7seg_sel compose verify"
[ "$(jq -r .total <<<"$C")" = "472" ] || fail "hex7seg_sel compose cost"
[ "$(jq -r '[.atoms[] | select(.module=="mux2") | .instances] | add' <<<"$C")" = "39" ] \
  || fail "hex7seg_sel mux2 count"
V=$("$BIN" multi synth --rtl "$TMP/compose.v" --atoms "$TMP/mux2.v" \
      --ref "$G" --json)
[ "$(jq -r .verified <<<"$V")" = "true" ] || fail "hex7seg_sel miter"
[ "$(jq -r .total <<<"$V")" = "472" ] || fail "hex7seg_sel synth cost"
pass "hex7seg_sel 4-bit hex/decimal 7-seg (SEL): 39x mux2 = 472T, miter PASS"
