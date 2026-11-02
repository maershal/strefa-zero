#!/usr/bin/env bash
# PR hygiene checks run by .github/workflows/pr-hygiene.yml; also runnable locally, e.g.:
#   gh pr view 13 --json body --jq .body > /tmp/body.md
#   ./scripts/pr-hygiene.sh --base origin/main --head origin/perf/scene-benchmark --body-file /tmp/body.md
set -euo pipefail

usage() {
    echo "Użycie: $0 --base <ref> --head <ref> --body-file <plik> [--labels \"a,b\"]" >&2
    exit 2
}

base=""
head=""
body_file=""
labels=""
while [[ $# -gt 0 ]]; do
    case "$1" in
    --base) base="${2:-}"; shift 2 ;;
    --head) head="${2:-}"; shift 2 ;;
    --body-file) body_file="${2:-}"; shift 2 ;;
    --labels) labels="${2:-}"; shift 2 ;;
    *) usage ;;
    esac
done
[[ -n "$base" && -n "$head" && -n "$body_file" && -f "$body_file" ]] || usage

readonly max_file_bytes=1048576
readonly max_code_lines=800
readonly large_label="large-pr-approved"
# The template is in English; "|" lists the Polish headings of older descriptions, still accepted.
readonly sections=(
    "Goal"
    "Changes"
    "Tests"
    "Risks"
    "Out of scope"
    "Contracts touched?|Kontrakty dotknięte?"
    "GPU tests not run locally|Testy GPU nieuruchomione lokalnie"
    "Pre-merge checks|Kontrole przed scaleniem"
)
readonly required_content=("Goal" "Changes" "Tests")

failures=0
# Workflow commands treat %, CR and LF specially; escape them so file names cannot break annotations.
annotation() {
    local text="${1//%/%25}"
    text="${text//$'\r'/%0D}"
    printf '%s\n' "${text//$'\n'/%0A}"
}
fail() {
    failures=$((failures + 1))
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::error::$(annotation "$1")"
    else
        echo "BŁĄD: $1"
    fi
}
warn() {
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        echo "::warning::$(annotation "$1")"
    else
        echo "UWAGA: $1"
    fi
}

merge_base=$(git merge-base "$base" "$head")

# --- 1. Description: not empty, every template section present, key sections filled in.
body=$(tr -d '\r' <"$body_file" | perl -0pe 's/<!--.*?-->//gs')

# Prints the text of section "$1" (lines after its heading, up to the next "## " heading).
section_text() {
    awk -v name="$1" '
        /^##[ \t]/ {
            heading = $0
            sub(/^##[ \t]+/, "", heading)
            sub(/[ \t]+$/, "", heading)
            inside = (heading == name)
            next
        }
        inside { print }
    ' <<<"$body"
}

has_section() {
    grep -qxF -- "## $1" <<<"$(sed -E 's/[[:space:]]+$//' <<<"$body")"
}

# Prints the first heading of "$1" ("name|alias|...") present in the description; fails if none is.
find_section() {
    local name names
    IFS='|' read -ra names <<<"$1"
    for name in "${names[@]}"; do
        if has_section "$name"; then
            printf '%s\n' "$name"
            return 0
        fi
    done
    return 1
}

if [[ -z "${body//[[:space:]]/}" ]]; then
    fail "Opis PR jest pusty. Użyj szablonu .github/pull_request_template.md."
else
    for section in "${sections[@]}"; do
        find_section "$section" >/dev/null ||
            fail "Brak sekcji „## ${section%%|*}” w opisie PR (szablon .github/pull_request_template.md)."
    done
    for section in "${required_content[@]}"; do
        if has_section "$section"; then
            text=$(section_text "$section")
            [[ -n "${text//[[:space:]]/}" ]] || fail "Sekcja „## $section” w opisie PR jest pusta."
        fi
    done
    if checks=$(find_section "Pre-merge checks|Kontrole przed scaleniem"); then
        unchecked=$(section_text "$checks" | grep -cE '^[[:space:]]*[-*][[:space:]]+\[ \]' || true)
        if [[ "$unchecked" -gt 0 ]]; then
            warn "Nieodhaczone pozycje w „$checks”: $unchecked. Odhacz je przed merge."
        fi
    fi
fi

# --- 2./3. Added files: size limit and forbidden measurement paths. Renames are moves, not new data.
while IFS= read -r -d '' path; do
    size=$(git cat-file -s "$head:$path")
    if [[ "$size" -gt "$max_file_bytes" ]]; then
        fail "Plik $path ma $size B (limit $max_file_bytes B). Duże dane trzymaj poza repozytorium."
    fi
    case "$path" in
    docs/measurements/*)
        fail "Ścieżka $path: surowe pomiary należą do repozytorium strefa-zero-measurements." ;;
    assets/*) ;;
    *.csv | *.CSV | *.bmp | *.BMP)
        fail "Plik $path: *.csv i *.bmp poza assets/ to surowe dane pomiarowe (repozytorium strefa-zero-measurements)." ;;
    esac
done < <(git diff -z --name-only --diff-filter=AC "$merge_base" "$head")

# --- 4. Size of the code change (without docs, Markdown, binaries and linguist-generated files).
is_generated() {
    local value
    value=$(git check-attr --source="$head" linguist-generated -- "$1" | sed 's/.*: //')
    [[ "$value" == "true" || "$value" == "set" ]]
}

code_lines=0
while IFS= read -r -d '' record; do
    IFS=$'\t' read -r added deleted path <<<"$record"
    if [[ -z "$path" ]]; then
        # Rename/copy: the record is followed by the old and the new path.
        IFS= read -r -d '' _old_path
        IFS= read -r -d '' path
    fi
    [[ "$added" == "-" ]] && continue
    case "$path" in
    docs/* | *.md) continue ;;
    esac
    is_generated "$path" && continue
    code_lines=$((code_lines + added + deleted))
done < <(git diff -z --numstat "$merge_base" "$head")

has_large_label=0
[[ ",$labels," == *",$large_label,"* ]] && has_large_label=1

echo "Zmienione linie kodu (bez docs/, *.md, binarnych i wygenerowanych): $code_lines (limit $max_code_lines)."
if [[ "$code_lines" -gt "$max_code_lines" && "$has_large_label" -eq 0 ]]; then
    fail "PR zmienia $code_lines linii kodu (> $max_code_lines). Podziel go albo dodaj etykietę $large_label po uzgodnieniu."
fi

if [[ "$failures" -gt 0 ]]; then
    echo "pr-hygiene: błędy: $failures."
    exit 1
fi
echo "pr-hygiene: OK."
