#!/usr/bin/env bash
# Black-box CLI tests.  Requires jq; Yosys/Verilator are used for the formal
# checks, and the CDL checks are skipped when Nangate45 is not installed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT/build/dcstudy}"
CDL="${NANGATE45_CDL:-$HOME/yosys-sta/pdk/nangate45/cdl/NangateOpenCellLibrary.cdl}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok   $*"; }

[ -x "$BIN" ] || fail "missing $BIN (run make)"
command -v jq >/dev/null || fail "jq is required for integration tests"

# Boolean functions and exact covers.
[ "$("$BIN" bool info --expr 'A^B' --json | jq -r .truth)" = "0110" ] || fail "xor truth"
[ "$("$BIN" bool info --expr 'A^B' --json | jq -r .realizable)" = "false" ] || fail "xor realisable"
[ "$("$BIN" bool info --tt 0110 --json | jq -r .truth)" = "0110" ] || fail "truth-table input"
[ "$("$BIN" bool count --n 5 --json | jq -r .count)" = "7581" ] || fail "Dedekind n=5"
[ "$(echo '{"op":"ping"}' | "$ROOT/build/dcstudy-z3" | jq -r .version)" = "1" ] || fail "Z3 worker ping"
[ "$("$BIN" math closure --n 3 --edges 0-1,1-2 --json | jq -r '.rows[0] | join(",")')" = "0,1,2" ] || fail "math closure"
[ "$("$BIN" math gf2 --rows '1,1' --json | jq -r '.rank')" = "1" ] || fail "math gf2 rank"
[ "$("$BIN" math gf2 --rows '1,1' --json | jq -r '.nullity')" = "1" ] || fail "math gf2 nullity"
pass "boolean functions + math worker"

[ "$("$BIN" cmos synth --expr '~(A&B)' --json | jq -r .total)" = "4" ] || fail "NAND2 4T"
[ "$("$BIN" cmos synth --expr '~(A|B)' --json | jq -r .total)" = "4" ] || fail "NOR2 4T"
[ "$("$BIN" cmos synth --expr 'A&B' --json | jq -r .realizable)" = "false" ] || fail "AND2 not single-stage"
[ "$("$BIN" cmos synth --expr 'A^B' --json | jq -r .realizable)" = "false" ] || fail "XOR not single-stage"
pass "complementary CMOS"

# Switch-level analysis, if the PDK is available.
if [ -f "$CDL" ]; then
  [ "$("$BIN" cdl solve --cdl "$CDL" --cell XOR2_X1 --json | jq -r .truth.Z)" = "0110" ] || fail "CDL XOR2"
  [ "$("$BIN" cdl solve --cdl "$CDL" --cell XNOR2_X1 --exact --json | jq -r .truth.ZN)" = "1001" ] || fail "CDL XNOR2 exact"
  [ "$("$BIN" cdl solve --cdl "$CDL" --cell NAND2_X1 --json | jq -r .truth.ZN)" = "1110" ] || fail "CDL NAND2"
  [ "$("$BIN" cdl solve --cdl "$CDL" --cell XOR2_X1 --graphblas --json | jq -r .truth.Z)" = "0110" ] || fail "CDL XOR2 GraphBLAS"
  pass "CDL switch level"
else
  echo "skip CDL checks (no $CDL)"
fi

# Minimum pass network + Yosys SAT proof.
"$BIN" design min --expr 'A^B' --max-cost 6 --max-chain 3 --limit 1 \
  --verify yosys --json > "$TMP/xor.json" || fail "design xor verification"
[ "$(jq -r .optimum "$TMP/xor.json")" = "6" ] || fail "XOR2 optimum"
[ "$(jq -r '.designs[0].verification.yosys.ok' "$TMP/xor.json")" = "true" ] || fail "XOR2 proof"
"$BIN" design min --expr '~(A&B)' --max-cost 6 --max-chain 3 --all --canonical \
  --verify yosys --json > "$TMP/nand.json" || fail "NAND2 canonical proofs"
[ "$(jq -r '[.designs[].verification.yosys.ok] | all' "$TMP/nand.json")" = "true" ] || fail "NAND2 proof"
pass "minimum design + Yosys"

# General path construction: BDD-derived switch DAG.
"$BIN" design bdd --expr 'A^B' --json > "$TMP/bdd.json" || fail "BDD switch construction"
[ "$(jq -r .verified "$TMP/bdd.json")" = "true" ] || fail "BDD switch verification"
[ "$(jq -r '.graph.edges | length' "$TMP/bdd.json")" = "6" ] || fail "BDD edge count"

# TG/NP quotient + input-automorphism orbit blocking.
"$BIN" design min --expr 'A^B' --max-cost 6 --max-chain 3 --all --canonical \
  --limit 8 --verify none --json > "$TMP/canon.json" || fail "canonical XOR"
