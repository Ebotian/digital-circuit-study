#!/usr/bin/env bash
# Modular benchmark driver.  Suites are dependency-light unless noted.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT/build/dcstudy}"
SUITE="all"
SAVE=0
RUNS=10
WARMUP=3
THRESHOLD="0.20"

usage() {
  cat <<EOF
usage: bench/run.sh [options]
  --suite NAME   core | exact | compose | synth | all  (default: all)
  --runs N       hyperfine min-runs (default: 10)
  --warmup N     hyperfine warmup runs (default: 3)
  --threshold F  regression threshold, e.g. 0.20 (default)
  --save         copy results to bench/baselines/
  --list         list suites and commands
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --suite|-s) SUITE="$2"; shift 2 ;;
    --runs) RUNS="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --threshold) THRESHOLD="$2"; shift 2 ;;
    --save) SAVE=1; shift ;;
    --list) SUITE=list; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

[ -x "$BIN" ] || { echo "missing $BIN (run make)" >&2; exit 1; }
command -v hyperfine >/dev/null || { echo "hyperfine not installed" >&2; exit 1; }

exact_spec="$ROOT/tests/verify/instances/specs"
fixtures="$ROOT/bench/fixtures"

suite_commands() {
  case "$1" in
    core)
      printf '%s\n' \
        "$BIN bool info --expr 'A^B' --json" \
        "$BIN bool count --n 5 --json"
      ;;
    exact)
      printf '%s\n' \
        "$BIN multi exact --spec $exact_spec/encoder42.json --json" \
        "$BIN multi exact --spec $exact_spec/hex7seg.json --json"
      ;;
    compose)
      printf '%s\n' \
        "$BIN multi compose --spec $exact_spec/mux4_3bit.json --json" \
        "$BIN multi compose --spec $fixtures/prio8.json --library $exact_spec/library.json --json"
      ;;
    synth)
      printf '%s\n' \
        "$BIN multi synth --spec $exact_spec/decoder38.json --json"
      ;;
    *) return 1 ;;
  esac
}

suites=(
  core
  exact
  compose
  synth
)

if [ "$SUITE" = "list" ]; then
  for s in "${suites[@]}"; do
    echo "[$s]"
    suite_commands "$s"
  done
  exit 0
fi

run_one() {
  local s="$1"
  local out="$ROOT/bench/results/$s.json"
  mapfile -t cmds < <(suite_commands "$s")
  echo "== $s (${#cmds[@]} workloads)"
  hyperfine --warmup "$WARMUP" --min-runs "$RUNS" --export-json "$out" "${cmds[@]}"
  # Drop the per-iteration arrays; only medians/min/max are needed.
  python3 - "$out" <<'SHRINK'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
for r in d.get("results", []):
    r.pop("measurements", None)
json.dump(d, open(p, "w"), indent=2)
SHRINK
  if [ -f "$ROOT/bench/baselines/$s.json" ]; then
    python3 "$ROOT/bench/compare.py" "$ROOT/bench/baselines/$s.json" "$out" "$THRESHOLD"
  fi
  if [ "$SAVE" = "1" ]; then
    cp "$out" "$ROOT/bench/baselines/$s.json"
    echo "saved baseline: bench/baselines/$s.json"
  fi
}

if [ "$SUITE" = "all" ]; then
  for s in "${suites[@]}"; do
    if [ "$s" = "synth" ] && [ ! -d "${NANGATE45_LIB:-$HOME/yosys-sta/pdk/nangate45}" ]; then
      echo "skip synth (Nangate45 PDK not found)"
      continue
    fi
    run_one "$s"
  done
else
  run_one "$SUITE"
fi
