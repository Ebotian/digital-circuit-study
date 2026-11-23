#!/usr/bin/env bash
# Formal optional: full CLI + switch-level + Z3 + Yosys checks.
# Skips the parts whose dependencies are not installed (fails under CI=1).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BIN="$ROOT/build/dcstudy" bash "$ROOT/tests/integration.sh"
