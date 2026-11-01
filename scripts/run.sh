#!/usr/bin/env bash
set -euo pipefail

task_script_directory=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ -x $task_script_directory/StrefaZero ]]; then
    task_build_directory=$task_script_directory
else
    task_build_directory="$(dirname -- "$task_script_directory")/build/linux-release"
fi
while (($#)); do
    case "$1" in
        --build-dir)
            (($# >= 2)) || { echo 'Missing value for --build-dir' >&2; exit 2; }
            task_build_directory=$2; shift 2 ;;
        --software)
            export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
            shift ;;
        -h|--help)
            echo 'Usage: ./scripts/run.sh [--build-dir PATH] [--software] [--] [game arguments]'
            exit 0 ;;
        --) shift; break ;;
        *) break ;;
    esac
done
task_executable="$(realpath -m -- "$task_build_directory")/StrefaZero"
[[ -x $task_executable ]] || {
    echo "Missing Linux executable: $task_executable. Run ./scripts/build.sh first." >&2
    exit 2
}
if [[ -z ${DISPLAY:-} && -z ${WAYLAND_DISPLAY:-} ]]; then
    echo 'No graphical session. Use a Linux desktop or WSL2 with WSLg.' >&2
    exit 2
fi
# shellcheck source=scripts/linux-env.sh
source "$task_script_directory/linux-env.sh"
exec "$task_executable" "$@"
