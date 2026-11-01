#!/usr/bin/env bash
# Source from the build/test and game launchers; never override user choices.
# Some WSL Mesa installations select llvmpipe even with a working vGPU.
if [[ -e /dev/dxg && -z ${GALLIUM_DRIVER:-} && -z ${LIBGL_ALWAYS_SOFTWARE:-} ]]; then
    for task_driver in /usr/lib/*/dri/d3d12_dri.so /usr/lib64/dri/d3d12_dri.so /usr/lib/dri/d3d12_dri.so; do
        if [[ -f $task_driver ]]; then
            export GALLIUM_DRIVER=d3d12
            break
        fi
    done
    unset task_driver
fi
