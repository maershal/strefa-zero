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
    output=$("$python" "$root/scripts/gen_materials.py" --registry "$work/$name.toml" --output "$work/$name.hpp" \
        --glsl "$work/$name.glsl" 2>&1) || status=$?
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
expect unknown-top-level 'unknown top-level key particles' 's/^particle_materials = /particles = /'
expect bad-particle 'particle_materials must be' 's/^particle_materials = .*/particle_materials = ["mud"]/'
expect alias-to-self 'acid: enthalpy_offset_of must name' 's/^enthalpy_offset_of = .*/enthalpy_offset_of = "acid"/'
expect alias-unknown 'acid: enthalpy_offset_of must name' 's/^enthalpy_offset_of = .*/enthalpy_offset_of = "slush"/'
expect alias-not-a-string 'acid: enthalpy_offset_of must name' 's/^enthalpy_offset_of = .*/enthalpy_offset_of = 3/'
expect alias-to-alias 'enthalpy_offset_of must name' 's/^id = "oil"$/&\nenthalpy_offset_of = "acid"/'
expect transition-from-alias 'the enthalpy offset of acid must be defined before' 's/^from = "stone"$/from = "acid"/'
expect transition-not-a-table 'transition must be an array of tables' '/^\[\[transition\]\]$/,$d;s/^particle_materials = .*/&\ntransition = 5/'
expect transition-missing-key 'transition #1: expected exactly the keys' '/^to = "steam"$/d'
expect transition-id-not-a-string 'transition #0: from and to must be' 's/^from = "ice"$/from = 17/'
expect transition-bad-number 'transition #0.temperature: expected a number' 's/^temperature = 273.15$/temperature = "cold"/'
expect transition-not-finite 'transition #0.temperature: must be finite' 's/^temperature = 273.15$/temperature = inf/'
expect transition-zero-latent 'transition #0.latent: must be positive' 's/^latent = 334$/latent = 0/'
expect transition-negative 'transition #0.temperature: must be positive' 's/^temperature = 273.15$/temperature = -1/'
expect particles-missing 'particle_materials must be' '/^particle_materials = /d'
expect particle-not-a-string 'particle_materials must be' 's/^particle_materials = .*/particle_materials = [1, 2]/'
expect pyrolysis-not-wood 'the thermal shaders support exactly one' 's/^from = "wood"$/from = "oil"/'
expect pyrolysis-missing 'the thermal shaders support exactly one' '/^\[\[pyrolysis\]\]$/,/^$/d'
expect pyrolysis-incombustible 'wood must be combustible' '/^id = "wood"$/,/^$/s/^burn_rate = .*/burn_rate = 0/'
expect pyrolysis-offset 'coal must not have an enthalpy offset' '/^id = "coal"$/,/^$/s/^strength = .*/&\nenthalpy_offset_of = "water"/'
expect pyrolysis-transition-into 'coal must not have an enthalpy offset' 's/^to = "molten_metal"$/to = "coal"/'
expect pyrolysis-not-fuel-gas 'the thermal shaders support exactly one' 's/^gas = "gas"$/gas = "smoke"/'
expect pyrolysis-not-a-table 'pyrolysis must be an array of tables' '/^\[\[pyrolysis\]\]$/,/^$/d;s/^particle_materials = .*/&\npyrolysis = 5/'
expect pyrolysis-extra-key 'pyrolysis #0: expected exactly the keys' 's/^gas = "gas"$/&\npressure = 1/'
expect pyrolysis-same-material 'pyrolysis #0: from, to and gas must be three different' 's/^to = "coal"$/to = "wood"/'
expect pyrolysis-unknown 'pyrolysis #0: from, to and gas must be three different' 's/^to = "coal"$/to = "charcoal"/'
expect pyrolysis-zero-rate 'pyrolysis #0.rate: must be positive' 's/^rate = 0.065$/rate = 0/'
expect pyrolysis-not-a-number 'pyrolysis #0.span: expected a number' 's/^span = 100$/span = "wide"/'
expect pyrolysis-all-gas 'pyrolysis #0.volatile_fraction: must be below 1' 's/^volatile_fraction = .*/volatile_fraction = 1/'
expect pyrolysis-solid-gas 'pyrolysis #0: gas must be a material of phase gas' '/^id = "gas"$/,/^$/s/^phase = .*/phase = "liquid"/'
expect pyrolysis-energy 'the products carry more chemical energy than wood' 's/^volatile_fraction = .*/volatile_fraction = 0.1/'
expect transition-unknown 'transition #0: from and to must be' 's/^from = "ice"$/from = "slush"/'
expect transition-extra-key 'transition #0: expected exactly the keys' 's/^from = "ice"$/&\npressure = 1/'
expect transition-duplicate 'the enthalpy offset of lava is already defined' 's/^to = "molten_metal"$/to = "lava"/'
expect transition-order 'the enthalpy offset of water must be defined before' \
    's/^from = "ice"$/from = "tmp"/;s/^from = "water"$/from = "ice"/;s/^from = "tmp"$/from = "water"/;s/^to = "water"$/to = "tmp"/;s/^to = "steam"$/to = "water"/;s/^to = "tmp"$/to = "steam"/'
expect transition-into-alias 'the enthalpy offset of acid is already defined' 's/^to = "lava"$/to = "acid"/'
expect swapped-order 'append-only' 's/^id = "sand"$/id = "tmp"/;s/^id = "water"$/id = "sand"/;s/^id = "tmp"$/id = "water"/'
last_table=$(tr -d '\r' <"$root/src/sim/materials.toml" | grep -n '^\[\[material\]\]$' | tail -n 1 | cut -d: -f1)
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
if "$python" "$root/scripts/gen_materials.py" --check --glsl "$work/stale.hpp" 2>/dev/null; then
    echo "FAIL: stale GLSL output passed --check"
    failures=$((failures + 1))
fi

if [[ $failures -ne 0 ]]; then
    echo "test-gen-materials: $failures failure(s)"
    exit 1
fi
echo "test-gen-materials: OK"
