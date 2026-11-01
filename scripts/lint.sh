#!/usr/bin/env bash
# Run clang-tidy with the repository .clang-tidy over project translation units.
#   ./scripts/lint.sh [--build-dir PATH] [files...]
# Needs a configured build (compile_commands.json); defaults to build/linux-release.
set -euo pipefail

project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_directory="$project/build/linux-release"
clang_tidy=${CLANG_TIDY:-clang-tidy}
if [[ ${1:-} == --build-dir ]]; then
    (($# >= 2)) || { echo 'Missing value for --build-dir' >&2; exit 2; }
    build_directory=$2
    shift 2
fi
command -v "$clang_tidy" >/dev/null || { echo "Missing tool: $clang_tidy" >&2; exit 2; }
[[ -f $build_directory/compile_commands.json ]] || {
    echo "No compile_commands.json in $build_directory. Run ./scripts/build.sh first." >&2
    exit 2
}

cd -- "$project"
if (($#)); then
    files=("$@")
else
    mapfile -t files < <(git ls-files -- 'src/*.cpp' 'tests/*.cpp')
fi
if command -v run-clang-tidy >/dev/null; then
    run-clang-tidy -quiet -p "$build_directory" "${files[@]}"
else
    "$clang_tidy" --quiet -p "$build_directory" "${files[@]}"
fi
