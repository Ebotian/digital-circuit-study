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
pass "boolean functions"

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
