function(strefa_configure_reflection)
    include(CheckCXXSourceCompiles)
    if(MSVC)
        # Probe the feature itself even when CMake has no MSVC CXX26 dialect entry.
        unset(CMAKE_CXX_STANDARD)
        set(CMAKE_REQUIRED_FLAGS "/std:c++latest")
    else()
        set(CMAKE_CXX_STANDARD 26)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(CMAKE_REQUIRED_FLAGS "-freflection")
        endif()
    endif()
    check_cxx_source_compiles("
    #include <meta>
    #include <string_view>
    struct SettingsProbe { bool vsync; };
    static_assert(std::meta::identifier_of(^^SettingsProbe) == std::string_view{\"SettingsProbe\"});
    int main() {}
    " STREFA_HAS_CPP26_REFLECTION)
    if(NOT STREFA_HAS_CPP26_REFLECTION)
        message(FATAL_ERROR "C++26 reflection is unavailable in this toolchain. Build the application with STREFA_EXPERIMENTAL_REFLECTION=OFF. The portable C++23 schema remains fully functional.")
    endif()
    add_executable(strefa_reflection_probe experiments/reflection_probe.cpp)
    if(MSVC)
        unset(CMAKE_CXX_STANDARD)
        target_compile_options(strefa_reflection_probe PRIVATE /std:c++latest)
    else()
        set_target_properties(strefa_reflection_probe PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED ON)
        target_compile_options(strefa_reflection_probe PRIVATE $<$<CXX_COMPILER_ID:GNU>:-freflection>)
    endif()
    if(BUILD_TESTING)
        add_test(NAME experiment.reflection COMMAND strefa_reflection_probe)
    endif()
endfunction()
strefa_configure_reflection()
