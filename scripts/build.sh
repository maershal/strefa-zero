#!/usr/bin/env bash
set -euo pipefail

task_project=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
task_configuration=Release
task_build_directory=
task_dependency_cache=
task_core_only=OFF
task_skip_tests=false
task_test_suite=smoke
task_jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-4}
task_cmake_arguments=()

usage() {
    cat <<'HELP'
Usage: ./scripts/build.sh [options]
  --configuration Release|Debug  Build configuration (default: Release)
  --build-dir PATH               Separate Linux build directory
  --dependency-cache PATH        Directory with pinned SDL3/ImGui archives
  --jobs N                       Parallel compiler jobs (default: 4)
  --core-only                    Build physics and CPU tests without SDL/OpenGL
  --test-suite cpu|smoke|all      CPU tests, CPU + window smoke tests, or full QA
  --skip-tests                   Build without running tests
  -h, --help                     Show this help
Additional CMake options can follow --.
Requires Linux, Bash, CMake >= 3.28, Ninja and a C++23 compiler (GCC >= 13).
HELP
}

while (($#)); do
    case "$1" in
        --configuration|--build-dir|--dependency-cache|--jobs|--test-suite)
            (($# >= 2)) || { echo "Missing value for $1" >&2; exit 2; }
            case "$1" in
                --configuration) task_configuration=$2 ;;
                --build-dir) task_build_directory=$2 ;;
                --dependency-cache) task_dependency_cache=$2 ;;
                --jobs) task_jobs=$2 ;;
                --test-suite) task_test_suite=$2 ;;
            esac
            shift 2 ;;
        --core-only) task_core_only=ON; shift ;;
        --skip-tests) task_skip_tests=true; shift ;;
        -h|--help) usage; exit 0 ;;
        --) shift; task_cmake_arguments=("$@"); break ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done
[[ $(uname -s) == Linux ]] || { echo 'Run this script in Linux or WSL2.' >&2; exit 2; }
case "$task_configuration" in Release|Debug) ;; *) echo 'Use Release or Debug.' >&2; exit 2 ;; esac
case "$task_test_suite" in cpu|smoke|all) ;; *) echo 'Use cpu, smoke or all.' >&2; exit 2 ;; esac
[[ $task_jobs =~ ^[1-9][0-9]*$ ]] || { echo '--jobs must be positive.' >&2; exit 2; }
for task_tool in cmake ninja ctest; do
    command -v "$task_tool" >/dev/null || { echo "Missing tool: $task_tool" >&2; exit 2; }
done
if [[ -z $task_build_directory ]]; then
    task_build_directory="$task_project/build/linux-${task_configuration,,}"
    [[ $task_core_only == OFF ]] || task_build_directory+=-core
fi
task_build_directory=$(realpath -m -- "$task_build_directory")
task_configure=(-S "$task_project" -B "$task_build_directory" -G Ninja
    "-DCMAKE_BUILD_TYPE=$task_configuration" -DSTREFA_WARNINGS_AS_ERRORS=ON
    -DBUILD_TESTING=ON "-DSTREFA_BUILD_APP=$([[ $task_core_only == ON ]] && echo OFF || echo ON)")
if [[ -n $task_dependency_cache ]]; then
    task_configure+=("-DSTREFA_DOWNLOAD_CACHE=$(realpath -m -- "$task_dependency_cache")")
fi
cmake "${task_configure[@]}" "${task_cmake_arguments[@]}"
cmake --build "$task_build_directory" --parallel "$task_jobs"
if [[ $task_skip_tests == false ]]; then
    source "$task_project/scripts/linux-env.sh"
    if [[ $task_test_suite == all ]]; then
        ctest --test-dir "$task_build_directory" --output-on-failure --parallel 1
    else
        ctest --test-dir "$task_build_directory" --output-on-failure -LE graphics
        if [[ $task_core_only == OFF && $task_test_suite == smoke ]]; then
            ctest --test-dir "$task_build_directory" --output-on-failure --parallel 1 \
                -R '^(app\.opengl(33)?_smoke|gpu\.(platform_dispatch|gameplay|laboratory_ui))$'
        fi
    fi
fi
printf 'Build ready: %s\n' "$task_build_directory"
