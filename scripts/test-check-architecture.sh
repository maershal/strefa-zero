#!/usr/bin/env bash
# Regression tests for scripts/check_architecture.py on a throwaway copy of the sources.
# Usage: ./scripts/test-check-architecture.sh
set -u
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
check="$root/scripts/check_architecture.py"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failures=0

mkdir -p "$work/repo/assets"
cp -r "$root/src" "$root/tests" "$root/tools" "$root/CMakeLists.txt" "$work/repo/"
cp -r "$root/assets/shaders" "$work/repo/assets/"
cd "$work/repo" || exit 1
git init -q -b main
git config user.name test
git config user.email test@example.invalid
git config core.autocrlf false
git add -A
git commit -qm base

fail() {
    echo "FAIL: $*"
    failures=$((failures + 1))
}

# expect <pass|message> <case> <command...>: applies the command on a branch from main and runs the check with
# --base main. `pass` expects exit 0; anything else expects exit 1 and that text in the output (no traceback).
expect() {
    local want=$1 name=$2 status=0
    shift 2
    git switch -q -C "$name" main
    "$@"
    python3 "$check" --root . --base main >"$work/out.txt" 2>&1 || status=$?
    if [[ $want == pass ]]; then
        ((status == 0)) || { fail "$name -> exit $status (expected pass)"; cat "$work/out.txt"; }
    elif ((status != 1)) || ! grep -qF -- "$want" "$work/out.txt" || grep -q Traceback "$work/out.txt"; then
        fail "$name -> exit $status without \"$want\""
        cat "$work/out.txt"
    fi
    git add -A
    git commit -qm "$name" --allow-empty
}

expect pass unchanged true
# A new forbidden include: sim must not see the GPU, and nothing outside StrefaQA may include QA code.
expect 'nowe naruszenie warstw `include sim gpu`' sim-includes-gpu sed -i '1i #include "gpu/world.hpp"' src/sim/air.cpp
expect 'nowe naruszenie warstw `include ui qa`' ui-includes-qa \
    sed -i '1i #include "app/diagnostic_scenarios.hpp"' src/ui/menu.cpp
# One more edge of an allowed pair exceeds its count.
expect '`include ui gpu`: 3 > 2' ui-includes-gpu-again sed -i '1i #include "gpu/world.hpp"' src/ui/menu.cpp
expect 'nowe naruszenie warstw `link sim gpu`' new-link \
    sed -i 's/target_link_libraries(strefa_sim PUBLIC strefa_options/& strefa_gpu/' CMakeLists.txt
expect 'world_cpp_lines' world-grows sed -i '$a // one more line' src/gpu/world.cpp
expect 'size_literals_cpp' size-literal sed -i '$a constexpr int extra_width = 320;' src/sim/air.cpp
expect 'size_literals_glsl' glsl-literal sed -i '$a const int extraHeight = 192;' assets/shaders/particle_common.glsl
expect 'particle_world_test_hooks' test-hook \
    sed -i 's/    void apply_fluid_velocities_for_test();/&\n    void reset_for_test();/' src/gpu/world.hpp
# An unknown target is reported, not a traceback.
expect 'Nieznany target CMake: strefa_extra' unknown-target sed -i '$a add_library(strefa_extra STATIC src/sim/air.cpp)' \
    CMakeLists.txt

# Paying a debt off without lowering the budget fails; --update writes the lower value and passes.
git switch -q -C pay-off main
sed -i '/#include "sim\/material.hpp"/d' src/gpu/thermal.hpp
python3 "$check" --root . >"$work/out.txt" 2>&1 && fail "unrecorded pay-off passed"
grep -qF 'dług spadł' "$work/out.txt" || fail "unrecorded pay-off: no hint to --update"
python3 "$check" --root . --update >/dev/null || fail "--update after pay-off"
grep -q '^include gpu sim' tools/architecture_allowlist.txt && fail "allowlist kept gpu sim"
python3 "$check" --root . --base main >/dev/null || fail "recorded pay-off"
git add -A
git commit -qm pay-off

# New debt recorded by hand in the budget or the allowlist passes on its own but fails against the base.
raise_budget() {
    sed -i '$a // one more line' src/gpu/world.cpp
    python3 -c 'import json, sys
budget = json.load(open(sys.argv[1]))
budget["world_cpp_lines"] += 1
budget["world_tu_lines"] += 1
json.dump(budget, open(sys.argv[1], "w"), indent=2)' tools/architecture_budget.json
    python3 "$check" --root . >/dev/null || fail "raised budget does not pass on its own"
}
allow_edge() {
    sed -i '1i #include "gpu/world.hpp"' src/sim/air.cpp
    sed -i '$a include sim gpu 1' tools/architecture_allowlist.txt
    python3 "$check" --root . >/dev/null || fail "extended allowlist does not pass on its own"
}
expect 'world_cpp_lines = 2' raise-budget raise_budget
expect '`include sim gpu 1` przekracza main (0)' allow-new-edge allow_edge

if ((failures)); then
    echo "$failures test(s) failed"
    exit 1
fi
echo "check_architecture.py: all tests passed"
