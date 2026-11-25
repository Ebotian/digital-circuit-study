#!/usr/bin/env bash
# Instance: 4-bit data + SEL -> 7-segment.
#   SEL=0: hex 0-F; SEL=1: decimal 0-9, blank for 10-15.
#   Spec (5 inputs) -> multi-stage static CMOS, plus the native SVG render.
. "$(dirname "$0")/../../lib.sh"
S="$(dirname "$0")/../specs/hex7seg_sel.json"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
J=$("$BIN" multi synth --spec "$S" --out "$TMP" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "hex7seg_sel verify"
# ABC mapping drifts between Yosys versions (232T on 0.66, 252T on 0.52), so
# assert the fan-in contract and a budget bound, not the exact cell count.
[ "$(jq -r '.total <= 300' <<<"$J")" = "true" ] || fail "hex7seg_sel cost"
[ "$(jq -r '.cells <= 48' <<<"$J")" = "true" ] || fail "hex7seg_sel cells"
[ -f "$TMP/scheme-0.svg" ] || fail "hex7seg_sel render"
pass "hex7seg_sel 4-bit hex/decimal 7-seg (SEL): <=48 cells, <=300T, rendered"
