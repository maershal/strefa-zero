#!/usr/bin/env bash
# Regression tests for scripts/ci-changes.sh on a throwaway repository.
# Usage: ./scripts/test-ci-changes.sh
set -u
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
changes="$root/scripts/ci-changes.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failures=0

git init -q -b main "$work/repo"
cd "$work/repo" || exit 1
git config user.name test
git config user.email test@example.invalid
# Rename detection on regardless of the user's config, as in CI.
git config diff.renames true
git config core.autocrlf false
mkdir -p src docs
printf 'int x;\nint y;\nint z;\n' >src/x.cpp
printf '# Notes\n' >docs/NOTES.md
printf '# Linux\n' >docs/LINUX_WSL.md
git add -A
git commit -qm base

# expect <code=true|code=false> <case> <command...>: runs the command on a branch from main and commits it.
expect() {
    local want=$1 name=$2 got
    shift 2
    git switch -q -C "$name" main
    "$@"
    git add -A
    git commit -qm "$name"
    got=$("$changes" main HEAD)
    if [[ $got != "$want" ]]; then
        echo "FAIL: $name -> $got (expected $want)"
        failures=$((failures + 1))
    fi
}

expect code=false docs-only eval 'printf "more\n" >>docs/NOTES.md'
expect code=false markdown-at-root eval 'printf "# R\n" >README.md'
expect code=true src-edit eval 'printf "int w;\n" >>src/x.cpp'
expect code=true packaged-doc eval 'printf "more\n" >>docs/LINUX_WSL.md'
# A move out of src/ removes code from the build: it must not pass as docs-only.
expect code=true rename-src-to-docs git mv src/x.cpp docs/x.cpp
expect code=true rename-src-to-md git mv src/x.cpp src/x.md
expect code=false rename-within-docs git mv docs/NOTES.md docs/MORE.md

got=$("$changes" "" HEAD)
[[ $got == code=true ]] || { echo "FAIL: empty base -> $got"; failures=$((failures + 1)); }
got=$("$changes" 0000000000000000000000000000000000000000 HEAD)
[[ $got == code=true ]] || { echo "FAIL: zero base -> $got"; failures=$((failures + 1)); }

if ((failures > 0)); then
    echo "test-ci-changes: failures: $failures"
    exit 1
fi
echo "test-ci-changes: OK"
