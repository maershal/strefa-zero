include(FetchContent)

set(_sdl_url https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz)
set(_imgui_url https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.tar.gz)
if(EXISTS "${STREFA_DOWNLOAD_CACHE}/SDL3-3.4.18.tar.gz")
    set(_sdl_url "${STREFA_DOWNLOAD_CACHE}/SDL3-3.4.18.tar.gz")
endif()
if(EXISTS "${STREFA_DOWNLOAD_CACHE}/imgui-1.93.0.tar.gz")
    set(_imgui_url "${STREFA_DOWNLOAD_CACHE}/imgui-1.93.0.tar.gz")
endif()

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sdl3 URL "${_sdl_url}"
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_Declare(imgui URL "${_imgui_url}"
    URL_HASH SHA256=f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(sdl3 imgui)

configure_file(${sdl3_SOURCE_DIR}/LICENSE.txt ${CMAKE_CURRENT_BINARY_DIR}/SDL3-LICENSE.txt COPYONLY)
configure_file(${imgui_SOURCE_DIR}/LICENSE.txt ${CMAKE_CURRENT_BINARY_DIR}/DearImGui-LICENSE.txt COPYONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SDL3-LICENSE.txt
    ${CMAKE_CURRENT_BINARY_DIR}/DearImGui-LICENSE.txt DESTINATION licenses COMPONENT StrefaRuntime)
