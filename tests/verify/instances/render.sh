#!/usr/bin/env bash
# Render selected implementations from render.manifest.
#   make render                         # default variant of every instance
#   make render INSTANCE=fa1            # all variants of fa1
#   make render INSTANCE=fa1 VARIANT=ha # one implementation
#   make render VARIANT=rtl             # that variant everywhere it exists
. "$(dirname "$0")/../lib.sh"
MANIFEST="$(dirname "$0")/render.manifest"
OUT="${OUT:-$ROOT/out/render}"
INSTANCE="${INSTANCE:-}"
VARIANT="${VARIANT:-}"
mkdir -p "$OUT"
need rsvg-convert
need jq

path_of() {
  if [[ "$1" == bench/* ]]; then printf '%s\n' "$ROOT/$1";
  else printf '%s\n' "$ROOT/tests/verify/instances/$1"; fi
}

selected=0
render_synth() {
  local name=$1 variant=$2 dir=$3 spec=$4
  shift 4
  mkdir -p "$dir"
  "$BIN" multi synth --spec "$(path_of "$spec")" "$@" \
    --out "$dir" --png --json >"$dir/result.json"
  pass "render $name/$variant (synth)"
}
render_compose() {
  local name=$1 variant=$2 dir=$3 spec=$4 lib=${5:-}
  mkdir -p "$dir"
  local args=(--spec "$(path_of "$spec")")
  [ -n "$lib" ] && args+=(--library "$(path_of "$lib")")
  "$BIN" multi compose "${args[@]}" --out "$dir" --png --json >"$dir/result.json"
  pass "render $name/$variant (compose)"
}
render_atoms() {
  local name=$1 variant=$2 dir=$3 atom_spec=$4 top=$5 ref=$6
  mkdir -p "$dir"
  local tmp atom cost json
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' RETURN
  atom=$(basename "$atom_spec" .json)
  json=$("$BIN" multi synth --spec "$(path_of "$atom_spec")" \
    --emit-atom "$atom" --json)
  jq -r .atom_verilog <<<"$json" >"$tmp/atoms.v"
  json=$("$BIN" multi synth --rtl "$(path_of "$top")" \
    --atoms "$tmp/atoms.v" --ref "$(path_of "$ref")" --json)
  cost=$(jq -r .total <<<"$json")
  "$BIN" render --netlist "$(path_of "$top")" \
    --atoms "$tmp/atoms.v" --top top --title "$name" --cost "$cost" \
    --out "$dir" --png >/dev/null
  printf '%s\n' "$json" >"$dir/result.json"
  pass "render $name/$variant (atoms, ${cost}T)"
}
render_rtl() {
  local name=$1 variant=$2 dir=$3 top=$4 ref=$5
  shift 5
  mkdir -p "$dir"
  local tmp cost json
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' RETURN
  cat "$@" >"$tmp/atoms.v" 2>/dev/null || true
  if [ -s "$tmp/atoms.v" ]; then
    json=$("$BIN" multi synth --rtl "$(path_of "$top")" \
      --atoms "$tmp/atoms.v" --ref "$(path_of "$ref")" --json)
    cost=$(jq -r .total <<<"$json")
    printf '%s\n' "$json" >"$dir/result.json"
  else
    cost=0
  fi
  local args=(--netlist "$(path_of "$top")" --top top
              --title "$name" --out "$dir" --png)
  [ "$cost" -gt 0 ] && args+=(--cost "$cost")
  if [ -s "$tmp/atoms.v" ]; then
    # render expects one --atoms file; concatenate atom modules there.
    cat "$tmp/atoms.v" >"$tmp/atoms-all.v"
    "$BIN" render --netlist "$(path_of "$top")" \
      --atoms "$tmp/atoms-all.v" --top top --title "$name" --cost "$cost" \
      --out "$dir" --png >/dev/null
  else
    "$BIN" render --netlist "$(path_of "$top")" \
      --top top --title "$name" --out "$dir" --png >/dev/null
  fi
  pass "render $name/$variant (rtl, ${cost}T)"
}

while read -r name variant kind rest; do
  case "$name" in ''|'#'*) continue ;; esac
  default=0
  if [[ "$variant" == *'*' ]]; then default=1; variant=${variant%\*}; fi
  if [ -n "$INSTANCE" ] && [ "$name" != "$INSTANCE" ]; then continue; fi
  if [ -n "$VARIANT" ] && [ "$variant" != "$VARIANT" ]; then continue; fi
  if [ -z "$INSTANCE" ] && [ -z "$VARIANT" ] && [ "$default" -eq 0 ]; then continue; fi
  selected=$((selected + 1))
  if [ "$default" -eq 1 ]; then dir="$OUT/$name"; else dir="$OUT/$name/$variant"; fi
  # shellcheck disable=SC2086
  set -- $rest
  case "$kind" in
    synth) render_synth "$name" "$variant" "$dir" "$@" ;;
    compose) render_compose "$name" "$variant" "$dir" "$@" ;;
    atoms)
      [ $# -eq 3 ] || fail "atoms manifest row needs: atom_spec top ref"
      render_atoms "$name" "$variant" "$dir" "$1" "$2" "$3" ;;
    rtl)
      [ $# -ge 3 ] || fail "rtl manifest row needs: top atom... ref"
      top=$1; shift; ref=${!#}
      atoms=()
      for f in "$@"; do [ "$f" != "$ref" ] && atoms+=("$(path_of "$f")"); done
      render_rtl "$name" "$variant" "$dir" "$top" "$ref" "${atoms[@]}" ;;
    *) fail "unknown render kind: $kind" ;;
  esac
done <"$MANIFEST"
[ "$selected" -gt 0 ] || fail "no render variant matched INSTANCE='$INSTANCE' VARIANT='$VARIANT'"
