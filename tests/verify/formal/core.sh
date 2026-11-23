#!/usr/bin/env bash
# Formal core: unit/property tests (no external tools beyond the build).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
ctest --test-dir "$ROOT/build" --output-on-failure
