#!/usr/bin/env bash
# Verification entry point.
#   run.sh instances    instance-level designs (fast)
#   run.sh formal       formal core (unit/property)
#   run.sh optional     formal optional (CLI/switch-level/Z3/Yosys)
#   run.sh all          all of the above
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
case "${1:-all}" in
  instances) bash "$here/instances/run.sh" ;;
  formal)    bash "$here/formal/core.sh" ;;
  optional)  bash "$here/formal/optional.sh" ;;
  all)
    bash "$here/instances/run.sh"
    bash "$here/formal/core.sh"
    bash "$here/formal/optional.sh"
    ;;
  *) echo "usage: $0 [instances|formal|optional|all]" >&2; exit 2 ;;
esac
