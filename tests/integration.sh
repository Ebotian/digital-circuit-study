#!/usr/bin/env bash
# Black-box CLI tests.  Requires jq; Yosys/Verilator are used for the formal
# checks, and the CDL checks are skipped when Nangate45 is not installed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT/build/dcstudy}"
CDL="${NANGATE45_CDL:-$HOME/yosys-sta/pdk/nangate45/cdl/NangateOpenCellLibrary.cdl}"
TMP="$(mktemp -d)"
SPEC="$ROOT/tests/verify/instances"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "ok   $*"; }
# Missing dependency: skip locally, but never let CI go green without running
# the check (a silent skip once hid a broken multi-output verification).
skip_or_fail() {
  if [ -n "${CI:-}" ]; then fail "${1:-missing dependency}"; fi
  echo "skip $*"
}

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
# Hex truth tables wider than 64 rows used to set bit 64 too (shift UB),
# and over-wide hex was silently truncated before the width check.
[ "$("$BIN" bool info --tt 0x1 --vars A,B,C,D,E,F,G --json | jq -r '[.truth | explode[] | select(. == 49)] | length')" = "1" ] || fail "hex truth width"
rc=0
"$BIN" bool info --tt 0x10000000000000000 --vars A,B --json >/dev/null 2>&1 || rc=$?
[ "$rc" = "2" ] || fail "over-wide hex accepted"
# 64 variables must hit the arity check before 1 << n is evaluated.
vars=$(seq -s, -f 'V%g' 0 63)
rc=0
"$BIN" bool info --tt 0x1 --vars "$vars" --json >/dev/null 2>&1 || rc=$?
[ "$rc" = "2" ] || fail "64-variable truth table accepted"

# A floating gate that can turn on both the pull-up and the pull-down must be
# reported as contention, not as the one value the surviving states imply.
cat > "$TMP/race.cdl" <<'CDL'
.SUBCKT RACE Y A VDD VSS
*.PININFO Y:O A:I VDD:P VSS:G
M1 Y A VDD VDD PMOS
M2 Y F VSS VSS NMOS
M3 x A VSS VSS NMOS
.ENDS
CDL
[ "$("$BIN" cdl solve --cdl "$TMP/race.cdl" --cell RACE --inputs A --exact --json | jq -r .truth.Y)" = "XZ" ] || fail "floating gate race"
pass "boolean functions + math worker"

# A memory cap must turn a runaway search into a clean error.  n=6 Dedekind
# needs about 10 GB, so 256 MiB has to stop it in-process, with exit code 2,
# instead of letting the machine swap and then die.
if DCSTUDY_MEM_LIMIT_MB=256 "$BIN" bool count --n 6 --heavy --json \
     > "$TMP/mem-heavy.json" 2>/dev/null; then
  fail "256 MB cap should stop the n=6 Dedekind count"
fi
[ "$(jq -r .ok "$TMP/mem-heavy.json")" = "false" ] || fail "memory cap ok=false"
[ "$(jq -r .exit_code "$TMP/mem-heavy.json")" = "2" ] || fail "memory cap exit code"
[ "$(jq -r '.error | test("memory limit")' "$TMP/mem-heavy.json")" = "true" ] || fail "memory cap message"
pass "memory limit"

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
  skip_or_fail "CDL checks (no $CDL)"
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
"$BIN" design path --expr 'A^B' --json > "$TMP/path.json" || fail "path-variable synthesis"
[ "$(jq -r .total "$TMP/path.json")" = "6" ] || fail "path XOR 6T"
[ "$(jq -r .verified "$TMP/path.json")" = "true" ] || fail "path switch verification"
"$BIN" design template --expr 'A^B' --json > "$TMP/template.json"   || fail "bounded template optimization"
[ "$(jq -r .verified "$TMP/template.json")" = "true" ] || fail "template verification"
[ "$(jq -r .proof "$TMP/template.json")" = "true" ] || fail "template optimality"
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
# Fan-in counts distinct input signals, not the longest product term:
# ~(A|B|C) is a 3-input NOR and must not pass --max-fanin 2 single-stage.
[ "$("$BIN" design min --expr '~(A|B|C)' --vars A,B,C --static-cmos --max-fanin 2 --json | jq -r '.designs[0].cost')" = "10" ] || fail "NOR3 fan-in 2"
# --verify static without --static-cmos has no gate to prove; it must not
# return an unverified design with ok:true.
rc=0
"$BIN" design min --expr 'A^B' --verify static --json >/dev/null 2>&1 || rc=$?
[ "$rc" = "2" ] || fail "--verify static without --static-cmos"
rc=0
"$BIN" pipeline --funcs 'nand2=~(A&B)' --verify static >/dev/null 2>&1 || rc=$?
[ "$rc" = "2" ] || fail "pipeline --verify static"
pass "static CMOS constraint"

