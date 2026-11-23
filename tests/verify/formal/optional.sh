#!/usr/bin/env bash
# Formal optional: full CLI + switch-level + Z3 + Yosys checks.
# Skips the parts whose dependencies are not installed (fails under CI=1).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
# Standalone Verilator regression (switch-level XOR6, positive + negative).
bash "$ROOT/tests/verify/formal/optional/verilator-xor6/run.sh"

BIN="$ROOT/build/dcstudy" bash "$ROOT/tests/integration.sh"
