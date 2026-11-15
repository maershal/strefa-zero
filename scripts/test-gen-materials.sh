#!/usr/bin/env bash
# Regression tests for scripts/gen_materials.py on copies of the registry.
# Usage: ./scripts/test-gen-materials.sh
set -u
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
python=$(command -v python3 || command -v python)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failures=0

# run <case> <expected message part, empty = success>: generates from $work/<case>.toml.
run() {
    local name=$1 want=$2 output status=0
    output=$("$python" "$root/scripts/gen_materials.py" --registry "$work/$name.toml" --output "$work/$name.hpp" 2>&1) ||
        status=$?
    if [[ -z $want ]]; then
        [[ $status -eq 0 ]] && return
    elif [[ $status -ne 0 && $output == *"$want"* ]]; then
        return
    fi
    echo "FAIL: $name -> exit $status: $output (expected '${want:-success}')"
    failures=$((failures + 1))
}
# expect <case> <expected message part> <sed expression applied to the registry copy>
expect() {
    tr -d '\r' <"$root/src/sim/materials.toml" | sed -e "$3" >"$work/$1.toml"
    run "$1" "$2"
}

expect unchanged '' ''
expect missing-key 'stone: missing density' '/^id = "stone"$/,/^$/{/^density = /d}'
expect unknown-key 'stone: unknown key hardness' 's/^id = "stone"$/&\nhardness = 3/'
expect duplicate-id 'id must be a unique' 's/^id = "sand"$/id = "stone"/'
expect bad-phase 'stone: phase must be one of' '/^id = "stone"$/,/^$/s/^phase = .*/phase = "plasma"/'
expect bad-color 'stone: color must be three integers' '/^id = "stone"$/,/^$/s/^color = .*/color = [1, 2, 300]/'
expect not-a-number 'stone.density: expected a number' '/^id = "stone"$/,/^$/s/^density = .*/density = "heavy"/'
expect not-finite 'stone.density: must be finite' '/^id = "stone"$/,/^$/s/^density = .*/density = inf/'
expect multiline-name 'stone.name: expected a single-line string' '/^id = "stone"$/,/^$/s/^name = .*/name = "a\\nb"/'
expect renamed-id 'append-only' 's/^id = "sand"$/id = "gravel"/'
expect swapped-order 'append-only' 's/^id = "sand"$/id = "tmp"/;s/^id = "water"$/id = "sand"/;s/^id = "tmp"$/id = "water"/'
last_table=$(tr -d '' <"$root/src/sim/materials.toml" | grep -n '^\[\[material\]\]$' | tail -n 1 | cut -d: -f1)
expect removed-last 'append-only' "$last_table,\$d"

# A material appended to the registry without its id in PINNED_IDS.
tr -d '\r' <"$root/src/sim/materials.toml" >"$work/appended.toml"
{
    printf '\n[[material]]\nid = "selftest_material"\nname = "x"\ndescription = "y"\nphase = "liquid"\ncolor = [1, 2, 3]\n'
    printf 'density = 1\nheat_capacity = 1\nconductance = 1\nspawn_temperature = 1\nignition = 0\nburn_rate = 0\n'
    printf 'heat_release = 0\nresidue = 0\nstrength = 0\n'
} >>"$work/appended.toml"
run appended 'append its id to PINNED_IDS'

# --check: the committed file is current; a stale copy is reported.
if ! "$python" "$root/scripts/gen_materials.py" --check; then
    echo "FAIL: committed material_tables.hpp is stale"
    failures=$((failures + 1))
fi
printf '// stale\n' >"$work/stale.hpp"
if "$python" "$root/scripts/gen_materials.py" --check --output "$work/stale.hpp" 2>/dev/null; then
    echo "FAIL: stale output passed --check"
    failures=$((failures + 1))
fi

if [[ $failures -ne 0 ]]; then
    echo "test-gen-materials: $failures failure(s)"
    exit 1
fi
echo "test-gen-materials: OK"
