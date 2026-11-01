#!/usr/bin/env bash
# Format tracked C++ sources with the repository .clang-format.
#   ./scripts/format.sh          rewrite files in place
#   ./scripts/format.sh --check  fail if any file needs formatting (CI)
set -euo pipefail

project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
clang_format=${CLANG_FORMAT:-clang-format}
command -v "$clang_format" >/dev/null || { echo "Missing tool: $clang_format" >&2; exit 2; }

mode=(-i)
case "${1:-}" in
    --check) mode=(--dry-run --Werror) ;;
    '') ;;
    *) echo 'Usage: ./scripts/format.sh [--check]' >&2; exit 2 ;;
esac

cd -- "$project"
git ls-files -z -- '*.cpp' '*.hpp' '*.h' '*.inl' 'cmake/*.hpp.in' ':!docs/**' | xargs -0 "$clang_format" "${mode[@]}"
