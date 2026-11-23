#!/usr/bin/env bash
# Verilator regression for the switch-level 6T XOR model.
# Positive control: 4/4 rows must match A^B with ok=1.
# Negative control: the injected fault (N3 gate tied to GND) must be reported.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
skip_or_fail() {
  if [ -n "${CI:-}" ]; then echo "FAIL: $*" >&2; exit 1; fi
  echo "skip $*"; exit 0
}
command -v verilator >/dev/null || skip_or_fail "verilator not installed"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cp "$here/tb_xor6.v" "$here/xor6.v" "$tmp/"
if ! (cd "$tmp" && verilator --binary --language 1800-2017 -Wno-fatal \
        tb_xor6.v xor6.v -o tb >/dev/null 2>&1); then
  echo "FAIL: verilator build" >&2
  exit 1
fi
out="$(cd "$tmp" && ./obj_dir/tb)"
if ! grep -q "正例失败 0 / 4 行" <<<"$out"; then
  echo "$out" >&2; echo "FAIL: positive control" >&2; exit 1
fi
if ! grep -q "负例失败 [1-9]" <<<"$out"; then
  echo "$out" >&2; echo "FAIL: negative control not detected" >&2; exit 1
fi
echo "ok   verilator-xor6  switch-level XOR6: 4/4 positive, injected fault detected"