[ "$(jq -r '.designs | length' "$TMP/canon.json")" = "1" ] || fail "XOR canonical class count"
  [ "$(jq -r '.designs[0].drivers.all_single' "$TMP/canon.json")" = "true" ] || fail "XOR dynamic driver exclusivity"
  [ "$(jq -r '.designs[0].drivers.max_drivers' "$TMP/canon.json")" = "1" ] || fail "XOR dynamic driver count"
  [ "$(jq -r '.designs[0].drivers.valid' "$TMP/canon.json")" = "true" ] || fail "XOR dynamic path validity"
"$BIN" design min --expr 'A^B' --max-cost 6 --max-chain 3 --all \
  --limit 8 --verify none --json > "$TMP/raw.json" || fail "raw XOR"
[ "$(jq -r '.designs | length' "$TMP/raw.json")" = "2" ] || fail "XOR raw mirror count"
pass "symmetry quotient"

# Static complementary CMOS flag: monotone-complement + kappa-linear netlist.
"$BIN" design min --expr '~(A&B)' --static-cmos --verify static \
  --json > "$TMP/static.json" || fail "static CMOS NAND"
[ "$(jq -r .space "$TMP/static.json")" = "static-cmos" ] || fail "static space"
[ "$(jq -r .optimum "$TMP/static.json")" = "4" ] || fail "static NAND 4T"
[ "$(jq -r .verification.static.ok "$TMP/static.json")" = "true" ] || fail "static switch-level"
"$BIN" design min --expr 'A^B' --static-cmos --verify static \
  --json > "$TMP/static-xor.json" || fail "multi-stage static XOR"
[ "$(jq -r .single_stage "$TMP/static-xor.json")" = "false" ] || fail "static XOR single-stage flag"
[ "$(jq -r .multi_stage "$TMP/static-xor.json")" = "true" ] || fail "static XOR multi-stage flag"
[ "$(jq -r .optimum "$TMP/static-xor.json")" = "14" ] || fail "static XOR cost"
[ "$(jq -r .verification.static.ok "$TMP/static-xor.json")" = "true" ] || fail "static XOR switch-level"
"$BIN" design min --expr '~(A&B&C)' --vars A,B,C --static-cmos --verify static \
  --json > "$TMP/nand3.json" || fail "static NAND3"
[ "$(jq -r .optimum "$TMP/nand3.json")" = "6" ] || fail "static NAND3 6T"
[ "$(jq -r .single_stage "$TMP/nand3.json")" = "true" ] || fail "static NAND3 single-stage"
"$BIN" design min --expr '~(A&B&C)' --vars A,B,C --static-cmos --max-fanin 2 \
  --verify static --json > "$TMP/nand3-10.json" || fail "static NAND3 fanin2"
[ "$(jq -r .optimum "$TMP/nand3-10.json")" = "10" ] || fail "static NAND3 10T"
[ "$(jq -r '[.designs[0].cells[].type] | sort | join(",")' "$TMP/nand3-10.json")" = "AND2_X1,NAND2_X1" ] || fail "NAND3 10T cells"
[ "$(jq -r .verification.static.ok "$TMP/nand3-10.json")" = "true" ] || fail "NAND3 10T switch-level"
pass "static CMOS constraint"

# General multi-output synthesis: the 3-to-8 decoder is only a spec file.
if [ -f "$CDL" ] && command -v yosys >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then
  cat > "$TMP/decoder38.json" <<'JSON'
{
  "name": "decoder3to8",
  "inputs": ["A", "B", "C"],
  "outputs": [
    {"name": "Y0", "expr": "~A&~B&~C"},
    {"name": "Y1", "expr": "~A&~B&C"},
    {"name": "Y2", "expr": "~A&B&~C"},
    {"name": "Y3", "expr": "~A&B&C"},
    {"name": "Y4", "expr": "A&~B&~C"},
    {"name": "Y5", "expr": "A&~B&C"},
    {"name": "Y6", "expr": "A&B&~C"},
    {"name": "Y7", "expr": "A&B&C"}
  ]
}
JSON
  "$BIN" multi synth --spec "$TMP/decoder38.json" --out "$TMP/decoder38"     --json > "$TMP/decoder38.out" || fail "multi-output decoder"
  [ "$(jq -r .verified "$TMP/decoder38.out")" = "true" ] || fail "multi-output verify"
  [ "$(jq -r .total "$TMP/decoder38.out")" = "54" ] || fail "decoder 54T"
  [ "$(jq -r '.truths.Y0' "$TMP/decoder38.out")" = "10000000" ] || fail "decoder Y0"
  [ "$(jq -r '.truths.Y7' "$TMP/decoder38.out")" = "00000001" ] || fail "decoder Y7"
  "$BIN" multi exact --spec "$TMP/decoder38.json" --json > "$TMP/decoder38.exact"     || fail "multi-output exact"
  [ "$(jq -r .optimal "$TMP/decoder38.exact")" = "54" ] || fail "decoder exact 54T"
  [ "$(jq -r .proof "$TMP/decoder38.exact")" = "true" ] || fail "decoder optimality proof"
  [ "$(jq -r .verified "$TMP/decoder38.exact")" = "true" ] || fail "decoder exact verification"
  [ "$(jq -r .nfet "$TMP/decoder38.exact")" = "27" ] || fail "decoder exact NMOS"
  [ "$(jq -r '.inverted | length' "$TMP/decoder38.exact")" = "3" ] || fail "decoder exact inverters"
  [ -f "$TMP/decoder38/index.html" ] || fail "decoder render index"
  [ -f "$TMP/decoder38/scheme-0.svg" ] || fail "decoder schematic"
  pass "multi-output circuit (decoder as spec)"