# Don't-care ("any") outputs: the 4-to-2 encoder only cares about one-hot
# inputs, so the PDNs can be chosen as A0|A2 / A0|A1 (8T, no inverters).
"$BIN" multi exact --spec "$SPEC/encoder42.json" --json > "$TMP/encoder.exact" || fail "encoder exact"
[ "$(jq -r .verified "$TMP/encoder.exact")" = "true" ] || fail "encoder verify"
[ "$(jq -r .total "$TMP/encoder.exact")" = "8" ] || fail "encoder don't-care 8T"
[ "$(jq -r '.any.Y0 | length' "$TMP/encoder.exact")" = "16" ] || fail "encoder any echo"
# Tri-state has no "any 0/1" semantics: Z is not an extension.
rc=0
"$BIN" multi path --spec "$SPEC/encoder42.json" --tri-state --json >/dev/null 2>&1 || rc=$?
[ "$rc" = "1" ] || fail "tri-state accepted don't-cares"
cat > "$TMP/badany.json" <<'JSON'
{"name":"badany","inputs":["A","B"],"outputs":[{"name":"Y","expr":"A","any":"101"}]}
JSON
rc=0
"$BIN" multi exact --spec "$TMP/badany.json" --json >/dev/null 2>&1 || rc=$?
[ "$rc" = "2" ] || fail "bad any mask accepted"
cat > "$TMP/allany.json" <<'JSON'
{"name":"allany","inputs":["A","B"],"outputs":[{"name":"Y","expr":"A&B","any":"1111"}]}
JSON
rc=0
"$BIN" multi exact --spec "$TMP/allany.json" --json >/dev/null 2>&1 || rc=$?
[ "$rc" = "2" ] || fail "all-don't-care output accepted"
# Only "A=0 => Y=1" is required; the 2T inverter is a valid extension.
cat > "$TMP/onlyhigh.json" <<'JSON'
{"name":"onlyhigh","inputs":["A"],"outputs":[{"name":"Y","expr":"~A","any":"01"}]}
JSON
[ "$("$BIN" multi exact --spec "$TMP/onlyhigh.json" --json | jq -r .total)" = "2" ] || fail "only-high constraint"
# Free rows must not leak the expression's arbitrary values into the cost.
cat > "$TMP/free.json" <<'JSON'
{"name":"free","inputs":["A","B"],"outputs":[{"name":"Y","expr":"A^B","any":"1011"}]}
JSON
[ "$("$BIN" multi exact --spec "$TMP/free.json" --json | jq -r .total)" = "2" ] || fail "free-row completion"
pass "don't-care outputs (encoder)"

