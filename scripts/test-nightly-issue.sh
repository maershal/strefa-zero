#!/usr/bin/env bash
# Dry runs of scripts/nightly-issue.sh: no GH_TOKEN, no network.
# Usage: ./scripts/test-nightly-issue.sh
set -u
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
script="$root/scripts/nightly-issue.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failures=0

unset GH_TOKEN GITHUB_TOKEN
export GITHUB_SERVER_URL=https://github.com GITHUB_REPOSITORY=owner/repo GITHUB_RUN_ID=42 GITHUB_RUN_NUMBER=7 \
    GITHUB_REF_NAME=main GITHUB_SHA=0123abc
run_url=https://github.com/owner/repo/actions/runs/42
printf '| Testy | 3 |\n' >"$work/summary.md"

# expect NAME STATUS PATTERN... -- ARGS: runs the script, checks the exit code and that every pattern occurs in
# the combined output.
expect() {
    local name=$1 status=$2
    shift 2
    local patterns=()
    while [[ $1 != -- ]]; do patterns+=("$1"); shift; done
    shift
    local output actual
    output=$("$script" "$@" 2>&1)
    actual=$?
    local ok=true
    [[ $actual == "$status" ]] || ok=false
    for pattern in "${patterns[@]}"; do grep -qF -- "$pattern" <<<"$output" || ok=false; done
    if [[ $ok == true ]]; then
        echo "ok   $name"
    else
        echo "FAIL $name (exit $actual, expected $status)"
        sed 's/^/     /' <<<"$output"
        failures=$((failures + 1))
    fi
}

expect 'new issue with summary' 0 \
    "Nocny przebieg TSan [#7]($run_url) na \`main\` (0123abc) nie przeszedł." '| Testy | 3 |' \
    '**Artefakty** (logi):' 'gh issue create --title Nocny przebieg TSan --label nightly' \
    'gh label create nightly' \
    -- --dry-run --title 'Nocny przebieg TSan' --run-name TSan --summary "$work/summary.md" --artifacts-note '(logi)'
NIGHTLY_DRY_RUN_ISSUE=12 expect 'existing issue is updated' 0 \
    'gh issue edit 12 --body-file' "gh issue comment 12 --body Kolejny nieudany przebieg: $run_url" \
    -- --dry-run --title 'Nocny przebieg TSan' --run-name TSan --summary "$work/summary.md"
expect 'missing summary uses the fallback' 0 'Brak podsumowania (build: failure).' \
    -- --dry-run --title T --run-name GPU --summary "$work/missing.md" --fallback 'Brak podsumowania (build: failure).'
expect 'skipped artifact is filtered' 0 'select(true and .name != "nightly-gpu-build")' \
    -- --dry-run --title T --run-name GPU --skip-artifact nightly-gpu-build
expect 'issue is matched by title' 0 'select(.title == "Nocny przebieg GPU: testy")' \
    -- --dry-run --title 'Nocny przebieg GPU: testy' --run-name GPU
expect 'title is required' 2 'wymagane --title' -- --dry-run --run-name GPU
expect 'quote in title is rejected' 2 'tytuł nie może' -- --dry-run --title 'a"b' --run-name GPU
expect 'unknown option is rejected' 2 'nieznana opcja --bogus' -- --dry-run --bogus
# In a subshell, so the variable stays set for later cases; its failure count comes back as the exit code.
(failures=0; unset GITHUB_RUN_ID; expect 'missing run id is rejected' 2 'brak zmiennej GITHUB_RUN_ID' \
    -- --dry-run --title T --run-name GPU; exit "$failures") || failures=$((failures + 1))

if [[ $failures -gt 0 ]]; then
    echo "$failures nightly-issue.sh test(s) failed"
    exit 1
fi
echo 'nightly-issue.sh: all tests passed'
