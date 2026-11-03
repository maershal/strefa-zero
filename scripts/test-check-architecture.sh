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

# expect <pass|fail> <case> <command...>: applies the command on a branch from main and runs the check.
expect() {
    local want=$1 name=$2 got=pass
    shift 2
    git switch -q -C "$name" main
    "$@"
    python3 "$check" --root . --base main >"$work/out.txt" 2>&1 || got=fail
    if [[ $got != "$want" ]]; then
        echo "FAIL: $name -> $got (expected $want)"
        cat "$work/out.txt"
        failures=$((failures + 1))
    fi
    git add -A
    git commit -qm "$name" --allow-empty
}

expect pass unchanged true
# A new forbidden include: sim must not see the GPU, and nothing outside StrefaQA may include QA code.
expect fail sim-includes-gpu sed -i '1i #include "gpu/world.hpp"' src/sim/air.cpp
expect fail ui-includes-qa sed -i '1i #include "app/diagnostic_scenarios.hpp"' src/ui/menu.cpp
# One more edge of an allowed pair exceeds its count.
expect fail ui-includes-gpu-again sed -i '1i #include "gpu/world.hpp"' src/ui/menu.cpp
expect fail new-link sed -i 's/target_link_libraries(strefa_sim PUBLIC strefa_options/& strefa_gpu/' CMakeLists.txt
expect fail world-grows sed -i '$a // one more line' src/gpu/world.cpp
expect fail size-literal sed -i '$a constexpr int extra_width = 320;' src/sim/air.cpp
expect fail glsl-literal sed -i '$a // 192' assets/shaders/particle_common.glsl
expect fail test-hook sed -i 's/    void apply_fluid_velocities_for_test();/&\n    void reset_for_test();/' src/gpu/world.hpp
# Paying a debt off without lowering the budget fails; --update writes the lower value and passes.
git switch -q -C pay-off main
sed -i '/#include "sim\/material.hpp"/d' src/gpu/thermal.hpp
python3 "$check" --root . >/dev/null 2>&1 && { echo "FAIL: unrecorded pay-off passed"; failures=$((failures + 1)); }
python3 "$check" --root . --update >/dev/null || { echo "FAIL: --update after pay-off"; failures=$((failures + 1)); }
grep -q '^include gpu sim' tools/architecture_allowlist.txt && { echo "FAIL: allowlist kept gpu sim"; failures=$((failures + 1)); }
python3 "$check" --root . --base main >/dev/null || { echo "FAIL: recorded pay-off"; failures=$((failures + 1)); }
# New debt recorded by hand in the budget or the allowlist passes on its own but fails against the base.
raise_budget() {
    sed -i '$a // one more line' src/gpu/world.cpp
    python3 -c 'import json, sys
budget = json.load(open(sys.argv[1]))
budget["world_cpp_lines"] += 1
budget["world_tu_lines"] += 1
json.dump(budget, open(sys.argv[1], "w"), indent=2)' tools/architecture_budget.json
    python3 "$check" --root . >/dev/null || echo "FAIL: raised budget does not pass on its own"
}
allow_edge() {
    sed -i '1i #include "gpu/world.hpp"' src/sim/air.cpp
    sed -i '$a include sim gpu 1' tools/architecture_allowlist.txt
    python3 "$check" --root . >/dev/null || echo "FAIL: extended allowlist does not pass on its own"
}
expect fail raise-budget raise_budget
expect fail allow-new-edge allow_edge

if ((failures)); then
    echo "$failures test(s) failed"
    exit 1
fi
echo "check_architecture.py: all tests passed"