# General multi-output synthesis: the 3-to-8 decoder is only a spec file.
if [ -f "$CDL" ] && command -v yosys >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then
  "$BIN" multi synth --spec "$SPEC/decoder38.json" --out "$TMP/decoder38"     --json > "$TMP/decoder38.out" || fail "multi-output decoder"
  [ "$(jq -r .verified "$TMP/decoder38.out")" = "true" ] || fail "multi-output verify"
  [ "$(jq -r .total "$TMP/decoder38.out")" = "54" ] || fail "decoder 54T"
  # Every output, so a rotated or shifted decoder cannot pass.
  i=0
  for want in 10000000 01000000 00100000 00010000 00001000 00000100 00000010 00000001; do
    [ "$(jq -r ".truths.Y$i" "$TMP/decoder38.out")" = "$want" ] || fail "decoder Y$i truth"
    i=$((i + 1))
  done
  "$BIN" multi exact --spec "$SPEC/decoder38.json" --json > "$TMP/decoder38.exact"     || fail "multi-output exact"
  [ "$(jq -r .optimal "$TMP/decoder38.exact")" = "54" ] || fail "decoder exact 54T"
  [ "$(jq -r .proof "$TMP/decoder38.exact")" = "true" ] || fail "decoder optimality proof"
  # Every output is one cube, so the exact model takes the analytic shortcut.
  [ "$(jq -r .proof_method "$TMP/decoder38.exact")" = "analytic_shortcut" ] || fail "decoder proof method"
  [ "$(jq -r .verified "$TMP/decoder38.exact")" = "true" ] || fail "decoder exact verification"
  [ "$(jq -r .nfet "$TMP/decoder38.exact")" = "27" ] || fail "decoder exact NMOS"
  [ "$(jq -r '.inverted | length' "$TMP/decoder38.exact")" = "3" ] || fail "decoder exact inverters"
  cat > "$TMP/demux4.json" <<'JSON'
{
  "name": "demux1to4",
  "inputs": ["A", "B"],
  "outputs": [
    {"name": "Y0", "expr": "~A&~B"},
    {"name": "Y1", "expr": "~A&B"},
    {"name": "Y2", "expr": "A&~B"},
    {"name": "Y3", "expr": "A&B"}
  ]
}
JSON
  "$BIN" multi path --spec "$SPEC/decoder38.json" --tri-state --max-cost 40 --json     > "$TMP/decoder38.tri" || fail "tri-state 3-8 decoder"
  [ "$(jq -r .total "$TMP/decoder38.tri")" = "16" ] || fail "tri 3-8 decoder 16T"
  [ "$(jq -r .verified "$TMP/decoder38.tri")" = "true" ] || fail "tri 3-8 verification"
  "$BIN" multi path --spec "$TMP/demux4.json" --tri-state --max-cost 40 --json     > "$TMP/demux4.tri" || fail "tri-state 1-to-4 demux"
  [ "$(jq -r .total "$TMP/demux4.tri")" = "6" ] || fail "tri 1-to-4 demux 6T"
  [ "$(jq -r .verified "$TMP/demux4.tri")" = "true" ] || fail "tri 1-to-4 verification"
  "$BIN" multi path --spec "$TMP/demux4.json" --max-cost 40 --json     > "$TMP/demux4.static" || fail "1-to-4 dual-rail path"
  [ "$(jq -r .total "$TMP/demux4.static")" = "18" ] || fail "1-to-4 dual-rail 18T"
  [ "$(jq -r .verified "$TMP/demux4.static")" = "true" ] || fail "1-to-4 dual-rail verification"
  [ "$(jq -r '.graph.sources | length' "$TMP/demux4.static")" = "2" ] || fail "1-to-4 dual rails"
  [ "$(jq -r .mode "$TMP/demux4.static")" = "dual-rail" ] || fail "1-to-4 dual-rail mode"
  # The path search honours --max-cost; the static fallback must too.
  if "$BIN" multi path --spec "$SPEC/decoder38.json" --max-cost 1 --json \
       > "$TMP/path-budget.json"; then
    fail "multi path ignored --max-cost"
  fi
  [ "$(jq -r .ok "$TMP/path-budget.json")" = "false" ] || fail "path budget json"
  # Two outputs defined by the same expression share one Yosys bit; both must
  # still be named and verified instead of one dangling as Z.
  cat > "$TMP/alias.json" <<'JSON'
{"name":"alias","inputs":["A","B"],"outputs":[
 {"name":"Y0","expr":"~(A&B)"},{"name":"Y1","expr":"~(A&B)"}]}
JSON
  "$BIN" multi synth --spec "$TMP/alias.json" --out "$TMP/alias-out" --json > "$TMP/alias.out" || fail "aliased outputs"
  [ "$(jq -r .verified "$TMP/alias.out")" = "true" ] || fail "aliased verify"
  # Don't-care specs go through mapping and path search too.
  "$BIN" multi synth --spec "$SPEC/encoder42.json" --json > "$TMP/encoder.synth" || fail "encoder synth"
  [ "$(jq -r .verified "$TMP/encoder.synth")" = "true" ] || fail "encoder synth verify"
  "$BIN" multi path --spec "$SPEC/encoder42.json" --max-cost 64 --json > "$TMP/encoder.path" || fail "encoder path"
  [ "$(jq -r .verified "$TMP/encoder.path")" = "true" ] || fail "encoder path verify"
  # Hierarchical RTL with a user atom: the 4-2 priority encoder stays a
  # blackbox, the glue is mapped to standard cells, and the composition is
  # proved per output against the spec.
  cat > "$TMP/atoms8.v" <<'VERILOG'
module pe42(input logic A3, A2, A1, A0, output logic Y1, Y0);
  assign Y1 = A3 | A2;
  assign Y0 = A3 | (~A2 & A1);
endmodule
VERILOG
  cat > "$TMP/top8.v" <<'VERILOG'