else
  echo "skip multi-output checks (no CDL/yosys/node)"
fi

# Automated schematic rendering (TS/Node worker by default).
if command -v node >/dev/null 2>&1; then
  "$BIN" render --expr '~(A^B)' --max-cost 6 --max-chain 3 --all --canonical \
    --out "$TMP/render" --json > "$TMP/render.json" || fail "render"
  [ "$(jq -r .ok "$TMP/render.json")" = "true" ] || fail "render ok"
  [ -f "$TMP/render/index.html" ] || fail "render index"
  [ -f "$TMP/render/scheme-0.svg" ] || fail "render svg"
  [ -f "$TMP/render/logic.svg" ] || fail "render logic svg"
  if command -v python3 >/dev/null 2>&1; then
    python3 - "$TMP/render" <<'PY' || fail "render SVG XML"
import pathlib, sys, xml.etree.ElementTree as ET
for p in sorted(pathlib.Path(sys.argv[1]).glob("*.svg")):
    ET.parse(p)
PY
  fi
  "$BIN" render --expr '~(A&B)' --static-cmos \
    --out "$TMP/static-render" --json > "$TMP/static-render.json" || fail "static render"
  [ "$(jq -r '.designs[0].kind' "$TMP/static-render.json")" = "static-cmos" ] || fail "static render kind"
  [ -f "$TMP/static-render/scheme-0.svg" ] || fail "static schematic"
  [ -f "$TMP/static-render/logic.svg" ] || fail "static logic"
  "$BIN" render --expr 'A^B' --static-cmos \
    --out "$TMP/static-xor-render" --json > "$TMP/static-xor-render.json" || fail "static XOR render"
  [ "$(jq -r '.designs[0].kind' "$TMP/static-xor-render.json")" = "static-cmos-multi" ] || fail "static XOR render kind"
  [ -f "$TMP/static-xor-render/scheme-0.svg" ] || fail "static XOR schematic"
  if command -v python3 >/dev/null 2>&1; then
    python3 - "$TMP/static-render" "$TMP/static-xor-render" <<'PY' || fail "static render SVG XML"
import pathlib, sys, xml.etree.ElementTree as ET
for root in sys.argv[1:]:
    for p in sorted(pathlib.Path(root).glob("*.svg")):
        ET.parse(p)
PY
  fi
  pass "render"
else
  echo "skip render (no node)"
fi

# A wrong DUT must be rejected (negative control).
cat > "$TMP/bad.v" <<'EOF'
module dut(input logic A, B, output logic Y, output logic ok);
  assign Y = A | B;
  assign ok = 1'b1;
endmodule
EOF
if "$BIN" verify equiv --dut "$TMP/bad.v" --expr 'A^B' >/dev/null 2>&1; then
  fail "bad DUT was accepted"
fi
pass "negative control"

# Pipeline reproduces the lecture numbers.
"$BIN" pipeline --funcs 'xor2=A^B,nand2=~(A&B),and2=A&B' \
  --max-cost 6 --max-chain 3 --verify yosys --out "$TMP/cands" \
  --json > "$TMP/pipe.json" || fail "pipeline"
[ "$(jq -r '.runs[] | select(.name=="xor2") | .optimum' "$TMP/pipe.json")" = "6" ] || fail "pipeline xor2"
[ "$(jq -r '.runs[] | select(.name=="nand2") | .optimum' "$TMP/pipe.json")" = "4" ] || fail "pipeline nand2"
[ "$(jq -r '.runs[] | select(.name=="and2") | .optimum' "$TMP/pipe.json")" = "5" ] || fail "pipeline and2"
[ -f "$TMP/cands/xor2_0/dut.v" ] || fail "pipeline emitted no DUT"
pass "pipeline"

# JSON errors keep the same interface.
rc=0
"$BIN" bool info --expr >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "usage error exit code ($rc)"
pass "CLI error handling"

echo "integration: all checks passed"
