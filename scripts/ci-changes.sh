#!/usr/bin/env bash
# Prints code=true when a change can affect the build, tests or the package, code=false for docs only.
# Usage: scripts/ci-changes.sh BASE HEAD (CI appends the output to $GITHUB_OUTPUT).
# An empty or unknown BASE (new branch, workflow_dispatch, force push) means code=true.
set -euo pipefail

task_base=${1:-}
task_head=${2:-HEAD}
if [[ -z $task_base || $task_base =~ ^0+$ ]] || ! git cat-file -e "$task_base^{commit}" 2>/dev/null; then
    echo code=true
    exit 0
fi
task_code=false
while IFS= read -r task_path; do
    case "$task_path" in
        # Shipped in the runtime package as README.md (CMakeLists.txt install rules).
        docs/LINUX_WSL.md|docs/QUICKSTART.md) task_code=true ;;
        docs/*|*.md) ;;
        *) task_code=true ;;
    esac
done < <(git diff --name-only "$task_base" "$task_head")
echo "code=$task_code"