module top(input logic A7, A6, A5, A4, A3, A2, A1, A0,
           output logic Y2, Y1, Y0);
  wire H1, H0, L1, L0;
  wire hi = A7 | A6 | A5 | A4;
  pe42 high(A7, A6, A5, A4, H1, H0);
  pe42 low (A3, A2, A1, A0, L1, L0);
  assign Y2 = hi;
  assign Y1 = hi ? H1 : L1;
  assign Y0 = hi ? H0 : L0;
endmodule
VERILOG
  cat > "$TMP/prio8.json" <<'JSON'
{"name":"prio8","inputs":["A7","A6","A5","A4","A3","A2","A1","A0"],
 "outputs":[
  {"name":"Y2","expr":"A7|A6|A5|A4"},
  {"name":"Y1","expr":"A7|A6|(~(A7|A6|A5|A4)&(A3|A2))"},
  {"name":"Y0","expr":"A7|(~A6&A5)|(~(A7|A6|A5|A4)&(A3|(~A2&A1)))"}]}
JSON
  "$BIN" multi synth --rtl "$TMP/top8.v" --atoms "$TMP/atoms8.v" \
    --spec "$TMP/prio8.json" --out "$TMP/rtl8" --json > "$TMP/rtl8.json" \
    || fail "rtl + atoms"
  [ "$(jq -r .mode "$TMP/rtl8.json")" = "rtl-atoms" ] || fail "rtl mode"
  [ "$(jq -r .verified "$TMP/rtl8.json")" = "true" ] || fail "rtl verify"
  [ "$(jq -r '.atoms[0].module' "$TMP/rtl8.json")" = "pe42" ] || fail "rtl atom"
  [ "$(jq -r '.total == (.atom_cost + .cell_cost)' "$TMP/rtl8.json")" = "true" ] || fail "rtl cost"
  [ -f "$TMP/rtl8/netlist.v" ] || fail "rtl netlist"
  # Shell pipeline: a generated module becomes the next stage's atom.
  #   stage1 --emit-atom pe42 | stage2 --atoms -
  cat > "$TMP/pe42.json" <<'JSON'
{"name":"pe42","inputs":["A3","A2","A1","A0"],
 "outputs":[{"name":"Y1","expr":"A3|A2"},{"name":"Y0","expr":"A3|(~A2&A1)"}]}
JSON
  "$BIN" multi synth --spec "$TMP/pe42.json" --emit-atom pe42 --json \
    | "$BIN" multi synth --rtl "$TMP/top8.v" --atoms - --spec "$TMP/prio8.json" --json \
    > "$TMP/pipe-atom.json" || fail "shell pipeline atoms"
  [ "$(jq -r .verified "$TMP/pipe-atom.json")" = "true" ] || fail "pipeline verify"
  [ "$(jq -r '.atoms[0].module' "$TMP/pipe-atom.json")" = "pe42" ] || fail "pipeline atom"
  [ "$(jq -r '.total == (.atom_cost + .cell_cost)' "$TMP/pipe-atom.json")" = "true" ] || fail "pipeline cost"
  # Arbitrary chaining: each stage adds its module to the piped atom library.
  cat > "$TMP/top8c.v" <<'V'
module top(input logic A7, A6, A5, A4, A3, A2, A1, A0,
           output logic Y2, Y1, Y0);
  pe8 u(A7, A6, A5, A4, A3, A2, A1, A0, Y2, Y1, Y0);
endmodule
V
  "$BIN" multi synth --spec "$TMP/pe42.json" --emit-atom pe42 --json \
    | "$BIN" multi synth --rtl "$TMP/top8.v" --atoms - --emit-atom pe8 \
        --spec "$TMP/prio8.json" --json \
    | "$BIN" multi synth --rtl "$TMP/top8c.v" --atoms - --spec "$TMP/prio8.json" --json \
    > "$TMP/pipe-chain.json" || fail "3-stage chain"
  [ "$(jq -r .verified "$TMP/pipe-chain.json")" = "true" ] || fail "chain verify"
  [ "$(jq -r '.atoms[0].module' "$TMP/pipe-chain.json")" = "pe8" ] || fail "chain atom"
  [ "$(jq -r '.atoms[0].transistors' "$TMP/pipe-chain.json")" = "66" ] || fail "chain cost"
  # Structural verification: no spec, reference module + miter.
  cat > "$TMP/gold8.v" <<'VERILOG'
