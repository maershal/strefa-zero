cmake_minimum_required(VERSION 3.28)
project(StrefaZero VERSION 0.7.0 LANGUAGES C CXX)

include(CTest)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("/std:c++23preview" STREFA_MSVC_CPP23_MODE)
    if(STREFA_MSVC_CPP23_MODE)
        # Some CMake/MSVC releases still map cxx_std_23 to the broader latest mode.
        set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "/std:c++23preview")
        set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "/std:c++23preview")
    endif()
endif()

option(STREFA_BUILD_APP "Build the SDL3 / OpenGL application" ON)
option(STREFA_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(STREFA_EXPERIMENTAL_REFLECTION "Build an isolated C++26 reflection probe" OFF)
set(STREFA_DOWNLOAD_CACHE "" CACHE PATH "Optional directory with pinned dependency archives")

add_library(strefa_options INTERFACE)
target_compile_features(strefa_options INTERFACE cxx_std_23)
target_compile_options(strefa_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/utf-8;/Zc:__cplusplus>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>)
if(STREFA_WARNINGS_AS_ERRORS)
    target_compile_options(strefa_options INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/WX>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>)
endif()

add_library(strefa_core STATIC src/core/settings.cpp)
target_include_directories(strefa_core PUBLIC src)
target_link_libraries(strefa_core PUBLIC strefa_options)

add_library(strefa_storage STATIC src/platform/settings_store.cpp)
target_link_libraries(strefa_storage PUBLIC strefa_core)

find_package(Threads REQUIRED)
add_library(strefa_sim STATIC src/sim/world.cpp src/sim/thermal.cpp src/sim/liquids.cpp
    src/sim/air.cpp src/sim/reactions.cpp src/sim/motion.cpp src/sim/scenes.cpp src/sim/recording.cpp src/sim/laboratory.cpp src/sim/pressure.cpp)
target_include_directories(strefa_sim PUBLIC src)
target_link_libraries(strefa_sim PUBLIC strefa_options Threads::Threads)

add_library(strefa_game STATIC src/game/session.cpp)
target_link_libraries(strefa_game PUBLIC strefa_sim)
add_executable(strefa_physics src/tools/physics_runner.cpp)
target_link_libraries(strefa_physics PRIVATE strefa_game)
set_target_properties(strefa_physics PROPERTIES OUTPUT_NAME StrefaPhysics)
install(TARGETS strefa_physics RUNTIME DESTINATION .)

if(STREFA_EXPERIMENTAL_REFLECTION)
    include(cmake/Reflection.cmake)
endif()

if(STREFA_BUILD_APP)
    include(cmake/Dependencies.cmake)
    find_package(OpenGL REQUIRED)

    add_library(strefa_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(strefa_imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(strefa_imgui PUBLIC SDL3::SDL3 PRIVATE OpenGL::GL)
    target_compile_features(strefa_imgui PUBLIC cxx_std_17)

    add_library(strefa_platform STATIC src/platform/window.cpp src/platform/texture.cpp src/platform/gl_debug.cpp)
    target_link_libraries(strefa_platform PUBLIC strefa_core SDL3::SDL3 PRIVATE OpenGL::GL)
    add_library(strefa_gpu STATIC src/gpu/world.cpp src/gpu/validation.cpp src/gpu/liquid_validation.cpp src/gpu/flow_validation.cpp src/gpu/mission_validation.cpp src/gpu/profiling.cpp src/gpu/readback_ring.cpp src/gpu/readback_validation.cpp src/gpu/execution_validation.cpp src/gpu/neighbor_validation.cpp src/gpu/coupling_validation.cpp)
    target_link_libraries(strefa_gpu PUBLIC strefa_core PRIVATE SDL3::SDL3 OpenGL::GL)
    foreach(shader mission.comp mission_state.glsl mission.vert mission.frag coupling.comp body_terrain.glsl neighbors.comp particle_storage.glsl granular.comp granular.vert granular.frag particle_common.glsl liquids.comp liquid_splat.vert liquid_splat.frag liquid_surface.vert liquid_surface.frag)
        configure_file(assets/shaders/${shader} assets/shaders/${shader} COPYONLY)
    endforeach()

    configure_file(cmake/build_info.hpp.in generated/build_info.hpp @ONLY)
    add_library(strefa_ui STATIC src/ui/session.cpp src/ui/menu.cpp src/ui/laboratory.cpp src/ui/arena.cpp src/ui/gpu_laboratory.cpp)
    target_link_libraries(strefa_ui PUBLIC strefa_core strefa_game PRIVATE strefa_imgui strefa_platform)
    target_include_directories(strefa_ui PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

    add_executable(strefa_zero WIN32 src/main.cpp src/app/application.cpp)
    target_link_libraries(strefa_zero PRIVATE
        strefa_platform strefa_storage strefa_ui strefa_imgui strefa_gpu OpenGL::GL)
    target_include_directories(strefa_zero PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    set_target_properties(strefa_zero PROPERTIES OUTPUT_NAME StrefaZero)
    add_custom_command(TARGET strefa_zero POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:strefa_zero>
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:strefa_zero>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/licenses $<TARGET_FILE_DIR:strefa_zero>/licenses)
    install(TARGETS strefa_zero SDL3-shared RUNTIME DESTINATION . LIBRARY DESTINATION .)
    install(DIRECTORY assets licenses DESTINATION .)
    install(FILES docs/QUICKSTART.md DESTINATION . RENAME README.md)
endif()

if(BUILD_TESTING)
    add_executable(strefa_clock_tests tests/clock_tests.cpp)
    target_link_libraries(strefa_clock_tests PRIVATE strefa_game)
    add_test(NAME game.clock COMMAND strefa_clock_tests)
    add_executable(strefa_core_tests tests/core_tests.cpp)
    target_link_libraries(strefa_core_tests PRIVATE strefa_core strefa_storage)
    add_test(NAME core.settings COMMAND strefa_core_tests)
    add_executable(strefa_sim_tests tests/sim_tests.cpp)
    target_link_libraries(strefa_sim_tests PRIVATE strefa_sim)
    add_test(NAME sim.materials COMMAND strefa_sim_tests)
    add_executable(strefa_numerics_tests tests/numerics_tests.cpp)
    target_link_libraries(strefa_numerics_tests PRIVATE strefa_sim)
    add_test(NAME sim.numerics_parallel COMMAND strefa_numerics_tests)
    add_executable(strefa_game_tests tests/game_tests.cpp)
    target_link_libraries(strefa_game_tests PRIVATE strefa_game)
    add_test(NAME game.training COMMAND strefa_game_tests)
    add_executable(strefa_laboratory_tests tests/laboratory_tests.cpp)
    target_link_libraries(strefa_laboratory_tests PRIVATE strefa_sim)
    add_test(NAME sim.laboratory_worker COMMAND strefa_laboratory_tests)
    if(STREFA_BUILD_APP)
        add_test(NAME app.opengl_smoke COMMAND strefa_zero --smoke-test
            --profile ${CMAKE_CURRENT_BINARY_DIR}/smoke-profile)
        set_tests_properties(app.opengl_smoke PROPERTIES LABELS "integration;graphics" TIMEOUT 30)
        add_test(NAME app.laboratory_responsiveness COMMAND strefa_zero --lab-smoke-test
            --profile ${CMAKE_CURRENT_BINARY_DIR}/laboratory-smoke-profile)
        set_tests_properties(app.laboratory_responsiveness PROPERTIES LABELS "integration;graphics;performance" TIMEOUT 30)
        add_test(NAME app.laboratory_four_threads COMMAND strefa_zero --lab-smoke-test --physics-threads 4
            --profile ${CMAKE_CURRENT_BINARY_DIR}/laboratory-four-thread-profile)
        set_tests_properties(app.laboratory_four_threads PROPERTIES LABELS "integration;graphics;performance" TIMEOUT 30)
        add_test(NAME gpu.mission COMMAND strefa_zero --mission-test --profile ${CMAKE_CURRENT_BINARY_DIR}/mission-profile)
        add_test(NAME gpu.instrumentation COMMAND strefa_zero --gpu-profile-check --profile ${CMAKE_CURRENT_BINARY_DIR}/instrumentation-profile)
        add_test(NAME gpu.coupling COMMAND strefa_zero --gpu-coupling-check --profile ${CMAKE_CURRENT_BINARY_DIR}/coupling-profile)
        set_tests_properties(gpu.coupling PROPERTIES LABELS "integration;graphics;gpu" TIMEOUT 600 SKIP_RETURN_CODE 77)
        add_test(NAME gpu.neighbors COMMAND strefa_zero --gpu-neighbor-check --profile ${CMAKE_CURRENT_BINARY_DIR}/neighbor-profile)
        set_tests_properties(gpu.neighbors PROPERTIES LABELS "integration;graphics;gpu" TIMEOUT 120 SKIP_RETURN_CODE 77)
        add_test(NAME gpu.execution COMMAND strefa_zero --gpu-execution-check --profile ${CMAKE_CURRENT_BINARY_DIR}/execution-profile)
        set_tests_properties(gpu.execution PROPERTIES LABELS "integration;graphics;gpu" TIMEOUT 120 SKIP_RETURN_CODE 77)
        add_test(NAME gpu.readback_ring COMMAND strefa_zero --gpu-readback-check --profile ${CMAKE_CURRENT_BINARY_DIR}/readback-profile)
        set_tests_properties(gpu.readback_ring PROPERTIES LABELS "integration;graphics;gpu" TIMEOUT 60 SKIP_RETURN_CODE 77)
        set_tests_properties(gpu.instrumentation PROPERTIES LABELS "integration;graphics;gpu" TIMEOUT 60 SKIP_RETURN_CODE 77)
        set_tests_properties(gpu.mission PROPERTIES LABELS "integration;graphics;gpu" TIMEOUT 600 SKIP_RETURN_CODE 77)
        add_test(NAME gpu.mission_ui COMMAND strefa_zero --mission-smoke-test --profile ${CMAKE_CURRENT_BINARY_DIR}/mission-ui-profile)
        set_tests_properties(gpu.mission_ui PROPERTIES LABELS "integration;graphics;gpu" TIMEOUT 60 SKIP_RETURN_CODE 77)
        add_test(NAME gpu.granular_physics COMMAND strefa_zero --gpu-test --profile ${CMAKE_CURRENT_BINARY_DIR}/gpu-test-profile)
        add_test(NAME gpu.liquid_physics COMMAND strefa_zero --gpu-liquid-test --profile ${CMAKE_CURRENT_BINARY_DIR}/gpu-liquid-profile)
        add_test(NAME gpu.flow_behaviour COMMAND strefa_zero --gpu-flow-test --profile ${CMAKE_CURRENT_BINARY_DIR}/gpu-flow-profile)
        set_tests_properties(gpu.flow_behaviour PROPERTIES LABELS "integration;graphics;gpu" TIMEOUT 600 SKIP_RETURN_CODE 77)
        set_tests_properties(gpu.liquid_physics PROPERTIES LABELS "integration;graphics;gpu" TIMEOUT 240 SKIP_RETURN_CODE 77)
        set_tests_properties(gpu.granular_physics PROPERTIES LABELS "integration;graphics;gpu" TIMEOUT 180 SKIP_RETURN_CODE 77)
        add_test(NAME gpu.laboratory_ui COMMAND strefa_zero --gpu-smoke-test --profile ${CMAKE_CURRENT_BINARY_DIR}/gpu-ui-profile)
        set_tests_properties(gpu.laboratory_ui PROPERTIES LABELS "integration;graphics;gpu" TIMEOUT 45 SKIP_RETURN_CODE 77)
        add_test(NAME app.opengl33_smoke COMMAND strefa_zero --smoke-test --opengl33 --profile ${CMAKE_CURRENT_BINARY_DIR}/legacy-smoke-profile)
        set_tests_properties(app.opengl33_smoke PROPERTIES LABELS "integration;graphics" TIMEOUT 30)
    endif()
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT STREFA_IPO_SUPPORTED OUTPUT STREFA_IPO_MESSAGE)
if(STREFA_IPO_SUPPORTED)
    foreach(own_target strefa_sim strefa_game strefa_physics strefa_core strefa_storage
        strefa_platform strefa_gpu strefa_ui strefa_zero strefa_core_tests strefa_sim_tests strefa_game_tests strefa_laboratory_tests strefa_numerics_tests)
        if(TARGET ${own_target})
            set_property(TARGET ${own_target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
    endforeach()
endif()


