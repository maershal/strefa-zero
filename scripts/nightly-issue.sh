#!/usr/bin/env bash
# Opens or updates the issue of a failed nightly workflow (nightly.yml, nightly-gpu.yml).
# One open issue per title, all labelled `nightly`: an open issue with the same title gets the new body and a
# comment with the run link, otherwise a new issue is created.
#
# Usage: scripts/nightly-issue.sh --title TITLE --run-name NAME [--summary FILE] [--fallback TEXT]
#                                 [--artifacts-note TEXT] [--skip-artifact NAME]... [--dry-run]
#   --summary FILE        Markdown test summary (junit-summary.py); --fallback is used when it is missing
#   --skip-artifact NAME  artifact left out of the list (e.g. a build tree passed between jobs)
#   --dry-run             print the issue body and the gh commands instead of running them; needs no GH_TOKEN.
#                         NIGHTLY_DRY_RUN_ISSUE=<number> simulates an open issue with the same title.
# Reads GITHUB_SERVER_URL, GITHUB_REPOSITORY, GITHUB_RUN_ID, GITHUB_RUN_NUMBER, GITHUB_REF_NAME and GITHUB_SHA.
set -euo pipefail

title= run_name= summary= fallback='Brak podsumowania JUnit.' note= dry_run=false
skipped=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --title | --run-name | --summary | --fallback | --artifacts-note | --skip-artifact)
            [[ $# -ge 2 ]] || { echo "nightly-issue.sh: brak wartości dla $1" >&2; exit 2; }
            case $1 in
                --title) title=$2 ;;
                --run-name) run_name=$2 ;;
                --summary) summary=$2 ;;
                --fallback) fallback=$2 ;;
                --artifacts-note) note=" $2" ;;
                --skip-artifact) skipped+=("$2") ;;
            esac
            shift 2 ;;
        --dry-run) dry_run=true; shift ;;
        *) echo "nightly-issue.sh: nieznana opcja $1" >&2; exit 2 ;;
    esac
done
[[ -n $title && -n $run_name ]] || { echo "nightly-issue.sh: wymagane --title i --run-name" >&2; exit 2; }
# The title goes into a jq string literal below.
[[ $title != *'"'* && $title != *'\'* ]] || { echo "nightly-issue.sh: tytuł nie może zawierać \" ani \\" >&2; exit 2; }
for name in GITHUB_SERVER_URL GITHUB_REPOSITORY GITHUB_RUN_ID GITHUB_RUN_NUMBER GITHUB_REF_NAME GITHUB_SHA; do
    [[ -n ${!name:-} ]] || { echo "nightly-issue.sh: brak zmiennej $name" >&2; exit 2; }
done

if [[ $dry_run == true ]]; then
    # Prints every call (arguments joined by spaces) to stderr; of the reads, the artifact list is empty and the
    # issue search returns NIGHTLY_DRY_RUN_ISSUE.
    gh() { echo "gh $*" >&2; }
    gh_read() {
        gh "$@"
        if [[ "$1 $2" == 'issue list' ]]; then printf '%s\n' "${NIGHTLY_DRY_RUN_ISSUE:-}"; fi
    }
else
    gh_read() { gh "$@"; }
fi

run_url="$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID"
body=$(mktemp)
trap 'rm -f "$body"' EXIT
skip_filter=
for name in "${skipped[@]}"; do skip_filter+=" and .name != \"$name\""; done
{
    echo "Nocny przebieg $run_name [#$GITHUB_RUN_NUMBER]($run_url) na \`$GITHUB_REF_NAME\` ($GITHUB_SHA) nie przeszedł."
    echo
    if [[ -n $summary && -s $summary ]]; then cat "$summary"; else echo "$fallback"; fi
    echo
    echo "**Artefakty**$note:"
    gh_read api "repos/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID/artifacts" \
        --jq ".artifacts[] | select(true$skip_filter) | \"- [\(.name)]($run_url/artifacts/\(.id))\""
} >"$body"
if [[ $dry_run == true ]]; then
    echo '--- issue body ---'
    cat "$body"
    echo '--- end ---'
fi

gh label create nightly --color B60205 --description 'Porażki nocnych przebiegów (nightly.yml, nightly-gpu.yml)' --force
number=$(gh_read issue list --label nightly --state open --limit 100 --json number,title \
    --jq "map(select(.title == \"$title\")) | .[0].number // empty")
if [[ -n $number ]]; then
    gh issue edit "$number" --body-file "$body"
    gh issue comment "$number" --body "Kolejny nieudany przebieg: $run_url"
else
    gh issue create --title "$title" --label nightly --body-file "$body"
fi