module golden(input logic A7, A6, A5, A4, A3, A2, A1, A0,
              output logic Y2, Y1, Y0);
  logic [7:0] a;
  logic [2:0] y;
  assign a = {A7, A6, A5, A4, A3, A2, A1, A0};
  always_comb begin
    y = 3'd0;
    for (int i = 0; i < 8; i++) if (a[i]) y = i[2:0];
  end
  assign Y2 = y[2]; assign Y1 = y[1]; assign Y0 = y[0];
endmodule
VERILOG
  "$BIN" multi synth --rtl "$TMP/top8.v" --atoms "$TMP/atoms8.v" \
    --ref "$TMP/gold8.v" --json > "$TMP/ref8.json" || fail "structural ref"
  [ "$(jq -r .verified "$TMP/ref8.json")" = "true" ] || fail "ref verify"
  [ "$(jq -r .total "$TMP/ref8.json")" = "66" ] || fail "ref cost"


  # Same free-row spec through the mapper and the path search: 2T as well.
  [ "$("$BIN" multi synth --spec "$TMP/free.json" --json | jq -r .total)" = "2" ] || fail "free-row synth"
  [ "$("$BIN" multi path --spec "$TMP/free.json" --max-cost 64 --json | jq -r .total)" = "2" ] || fail "free-row path"
  [ "$(jq -r '.truths.Y0' "$TMP/alias.out")" = "1110" ] || fail "aliased Y0"
  [ "$(jq -r '.truths.Y1' "$TMP/alias.out")" = "1110" ] || fail "aliased Y1"
  # The exported CDL must keep both ports and read back with both aliases.
  "$BIN" cdl list --cdl "$TMP/alias-out/flattened.cdl" --json > "$TMP/alias.lst" || fail "alias cdl list"
  [ "$(jq -r '.cells[0].outputs | length' "$TMP/alias.lst")" = "2" ] || fail "alias ports"
  "$BIN" cdl solve --cdl "$TMP/alias-out/flattened.cdl" --cell MULTI_STAGE_STATIC_CMOS --inputs A,B --json > "$TMP/alias.solve" || fail "alias cdl solve"
  [ "$(jq -r '.truth.Y1' "$TMP/alias.solve")" = "1110" ] || fail "alias readback"
  # A comment alone does not connect the second port in a SPICE simulator.
  grep -q "V__alias_Y1 Y1 Y0 0" "$TMP/alias-out/flattened.cdl" || fail "alias short missing"
  [ "$(jq -r '[.graph.nets[] | select(startswith("wire:"))] | length' "$TMP/demux4.static")" = "0" ] || fail "demux still has wire connectors"
  "$BIN" multi path --spec "$SPEC/decoder38.json" --max-cost 54 --json     > "$TMP/decoder38.static" || fail "3-8 dual-rail path"
  [ "$(jq -r .total "$TMP/decoder38.static")" = "44" ] || fail "3-8 dual-rail 44T"
  [ "$(jq -r .proof_scope "$TMP/decoder38.static")" = "retained candidate path set" ] || fail "path proof scope"
  # The old emitter tied outputs together with zero-cost "wire" TGH edges.
  [ "$(jq -r '[.graph.nets[] | select(startswith("wire:"))] | length' "$TMP/decoder38.static")" = "0" ] || fail "decoder still has wire connectors"
  [ "$(jq -r .verified "$TMP/decoder38.static")" = "true" ] || fail "3-8 dual-rail verification"
  [ -f "$TMP/decoder38/index.html" ] || fail "decoder render index"
  [ -f "$TMP/decoder38/scheme-0.svg" ] || fail "decoder schematic"
  pass "multi-output circuit (decoder as spec)"

  # Shipped spec: 16-hex seven-segment display (A is MSB).  The fan-in
  # contract is enforced by the tool itself -- a mapped cell with more inputs
  # than --max-fanin is a hard error -- so a passing run pins it.
  "$BIN" multi synth --spec "$SPEC/hex7seg.json" --max-fanin 2 --json \
    > "$TMP/hex7seg.fanin2" || fail "hex7seg --max-fanin 2"
  [ "$(jq -r .verified "$TMP/hex7seg.fanin2")" = "true" ] || fail "hex7seg 2-input verify"
  # ABC's cell count varies between Yosys/ABC releases, so assert the fan-in
  # contract (the tool errors on over-fanin cells) and a budget, not an exact
  # mapping.  This machine: 44 cells / 192T.
  total=$(jq -r .total "$TMP/hex7seg.fanin2")
  [ "$total" -le 230 ] || fail "hex7seg 2-input over budget ($total T)"
  "$BIN" multi synth --spec "$SPEC/hex7seg.json" --json \
    > "$TMP/hex7seg.default" || fail "hex7seg default fan-in"
  [ "$(jq -r .verified "$TMP/hex7seg.default")" = "true" ] || fail "hex7seg default verify"
  total=$(jq -r .total "$TMP/hex7seg.default")
  [ "$total" -le 200 ] || fail "hex7seg default over budget ($total T)"
  "$BIN" multi exact --spec "$SPEC/hex7seg.json" --json \
    > "$TMP/hex7seg.exact" || fail "hex7seg exact"
  [ "$(jq -r .optimal "$TMP/hex7seg.exact")" = "180" ] || fail "hex7seg exact 180T"
  [ "$(jq -r .proof "$TMP/hex7seg.exact")" = "true" ] || fail "hex7seg optimality proof"
  [ "$(jq -r .proof_method "$TMP/hex7seg.exact")" = "exhaustive" ] || fail "hex7seg proof method"
  [ "$(jq -r .verified "$TMP/hex7seg.exact")" = "true" ] || fail "hex7seg exact verify"
  # All seven segments on both mappings: this is the intended glyph table
  # (0-9, A, b, C, d, E, F), checked by hand against a standard display, so a
  # spec edit that changes a glyph fails here instead of passing silently.
  for file in "$TMP/hex7seg.fanin2" "$TMP/hex7seg.exact"; do
    for want in seg_a=1011011111101011 seg_b=1111100111100100 \
                seg_c=1101111111110100 seg_d=1011011011011110 \
                seg_e=1010001010111111 seg_f=1000111011111011 \
                seg_g=0011111011110111; do
      [ "$(jq -r ".truths.${want%%=*}" "$file")" = "${want#*=}" ] || fail "hex7seg ${want%%=*} truth"
    done
  done
  pass "hex7seg spec (2-input / default / exact)"

  # The cap is inherited by yosys and dcstudy-z3: 1 GiB is plenty for this
  # spec, 64 MiB is not, and the failure has to stay a normal JSON error.
  "$BIN" multi exact --spec "$SPEC/hex7seg.json" --mem-limit 1024 --json \
    > "$TMP/mem-ok.json" || fail "1 GiB cap fits multi exact"
  [ "$(jq -r .verified "$TMP/mem-ok.json")" = "true" ] || fail "1 GiB cap verify"
  if "$BIN" multi synth --spec "$SPEC/hex7seg.json" --mem-limit 64 --json \
       > "$TMP/mem-fail.json" 2>/dev/null; then
    fail "64 MB cap should not fit multi synth"
  fi
  [ "$(jq -r .ok "$TMP/mem-fail.json")" = "false" ] || fail "64 MB cap ok=false"
  pass "memory cap reaches child processes"

