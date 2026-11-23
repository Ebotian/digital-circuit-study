#!/usr/bin/env bash
# Instance: 16-bit leading/trailing zero/one count, built from four 16-4
# priority encoders (pe16), proven structurally against a behavioural golden.
. "$(dirname "$0")/../../lib.sh"
D="$(dirname "$0")/../rtl"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat "$D/pe42.v" "$D/pe16.v" > "$tmp/atoms.v"
J=$("$BIN" multi synth --rtl "$D/lzcount16.v" --atoms "$tmp/atoms.v" \
    --ref "$D/gold_lzcount16.v" --json)
[ "$(jq -r .verified <<<"$J")" = "true" ] || fail "lzcount16 verify"
[ "$(jq -r .total <<<"$J")" = "866" ] || fail "lzcount16 cost"
[ "$(jq -r '.atoms[0].instances' <<<"$J")" = "4" ] || fail "lzcount16 encoders"
[ "$(jq -r '.atoms[0].transistors' <<<"$J")" = "172" ] || fail "lzcount16 atom cost"
if command -v verilator >/dev/null; then
  cp "$D/pe42.v" "$D/pe16.v" "$D/lzcount16.v" "$D/tb_lzcount16.v" "$tmp/"
  (cd "$tmp" && verilator --binary --language 1800-2017 -Wno-fatal \
      tb_lzcount16.v pe42.v pe16.v lzcount16.v -o tb >/dev/null 2>&1) \
    || fail "lzcount16 verilator build"
  "$tmp/obj_dir/tb" | grep -q "PASS lzcount16" || fail "lzcount16 simulation"
else
  if [ -n "${CI:-}" ]; then fail "verilator not installed"; fi
  echo "skip lzcount16 simulation (verilator not installed)"
fi
pass "lzcount16    16-bit LZ/LO/TZ/TO, 4x pe16(172T)+178T = 866T, miter PASS"
