# Checks that the "Kompilacja shaderów… n/m" screen drew frames while gpu.mission_ui_quick opened the mission
# (CTest gpu.shader_progress_frames):
#   cmake -DPROFILE=<profile dir> -P tests/shader_progress_check.cmake
# gpu.mission_ui_quick runs with empty driver shader caches, so its first start compiles every program. A frame is
# due every 100 ms of compilation; the check requires one only when the compilation took at least 500 ms longer than
# the longest gap between progress callbacks, so a driver that compiles faster than that does not fail it.
cmake_minimum_required(VERSION 3.28)
if(NOT PROFILE)
    message(FATAL_ERROR "Brak -DPROFILE=<katalog profilu gpu.mission_ui_quick>.")
endif()
# gpu.mission_ui_quick exited 77 (no OpenGL 4.3): skip too, unless STREFA_REQUIRE_GL43=1 made it a failure.
if(EXISTS "${PROFILE}/gpu-skipped.txt")
    file(READ "${PROFILE}/gpu-skipped.txt" skipped)
    if(NOT skipped MATCHES "required=1")
        message("STREFA_SKIPPED: brak OpenGL 4.3, test misji pominięty.")
        return()
    endif()
endif()
set(startup "${PROFILE}/gpu-startup.txt")
if(NOT EXISTS "${startup}")
    message(FATAL_ERROR "Brak pliku ${startup}: tryb GPU się nie otworzył.")
endif()
foreach(name IN ITEMS compile_ms progress_gap_ms progress_frames)
    file(STRINGS "${startup}" line REGEX "^${name}=")
    if(NOT line)
        message(FATAL_ERROR "gpu-startup.txt nie ma pola ${name}.")
    endif()
    string(REGEX REPLACE "^${name}=([0-9]+).*" "\\1" ${name} "${line}")
endforeach()
math(EXPR margin "${compile_ms} - ${progress_gap_ms}")
message("compile_ms=${compile_ms} progress_gap_ms=${progress_gap_ms} progress_frames=${progress_frames}")
if(margin LESS 500)
    message("Kompilacja za krótka, by wymagać klatki postępu (${margin} ms < 500 ms); sprawdzono tylko pola.")
elseif(progress_frames LESS 1)
    message(FATAL_ERROR "Ekran „Kompilacja shaderów” nie narysował żadnej klatki przez ${compile_ms} ms kompilacji.")
endif()
