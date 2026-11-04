include(FetchContent)

set(_sdl_hash 9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
set(_imgui_hash f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee)
set(_tracy_hash bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1)
set(_renderdoc_hash b7005e7dc34c3635046868bbd76d81b9b055aede0f56daa0bd39fedee0639ffb)

# Resolves an archive to STREFA_DOWNLOAD_CACHE/<file_name>. A missing archive is downloaded into the cache
# first (hash-checked), so CI only has to keep the directory between runs.
function(strefa_cached_archive url file_name sha256 out_url)
    if(NOT STREFA_DOWNLOAD_CACHE)
        set(${out_url} "${url}" PARENT_SCOPE)
        return()
    endif()
    set(cached "${STREFA_DOWNLOAD_CACHE}/${file_name}")
    if(NOT EXISTS "${cached}")
        file(DOWNLOAD "${url}" "${cached}.part" EXPECTED_HASH SHA256=${sha256} STATUS download_status)
        list(GET download_status 0 download_code)
        if(NOT download_code EQUAL 0)
            file(REMOVE "${cached}.part")
            message(FATAL_ERROR "Nie udało się pobrać ${url} do ${STREFA_DOWNLOAD_CACHE}: ${download_status}")
        endif()
        file(RENAME "${cached}.part" "${cached}")
    endif()
    set(${out_url} "${cached}" PARENT_SCOPE)
endfunction()

# Tracy client (docs/adr/0002-narzedzia-profilowania.md): only with STREFA_TRACY=ON, also for core-only builds.
# On demand and localhost only: with no profiler connected the zones cost one branch each.
if(STREFA_TRACY)
    strefa_cached_archive(https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
        tracy-0.14.1.tar.gz ${_tracy_hash} _tracy_url)
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
    set(TRACY_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(tracy URL "${_tracy_url}"
        URL_HASH SHA256=${_tracy_hash}
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(tracy)
endif()
if(NOT STREFA_BUILD_APP)
    return()
endif()

strefa_cached_archive(https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    SDL3-3.4.18.tar.gz ${_sdl_hash} _sdl_url)
strefa_cached_archive(https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.tar.gz
    imgui-1.93.0.tar.gz ${_imgui_hash} _imgui_url)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sdl3 URL "${_sdl_url}"
    URL_HASH SHA256=${_sdl_hash}
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_Declare(imgui URL "${_imgui_url}"
    URL_HASH SHA256=${_imgui_hash}
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(sdl3 imgui)

# RenderDoc in-application API header (MIT), used by StrefaQA only; the library itself is loaded at run time.
strefa_cached_archive(https://raw.githubusercontent.com/baldurk/renderdoc/v1.47/renderdoc/api/app/renderdoc_app.h
    renderdoc_app-1.47.h ${_renderdoc_hash} _renderdoc_url)
set(STREFA_RENDERDOC_INCLUDE_DIR ${CMAKE_BINARY_DIR}/_deps/renderdoc-1.47)
set(_renderdoc_header ${STREFA_RENDERDOC_INCLUDE_DIR}/renderdoc_app.h)
if(NOT EXISTS "${_renderdoc_header}")
    if(EXISTS "${_renderdoc_url}")
        configure_file("${_renderdoc_url}" "${_renderdoc_header}" COPYONLY)
    else()
        file(DOWNLOAD "${_renderdoc_url}" "${_renderdoc_header}" EXPECTED_HASH SHA256=${_renderdoc_hash}
            STATUS _renderdoc_status)
        list(GET _renderdoc_status 0 _renderdoc_code)
        if(NOT _renderdoc_code EQUAL 0)
            file(REMOVE "${_renderdoc_header}")
            message(FATAL_ERROR "Nie udało się pobrać ${_renderdoc_url}: ${_renderdoc_status}")
        endif()
    endif()
endif()
file(SHA256 "${_renderdoc_header}" _renderdoc_actual)
if(NOT _renderdoc_actual STREQUAL _renderdoc_hash)
    message(FATAL_ERROR "Niezgodny SHA-256 pliku ${_renderdoc_header}: ${_renderdoc_actual}")
endif()

configure_file(${sdl3_SOURCE_DIR}/LICENSE.txt ${CMAKE_CURRENT_BINARY_DIR}/SDL3-LICENSE.txt COPYONLY)
configure_file(${imgui_SOURCE_DIR}/LICENSE.txt ${CMAKE_CURRENT_BINARY_DIR}/DearImGui-LICENSE.txt COPYONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SDL3-LICENSE.txt
    ${CMAKE_CURRENT_BINARY_DIR}/DearImGui-LICENSE.txt DESTINATION licenses COMPONENT StrefaRuntime)
