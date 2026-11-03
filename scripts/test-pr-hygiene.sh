#!/usr/bin/env bash
# Regression tests for scripts/pr-hygiene.sh on a throwaway repository (needs git >= 2.40, perl).
# Usage: ./scripts/test-pr-hygiene.sh
set -u
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
hygiene="$root/scripts/pr-hygiene.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
repo="$work/repo"
failures=0

git init -q -b main "$repo"
cd "$repo" || exit 1
git config user.name test
git config user.email test@example.invalid
git config core.autocrlf false
mkdir -p src docs gen
printf 'gen/*.txt linguist-generated=true\n' >.gitattributes
printf 'int a;\n' >src/a.cpp
printf 'int x;\n' >src/x.cpp
printf '# Historia zmian\n' >docs/CHANGELOG.md
git add -A
git commit -qm base

approval='https://github.com/maershal/strefa-zero/pull/27#issuecomment-123456'

# body [out-of-scope text] [contracts text]
body() {
    local out_of_scope=${1-"- Faster thermal module (#31)"}
    local contracts=${2-"- [ ] SSBO 0-7 (bindings)
- [x] None"}
    cat <<EOF
## Goal

Test.

## Changes

Test.

## Tests

Test.

## Risks

None.

## Out of scope

<!-- comments are ignored: - item without an issue -->
$out_of_scope

## Contracts touched?

$contracts

## GPU tests not run locally

none

## Pre-merge checks

- [x] CI finished and green
EOF
}

# change <name> <command...>: a branch from main with one commit made by the command.
change() {
    local name=$1
    shift
    git switch -q -C "$name" main
    "$@"
    git add -A
    git commit -qm "$name"
}

# expect <0|1> <case> <message substring or ""> [extra pr-hygiene args...]; reads the body from $work/body.md.
expect() {
    local want=$1 name=$2 needle=$3 got output
    shift 3
    output=$(env -u GITHUB_ACTIONS "$hygiene" --base main --head "$name" --body-file "$work/body.md" "$@" 2>&1)
    got=$?
    ((got == 0)) || got=1
    if ((got != want)); then
        echo "FAIL: $name -> exit $got (expected $want)"
        printf '%s\n' "$output" | sed 's/^/    /'
        failures=$((failures + 1))
    elif [[ -n $needle && $output != *"$needle"* ]]; then
        echo "FAIL: $name: output lacks '$needle'"
        printf '%s\n' "$output" | sed 's/^/    /'
        failures=$((failures + 1))
    fi
}

src_with_changelog() {
    printf 'int b;\n' >>src/a.cpp
    printf '\n## Zmiana\n' >>docs/CHANGELOG.md
}
change src-changelog src_with_changelog
change src-only eval 'printf "int b;\n" >>src/a.cpp'
change docs-only eval 'printf "x\n" >docs/NOTE.md'
change rename-out-of-src git mv src/x.cpp docs/x.cpp
change generated eval 'seq 900 >gen/big.txt'
large_src() {
    seq 900 | sed 's/.*/int v&;/' >src/big.cpp
    printf '\n## Duża zmiana\n' >>docs/CHANGELOG.md
}
change large-src large_src

body >"$work/body.md"
# CHANGELOG entry for src/
expect 0 src-changelog ""
expect 1 src-only "docs/CHANGELOG.md"
expect 0 docs-only ""
expect 1 rename-out-of-src "docs/CHANGELOG.md"
# linguist-generated files do not count towards the 800-line limit
expect 0 generated "Zmienione linie kodu (bez docs/, *.md, binarnych i wygenerowanych): 0"
expect 1 large-src "900 linii kodu"
expect 0 large-src "" --labels "large-pr-approved"

# Out of scope: every top-level item needs #NNN
body "- Faster thermal module" >"$work/body.md"
expect 1 docs-only "Pozycja „Out of scope” bez issue (#NNN): - Faster thermal module"
body "- First (#31)
- Second, tracked elsewhere" >"$work/body.md"
expect 1 docs-only "bez issue (#NNN): - Second"
body "1. Numbered item without issue" >"$work/body.md"
expect 1 docs-only "bez issue"
body "- Owner/repo form maershal/strefa-zero#31
- URL form https://github.com/maershal/strefa-zero/issues/32
* Star bullet (#33)
  - indented detail without its own issue

Nothing else; prose is not an item." >"$work/body.md"
expect 0 docs-only ""
body "" >"$work/body.md"
expect 0 docs-only ""
# A PR comment link is not an issue reference.
body "- See https://github.com/maershal/strefa-zero/pull/27#issuecomment-1" >"$work/body.md"
expect 1 docs-only "bez issue"

# Contracts: a checked item needs an approval comment URL on its line
body "" "- [x] GL 3.3 fallback
- [ ] None" >"$work/body.md"
expect 1 docs-only "Zaznaczony kontrakt „GL 3.3 fallback”"
body "" "- [x] GL 3.3 fallback (approval: https://github.com/maershal/strefa-zero/pull/27)" >"$work/body.md"
expect 1 docs-only "GL 3.3 fallback"
body "" "- [x] GL 3.3 fallback — approval: $approval
- [X] Test thresholds and tolerances: https://github.com/maershal/strefa-zero/pull/24#discussion_r987
- [x] 15-option limit of StrefaZero https://github.com/maershal/strefa-zero/issues/15#issuecomment-55" >"$work/body.md"
expect 0 docs-only ""
body "" "- [x] Żaden" >"$work/body.md"
expect 0 docs-only ""

if ((failures > 0)); then
    echo "test-pr-hygiene: failures: $failures"
    exit 1
fi
echo "test-pr-hygiene: OK"
