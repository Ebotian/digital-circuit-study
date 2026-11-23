#!/usr/bin/env bash
# Fast CLI smoke test: the binary starts and the core paths answer.
set -euo pipefail
ROOT="${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
BIN="${BIN:-$ROOT/build/dcstudy}"
[ -x "$BIN" ] || { echo "missing $BIN (run make)" >&2; exit 1; }
command -v jq >/dev/null || { echo "missing jq" >&2; exit 1; }
"$BIN" --help >/dev/null
[ "$("$BIN" bool info --expr 'A^B' --json | jq -r .truth)" = "0110" ] \
  || { echo "bool info smoke failed" >&2; exit 1; }
[ "$("$BIN" multi synth --spec "$ROOT/tests/verify/instances/specs/encoder42.json" \
      --json | jq -r .total)" = "8" ] \
  || { echo "multi synth smoke failed" >&2; exit 1; }
[ "$("$BIN" multi exact --spec "$ROOT/tests/verify/instances/specs/encoder42.json" \
      --json | jq -r .verified)" = "true" ] \
  || { echo "multi exact smoke failed" >&2; exit 1; }
echo "ok   cli smoke (help, bool info, multi synth/exact)"
