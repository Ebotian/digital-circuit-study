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
declare -A DEFAULT_SEEN

render_atom_spec() {
  local name=$1 dir=$2 spec=$3
  mkdir -p "$dir/atoms/$name"
  "$BIN" multi synth --spec "$spec" --out "$dir/atoms/$name" --png --json \
    >"$dir/atoms/$name/result.json"
  pass "  atom $name"
}
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
  local m spec libfile tmp
  libfile=""
  [ -n "$lib" ] && libfile="$(path_of "$lib")"
  tmp=$(mktemp -d)
  trap 'rm -rf "$tmp"' RETURN
  for m in $(jq -r '.atoms[].module' "$dir/result.json"); do
    if [ -f "$ROOT/tests/verify/instances/specs/$m.json" ]; then
      spec="$ROOT/tests/verify/instances/specs/$m.json"
    elif [ -n "$libfile" ] && [ -f "$libfile" ]; then
      jq --arg m "$m" '.[] | select(.name==$m)' "$libfile" >"$tmp/$m.json"
      spec="$tmp/$m.json"
      [ -s "$spec" ] || continue
    else
      continue
    fi
    render_atom_spec "$m" "$dir" "$spec"
  done
  rm -rf "$tmp"
  pass "render $name/$variant (compose + atoms)"
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
  render_atom_spec "$atom" "$dir" "$(path_of "$atom_spec")"
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
  local f base spec atomtmp
  for f in "$@"; do
    base=$(basename "$f" .v)
    if [ -f "$ROOT/tests/verify/instances/specs/$base.json" ]; then
      render_atom_spec "$base" "$dir" \
        "$ROOT/tests/verify/instances/specs/$base.json"
    elif [ "$base" = "pe16" ]; then
      atomtmp=$(mktemp -d)
      cat "$ROOT/tests/verify/instances/rtl/pe42.v" >"$atomtmp/pe42.v"
      mkdir -p "$dir/atoms/pe16"
      "$BIN" render --netlist "$f" --atoms "$atomtmp/pe42.v" --top pe16 \
        --title pe16 --out "$dir/atoms/pe16" --png >/dev/null
      rm -rf "$atomtmp"
      render_atom_spec pe42 "$dir" \
        "$ROOT/tests/verify/instances/specs/pe42.json"
      pass "  atom pe16 (hierarchy)"
    fi
  done
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
  if [ "$default" -eq 1 ] && [ -z "${DEFAULT_SEEN[$name]:-}" ]; then
    dir="$OUT/$name"
    DEFAULT_SEEN[$name]=1
  else
    dir="$OUT/$name/$variant"
  fi
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