# Multi-output path variables with shared edges.
cat > "$TMP/halfadder.json" <<'JSON'
{
  "name": "half_adder",
  "inputs": ["A", "B"],
  "outputs": [
    {"name": "S",  "expr": "A^B"},
    {"name": "CO", "expr": "A&B"}
  ]
}
JSON
"$BIN" multi path --spec "$TMP/halfadder.json" --json > "$TMP/mpath.json"   || fail "multi-output path synthesis"
[ "$(jq -r .verified "$TMP/mpath.json")" = "true" ] || fail "multi-output path verification"
[ "$(jq -r '.outputs | length' "$TMP/mpath.json")" = "2" ] || fail "multi-output path output count"
[ "$(jq -r .proof "$TMP/mpath.json")" = "true" ] || fail "multi-output path proof"
else
  skip_or_fail "multi-output checks (no CDL/yosys/node)"
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
  skip_or_fail "render (no node)"
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
# Text output must not read verification fields that were not generated.
"$BIN" pipeline --funcs 'nand2=~(A&B)' --max-cost 6 --max-chain 3 --verify none \
  > "$TMP/pipe-none.txt" || fail "pipeline text none"
"$BIN" pipeline --funcs 'nand2=~(A&B)' --max-cost 6 --max-chain 3 --verify yosys \
  > "$TMP/pipe-yosys.txt" || fail "pipeline text yosys"
grep -q "yosys: PASS" "$TMP/pipe-yosys.txt" || fail "pipeline text yosys output"
pass "pipeline"

# JSON errors keep the same interface.
rc=0
"$BIN" bool info --expr >/dev/null 2>&1 || rc=$?
[ "$rc" -eq 2 ] || fail "usage error exit code ($rc)"
pass "CLI error handling"

echo "integration: all checks passed"
