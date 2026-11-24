#!/usr/bin/env python3
"""Compare two hyperfine JSON files; report median deltas and regressions."""
import json
import sys


def load(path):
    data = json.load(open(path))
    out = {}
    for r in data.get("results", []):
        out[r["command"]] = r
    return out


def main():
    if len(sys.argv) < 3:
        print("usage: compare.py BASELINE.json CURRENT.json [threshold]", file=sys.stderr)
        return 2
    threshold = float(sys.argv[3]) if len(sys.argv) > 3 else 0.20
    base = load(sys.argv[1])
    cur = load(sys.argv[2])
    regressions = 0
    print(f"{'workload':<60} {'base ms':>10} {'cur ms':>10} {'delta':>8}")
    for cmd, b in base.items():
        c = cur.get(cmd)
        if not c:
            continue
        bm = b["summary"]["time_wall_clock"]["median"] * 1000.0
        cm = c["summary"]["time_wall_clock"]["median"] * 1000.0
        delta = (cm - bm) / bm if bm else 0.0
        flag = ""
        if delta > threshold:
            flag = "  REGRESSION"
            regressions += 1
        short = cmd if len(cmd) <= 58 else cmd[:55] + "..."
        print(f"{short:<60} {bm:10.2f} {cm:10.2f} {delta:+7.1%}{flag}")
    if regressions:
        print(f"{regressions} workload(s) above +{threshold:.0%} threshold")
        return 1
    print("no regression above threshold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
