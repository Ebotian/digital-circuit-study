#!/usr/bin/env bash
# In-process microbench for the dependency-free core (BoolFunc/Bits).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build/micro_bench"
mkdir -p "$ROOT/build"
clang++ -std=c++20 -O2 -I"$ROOT/include" \
  "$ROOT/bench/micro/micro_bench.cpp" "$ROOT/src/core/boolfunc.cpp" -o "$OUT"
"$OUT"
