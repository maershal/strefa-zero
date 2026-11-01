#!/usr/bin/env bash
set -euo pipefail

task_project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
task_build_directory=${1:-"$task_project/build/linux-release"}
task_output_directory=${2:-"$task_project/dist"}
task_build_directory=$(realpath -m -- "$task_build_directory")
task_output_directory=$(realpath -m -- "$task_output_directory")
[[ -x $task_build_directory/StrefaZero ]] || {
    echo 'Build the Linux application before packaging.' >&2; exit 2
}
mkdir -p -- "$task_output_directory"
task_stage=$(mktemp -d -t strefa-package.XXXXXX)
trap 'rm -rf -- "$task_stage"' EXIT
task_bundle="StrefaZero-Linux-$(uname -m)"
cmake --install "$task_build_directory" --component StrefaRuntime --prefix "$task_stage/$task_bundle"
(
    cd -- "$task_stage/$task_bundle"
    find . -type f ! -name SHA256SUMS.txt -print0 | sort -z | xargs -0 sha256sum > "$task_stage/manifest"
    mv -- "$task_stage/manifest" SHA256SUMS.txt
)
tar -C "$task_stage" -czf "$task_output_directory/$task_bundle.tar.gz" "$task_bundle"
(
    cd -- "$task_output_directory"
    sha256sum "$task_bundle.tar.gz" > "$task_bundle.tar.gz.sha256"
)
printf 'Package ready: %s/%s.tar.gz\n' "$task_output_directory" "$task_bundle"
