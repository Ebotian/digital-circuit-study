#!/usr/bin/env bash
# Fast CLI smoke test: the binary starts and the dependency-free paths answer.
# (multi synth needs Yosys/PDK and multi exact needs the Z3 worker; those are
# covered by the full local suite, not by this smoke test.)
set -euo pipefail
ROOT="${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
BIN="${BIN:-$ROOT/build/dcstudy}"
[ -x "$BIN" ] || { echo "missing $BIN (run make)" >&2; exit 1; }
command -v jq >/dev/null || { echo "missing jq" >&2; exit 1; }
"$BIN" --help >/dev/null
[ "$("$BIN" bool info --expr 'A^B' --json | jq -r .truth)" = "0110" ] \
  || { echo "bool info smoke failed" >&2; exit 1; }
[ "$("$BIN" bool info --expr 'A^B' --json | jq -r .realizable)" = "false" ] \
  || { echo "bool realizable smoke failed" >&2; exit 1; }
[ "$("$BIN" bool count --n 5 --json | jq -r .count)" = "7581" ] \
  || { echo "bool count smoke failed" >&2; exit 1; }
[ "$("$BIN" bool info --tt 0110 --json | jq -r .truth)" = "0110" ] \
  || { echo "bool truth-table smoke failed" >&2; exit 1; }
echo "ok   cli smoke (help, bool info/count/truth-table)"
