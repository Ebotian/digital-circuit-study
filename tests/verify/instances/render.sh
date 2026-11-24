#!/usr/bin/env bash
# Optional render sweep: one schematic per instance under out/render/.
# Not part of `make ci`; invoke with `make render` or directly.
. "$(dirname "$0")/../lib.sh"
OUT="${OUT:-$ROOT/out/render}"
mkdir -p "$OUT"
need rsvg-convert

spec() {
  local name=$1 file=$2 dir="$OUT/$1"
  shift 2
  mkdir -p "$dir"
  "$BIN" multi synth --spec "$file" "$@" --out "$dir" --png --json >"$dir/result.json"
  pass "render $name (synth)"
}

compose() {
  local name=$1 file=$2 dir="$OUT/$1"
  shift 2
  mkdir -p "$dir"
  "$BIN" multi compose --spec "$file" "$@" --out "$dir" --png --json >"$dir/result.json"
  pass "render $name (compose)"
}

rtl() {
  local name=$1 top=$2 ref=$3 dir="$OUT/$1"
  shift 3
  mkdir -p "$dir"
  local tmp
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' RETURN
  cat "$@" >"$tmp/atoms.v"
  local cost
  cost=$("$BIN" multi synth --rtl "$top" --atoms "$tmp/atoms.v" \
    --ref "$ref" --json | jq -r .total)
  "$BIN" render --netlist "$top" --atoms "$tmp/atoms.v" --top top \
    --cost "$cost" --out "$dir" --png >/dev/null
  rm -rf "$tmp"
  pass "render $name (rtl hierarchy, ${cost}T)"
}

for c in "$@"; do
  case "$c" in
    decoder38) spec "$c" "$ROOT/tests/verify/instances/specs/decoder38.json" ;;
    encoder42) spec "$c" "$ROOT/tests/verify/instances/specs/encoder42.json" ;;
    eq4) spec "$c" "$ROOT/tests/verify/instances/specs/eq4.json" ;;
    hex7seg) spec "$c" "$ROOT/tests/verify/instances/specs/hex7seg.json" ;;
    hex7seg_sel) spec "$c" "$ROOT/tests/verify/instances/specs/hex7seg_sel.json" ;;
    pe42) spec "$c" "$ROOT/tests/verify/instances/specs/pe42.json" ;;
    nor2) spec "$c" "$ROOT/tests/verify/instances/specs/nor2.json" ;;
    xor2) spec "$c" "$ROOT/tests/verify/instances/specs/xor2.json" ;;
    xnor2) spec "$c" "$ROOT/tests/verify/instances/specs/xnor2.json" ;;
    mux2) spec "$c" "$ROOT/tests/verify/instances/specs/mux2.json" ;;
    mux4_3bit) compose "$c" "$ROOT/tests/verify/instances/specs/mux4_3bit.json" ;;
    prio8)
      compose "$c" "$ROOT/bench/fixtures/prio8.json" \
        --library "$ROOT/tests/verify/instances/specs/library.json" ;;
    prio16)
      rtl "$c" "$ROOT/tests/verify/instances/rtl/top16.v" \
        "$ROOT/tests/verify/instances/rtl/gold16.v" \
        "$ROOT/tests/verify/instances/rtl/pe42.v" ;;
    lzcount16)
      rtl "$c" "$ROOT/tests/verify/instances/rtl/lzcount16.v" \
        "$ROOT/tests/verify/instances/rtl/gold_lzcount16.v" \
        "$ROOT/tests/verify/instances/rtl/pe16.v" \
        "$ROOT/tests/verify/instances/rtl/pe42.v" ;;
    *) fail "no render recipe for instance: $c" ;;
  esac
done
