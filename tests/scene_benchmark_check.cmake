# Content checks for the gpu.scene_benchmark output (CTest gpu.scene_benchmark_content):
#   cmake -DPROFILE=<profile dir> -P tests/scene_benchmark_check.cmake
# Every scene measured frames in a visible window with status ok; GPU scenes produced GPU samples.
# No timing thresholds: the numbers are informational on every renderer.
# 3.28 policies: empty CSV cells (e.g. status_reason) stay list elements (CMP0007).
cmake_minimum_required(VERSION 3.28)
if(NOT PROFILE)
    message(FATAL_ERROR "Brak -DPROFILE=<katalog profilu benchmarku>.")
endif()
set(summary "${PROFILE}/scene-benchmark.csv")
set(info "${PROFILE}/scene-benchmark-info.txt")
# gpu.scene_benchmark exited 77 (no OpenGL 4.3) and wrote gpu-skipped.txt instead of results: this check is skipped
# too (SKIP_REGULAR_EXPRESSION in CMakeLists.txt). With STREFA_REQUIRE_GL43=1 the file says required=1 and it fails.
if(EXISTS "${PROFILE}/gpu-skipped.txt")
    file(READ "${PROFILE}/gpu-skipped.txt" skipped)
    if(NOT skipped MATCHES "required=1")
        message("STREFA_SKIPPED: brak OpenGL 4.3, benchmark scen pominięty.")
        return()
    endif()
endif()
foreach(file IN ITEMS "${summary}" "${info}")
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "Brak pliku wyników: ${file}")
    endif()
endforeach()

file(STRINGS "${info}" visible REGEX "^window_visible=")
if(NOT visible STREQUAL "window_visible=1")
    message(FATAL_ERROR "Okno benchmarku nie było widoczne na starcie (${visible}).")
endif()

file(STRINGS "${summary}" lines)
list(LENGTH lines line_count)
if(line_count LESS 2)
    message(FATAL_ERROR "scene-benchmark.csv nie ma wierszy scen.")
endif()
list(GET lines 0 header)
string(REPLACE "," ";" columns "${header}")
foreach(name IN ITEMS scene status frames physics_samples presentation_samples render_samples hidden_frames
        minimized_frames occluded_frames cloaked_frames)
    list(FIND columns ${name} column_${name})
    if(column_${name} LESS 0)
        message(FATAL_ERROR "scene-benchmark.csv: brak kolumny ${name}.")
    endif()
endforeach()

list(SUBLIST lines 1 -1 rows)
foreach(row IN LISTS rows)
    string(REPLACE "," ";" cells "${row}")
    foreach(name IN ITEMS scene status frames physics_samples presentation_samples render_samples hidden_frames
            minimized_frames occluded_frames cloaked_frames)
        list(GET cells ${column_${name}} ${name})
    endforeach()
    if(NOT status STREQUAL "ok")
        message(FATAL_ERROR "${scene}: status ${status} (wiersz: ${row}).")
    endif()
    if(NOT frames GREATER 0)
        message(FATAL_ERROR "${scene}: brak zmierzonych klatek.")
    endif()
    if(NOT hidden_frames EQUAL 0 OR NOT minimized_frames EQUAL 0 OR NOT occluded_frames EQUAL 0
            OR NOT cloaked_frames EQUAL 0)
        message(FATAL_ERROR "${scene}: okno niewidoczne w ${hidden_frames}, zminimalizowane w ${minimized_frames}, "
                "zasłonięte w ${occluded_frames}, ukryte przez DWM w ${cloaked_frames} klatkach.")
    endif()
    if(NOT scene MATCHES "^lab:")
        if(NOT physics_samples GREATER 0 OR NOT render_samples GREATER 0)
            message(FATAL_ERROR "${scene}: brak próbek GPU (fizyka ${physics_samples}, render ${render_samples}).")
        endif()
        if(NOT presentation_samples EQUAL physics_samples)
            message(FATAL_ERROR "${scene}: ${presentation_samples} próbek prezentacji przy ${physics_samples} fizyki.")
        endif()
    endif()
    message(STATUS "${scene}: ${frames} klatek, ${physics_samples} próbek fizyki GPU, status ${status}")
endforeach()
