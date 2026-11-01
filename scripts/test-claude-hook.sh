#!/usr/bin/env bash
# Regression tests for .claude/hooks/block-measurements.sh (needs jq).
# Usage: ./scripts/test-claude-hook.sh
set -u
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
hook="$root/.claude/hooks/block-measurements.sh"
failures=0

json_for() {
    local escaped=${1//\\/\\\\}
    escaped=${escaped//\"/\\\"}
    printf '{"tool_input":{"command":"%s"}}' "$escaped"
}

# expect <exit code> <command>
expect() {
    local want=$1 command=$2 got
    json_for "$command" | "$hook" > /dev/null 2>&1
    got=$?
    if ((got != want)); then
        echo "FAIL: '$command' -> $got (expected $want)"
        failures=$((failures + 1))
    fi
    # the settings.json invocation (through bash) must behave identically
    json_for "$command" | bash "$hook" > /dev/null 2>&1
    got=$?
    if ((got != want)); then
        echo "FAIL (bash): '$command' -> $got (expected $want)"
        failures=$((failures + 1))
    fi
}

if [[ ! -x $hook ]]; then
    echo "FAIL: $hook is not executable"
    exit 1
fi

# direct reads of measurements
expect 2 'cat docs/measurements/x'
expect 2 'ls docs/measurements'
expect 2 'head -n 3 docs/measurements/i11/report.csv'
expect 2 'echo ok; cat docs/measurements/x'
expect 2 'git status && cat docs/measurements/x'
expect 2 'git log | cat docs/measurements/x'
# git by name stays allowed
expect 0 'git mv docs/measurements/a docs/measurements/b'
expect 0 'git add docs/measurements/i11'
expect 0 'git diff --stat -- docs/measurements'
# recursive grep
expect 2 'grep -rn foo docs/'
expect 2 'grep -rn foo docs'
expect 2 'grep -R foo .'
expect 2 'grep --recursive foo ./docs'
expect 2 'grep -rn foo'
expect 2 'grep -rIn -e foo'
expect 2 'cd x && grep -rn foo docs/ | head'
expect 0 'grep -rn foo docs/ --exclude-dir=measurements'
expect 0 'grep -rn --exclude-dir=measurements foo .'
expect 0 'grep -rn foo src'
expect 0 'grep -n foo docs/ARCHITECTURE.md'
expect 0 'grep foo file.txt'
expect 0 'echo foo | grep -n foo'
# ripgrep is covered by /.ignore
expect 0 'rg foo'
expect 0 'rg -n foo docs/reports'
expect 2 'rg --no-ignore foo'
expect 2 'rg -uu foo docs'
# unrelated
expect 0 'ls -la'
expect 0 './scripts/build.sh --test-suite cpu'

# malformed input and missing jq must block (exit 2), not fail open
printf 'not json' | "$hook" > /dev/null 2>&1
((($? == 2))) || {
    echo "FAIL: malformed JSON did not exit 2"
    failures=$((failures + 1))
}
message=$(json_for 'ls' | env PATH=/nonexistent "$BASH" "$hook" 2>&1 > /dev/null)
status=$?
if ((status != 2)) || [[ $message != *jq* ]]; then
    echo "FAIL: missing jq -> $status, message '$message'"
    failures=$((failures + 1))
fi

if ((failures)); then
    echo "$failures failure(s)"
    exit 1
fi
echo "Hook OK"
