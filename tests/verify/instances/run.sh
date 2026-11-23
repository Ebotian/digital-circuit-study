#!/usr/bin/env bash
# Instance-level verification: end-to-end designs with known-answer costs.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
for t in decoder38 encoder42 hex7seg prio42 nor2 xor2 xnor2 prio16 lzcount16; do bash "$here/$t.sh"; done
# Same instance level, different verification layer: dynamic simulation.
bash "$here/simulation/verilator-xor6/run.sh"
echo "instances: all passed"
