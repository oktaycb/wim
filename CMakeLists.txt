cmake_minimum_required(VERSION 3.21)

project(wim
    VERSION 0.1.0
    DESCRIPTION "wim - a Windows-native terminal workspace for coding agents"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(FATAL_ERROR "wim targets Windows and is built with MSVC (Visual Studio 2022 C++ workload)")
endif()

add_compile_options(/W4 /permissive- /Zc:preprocessor /MP)
add_compile_definitions(
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    WIM_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# wimcore - everything that has no UI and is unit-testable
# ---------------------------------------------------------------------------
add_library(wimcore STATIC)
target_include_directories(wimcore PUBLIC src)
file(GLOB_RECURSE WIMCORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
list(FILTER WIMCORE_SOURCES EXCLUDE REGEX ".*_win\.cpp$")
target_sources(wimcore PRIVATE ${WIMCORE_SOURCES})
target_link_libraries(wimcore PUBLIC)

# ---------------------------------------------------------------------------
# wim - the application executable (GUI subsystem)
# ---------------------------------------------------------------------------
add_executable(wim WIN32
    src/app/main.cpp)
target_link_libraries(wim PRIVATE wimcore)

# ---------------------------------------------------------------------------
# wimtests - unit tests for core
# ---------------------------------------------------------------------------
enable_testing()
add_executable(wimtests tests/main.cpp)
target_link_libraries(wimtests PRIVATE wimcore)
add_test(NAME wimtests COMMAND wimtests)
