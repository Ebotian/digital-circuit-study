# Benchmarks

Modular measurement of the CLI and the C++ core.  The suites are deliberately
small and fixed so results are comparable across runs.

## Suites

| suite | what it exercises | dependencies |
|---|---|---|
| `core` | `bool info` / `bool count` (Bits, BoolFunc) | none |
| `exact` | `multi exact` on encoder42 / hex7seg (DNF search, switch-level) | none (Z3 worker optional) |
| `compose` | `multi compose` on mux4_3bit / prio8 + library | none |
| `synth` | `multi synth` decoder38 (Yosys/ABC mapping) | Yosys + Nangate45 |

```bash
bench/run.sh --list
bench/run.sh --suite exact
bench/run.sh --suite all --runs 20
bench/run.sh --suite all --save          # update baselines
```

`bench/compare.py` compares medians against `bench/baselines/<suite>.json`
and fails when a workload regresses by more than the threshold (default 20%).

## Measurement hygiene

- `hyperfine --warmup/--min-runs`: medians, not single runs.
- Governor is `powersave` on this machine; use
  `sudo cpupower frequency-set -g performance` for low-noise runs.
- Pin with `taskset -c 2 bench/run.sh ...` when comparing numbers.
- `synth` is dominated by Yosys/ABC; do not read it as a C++ benchmark.

## Other tools

```bash
make bench-perf      # perf stat (user-space counters)
make bench-profile   # callgrind on multi compose
make bench-micro     # in-process BoolFunc/Bits microbench
make bench-size      # bloaty sections
```

`perf record` needs `kernel.perf_event_paranoid <= 1`; `callgrind` works unprivileged.

## Adding a workload

1. Add a spec/fixture under `bench/fixtures/` (or reuse `tests/verify/instances/specs`).
2. Add the command to the matching `suite_commands` branch in `run.sh`.
3. Run `bench/run.sh --suite <suite> --save` once to establish the baseline.
