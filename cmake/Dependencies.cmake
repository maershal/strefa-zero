include(FetchContent)

set(_sdl_hash 9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
set(_imgui_hash f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee)

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

configure_file(${sdl3_SOURCE_DIR}/LICENSE.txt ${CMAKE_CURRENT_BINARY_DIR}/SDL3-LICENSE.txt COPYONLY)
configure_file(${imgui_SOURCE_DIR}/LICENSE.txt ${CMAKE_CURRENT_BINARY_DIR}/DearImGui-LICENSE.txt COPYONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/SDL3-LICENSE.txt
    ${CMAKE_CURRENT_BINARY_DIR}/DearImGui-LICENSE.txt DESTINATION licenses COMPONENT StrefaRuntime)
