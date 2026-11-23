#!/usr/bin/env bash
# Instance-level verification: end-to-end designs with known-answer costs.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
for t in decoder38 encoder42 prio42 nor2 xor2 xnor2 prio16; do bash "$here/$t.sh"; done
echo "instances: all passed"
