#!/usr/bin/env bash
# PreToolUse(Bash): refuse shell commands that read raw measurement evidence.
# Reports in docs/reports/ quote the numbers that matter; the raw data under
# docs/measurements/ is large, byte-identical evidence. git commands are allowed
# so files can still be moved, staged and diffed by name. Recursive grep over docs/
# or the repository root must exclude measurements (ripgrep is covered by /.ignore).
# Exit 2 blocks the call; any other non-zero code is non-blocking in Claude Code,
# so every failure path below exits 2 as well.

if ! command -v jq > /dev/null 2>&1; then
    echo 'Blokada docs/measurements: brak programu jq (potrzebny do odczytu JSON-a z hooka). Zainstaluj jq, np. "sudo apt install jq", "brew install jq" lub "winget install jqlang.jq".' >&2
    exit 2
fi

input=$(cat)
if ! command=$(jq -r '.tool_input.command // ""' <<< "$input" 2> /dev/null); then
    echo 'Blokada docs/measurements: nie udało się sparsować JSON-a przekazanego przez Claude Code.' >&2
    exit 2
fi

block() {
    echo "Zablokowano: $1 Zajrzyj do raportu w docs/reports/ albo poproś użytkownika o zdjęcie blokady." >&2
    exit 2
}

# git is allowed (mv, add, diff by name) unless it is chained with another command.
if [[ $command =~ ^[[:space:]]*git[[:space:]] && ! $command =~ [\;\&\|\`] && $command != *'$('* ]]; then
    exit 0
fi

# --exclude-dir=measurements is the sanctioned way to search docs/, so it is not a read.
scan=${command//--exclude-dir=[^[:space:]]*/}
if [[ $scan == *measurements/* || $scan == *measurements[[:space:]]* || $scan == *measurements ]]; then
    block 'docs/measurements/ zawiera surowe dane pomiarowe (~12 tys. plików).'
fi

# ripgrep honours /.ignore; refuse the flags that switch ignore files off.
if [[ $command =~ (^|[[:space:];\&\|])rg[[:space:]] &&
    $command =~ [[:space:]](--no-ignore[a-z-]*|-u+|-[a-zA-Z]*u[a-zA-Z]*)([[:space:]]|$) ]]; then
    block 'rg z --no-ignore/-u przeszukałby docs/measurements/.'
fi

# Recursive grep: inspect the arguments of every grep invocation.
read -r -a words <<< "${command//[;&|()]/ ; }"
in_grep=0
recursive=0
excluded=0
positionals=0
has_pattern_flag=0
hits_root=0
finish_grep() {
    ((in_grep && recursive && !excluded)) || return 0
    local paths=$((positionals - (has_pattern_flag ? 0 : 1)))
    if ((paths <= 0 || hits_root)); then
        block 'rekurencyjny grep po docs/ lub katalogu głównym przeszukałby docs/measurements/. Dodaj --exclude-dir=measurements.'
    fi
}
skip_next=0
for word in "${words[@]}" ";"; do
    if ((skip_next)); then
        skip_next=0
        continue
    fi
    if [[ $word == ";" ]]; then
        finish_grep
        in_grep=0 recursive=0 excluded=0 positionals=0 has_pattern_flag=0 hits_root=0
        continue
    fi
    if ((!in_grep)); then
        [[ $word == grep || $word == egrep || $word == fgrep || $word == */grep ]] && in_grep=1
        continue
    fi
    case $word in
        --exclude-dir=*measurements*) excluded=1 ;;
        --recursive | --dereference-recursive) recursive=1 ;;
        -e | -f | --regexp | --file)
            has_pattern_flag=1
            skip_next=1
            ;;
        --*) ;;
        -*)
            [[ $word =~ ^-[a-zA-Z]*[rR] ]] && recursive=1
            [[ $word =~ ^-[a-zA-Z]*[ef]$ ]] && has_pattern_flag=1 skip_next=1
            ;;
        *)
            positionals=$((positionals + 1))
            case $word in
                docs | docs/ | ./docs | ./docs/ | . | ./ | .. | ../ | '*' | ./'*' | "$PWD" | "$PWD"/) hits_root=1 ;;
            esac
            ;;
    esac
done

exit 0
