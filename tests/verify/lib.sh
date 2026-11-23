#!/usr/bin/env bash
# Shared helpers for the verification directory.
set -euo pipefail
ROOT="${ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
BIN="${BIN:-$ROOT/build/dcstudy}"
fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok   $*"; }
need() { command -v "$1" >/dev/null || fail "missing dependency: $1"; }
[ -x "$BIN" ] || fail "missing $BIN (run make)"
need jq
