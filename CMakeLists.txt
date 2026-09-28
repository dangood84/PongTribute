cmake_minimum_required(VERSION 3.16)

# Catalina is the oldest Mac this project aims to run on. Setting the
# deployment target before project() keeps it in force if CMake builds Raylib.
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()

project(PongTribute VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PONG_RAYLIB_TAG "5.5" CACHE STRING "Raylib git tag to download when none is installed")
option(PONG_USE_SYSTEM_RAYLIB "Use an installed Raylib if CMake can find it" ON)

set(PONG_RAYLIB_TARGET "")

if(PONG_USE_SYSTEM_RAYLIB)
    find_package(raylib QUIET CONFIG)
    if(TARGET raylib)
        set(PONG_RAYLIB_TARGET raylib)
    elseif(TARGET raylib::raylib)
        set(PONG_RAYLIB_TARGET raylib::raylib)
    endif()
endif()

if(PONG_RAYLIB_TARGET STREQUAL "")
    find_package(Git QUIET)
    if(NOT Git_FOUND)
        message(FATAL_ERROR
            "Raylib is not installed, and Git is not available to download it.\n"
            "Install Raylib (macOS: brew install raylib) or install Git and configure again.")
    endif()

    message(STATUS "System Raylib was not found. Downloading Raylib ${PONG_RAYLIB_TAG}.")
    include(FetchContent)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG ${PONG_RAYLIB_TAG}
        GIT_SHALLOW TRUE
    )
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)
    set(PONG_RAYLIB_TARGET raylib)
endif()

# Drop the binary in build/ (or build/pong.exe), the same place the other projects use.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${CMAKE_BINARY_DIR}")
endforeach()

add_executable(pong src/main.c)
target_link_libraries(pong PRIVATE ${PONG_RAYLIB_TARGET})

# sinf/cosf live in libm on Linux, including Raspberry Pi OS.
if(UNIX AND NOT APPLE)
    target_link_libraries(pong PRIVATE m)
endif()

if(MSVC)
    target_compile_options(pong PRIVATE /W4)
else()
    target_compile_options(pong PRIVATE -Wall -Wextra -Wpedantic)
endif()
