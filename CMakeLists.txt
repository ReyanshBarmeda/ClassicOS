cmake_minimum_required(VERSION 3.10)

#===============================================================================
# 1. Cross-Compiler Initialization MUST Occur BEFORE project()
#===============================================================================
set(TARGET_PSC_ARMHF ON CACHE BOOL
    "Cross-compile for Sony PlayStation Classic SCPH-1000R (ARMv8-A 32-bit hard-float)")
set(CROSS_PREFIX "arm-linux-gnueabihf-" CACHE STRING
    "GNU Toolchain prefix for PlayStation Classic 32-bit ARM hard-float builds")

if(TARGET_PSC_ARMHF)
    set(CMAKE_SYSTEM_NAME Linux)
    set(CMAKE_SYSTEM_PROCESSOR arm)

    # Strictly require the ARM cross-compiler; never silently fall back to host g++/c++
    if(NOT CMAKE_CXX_COMPILER)
        find_program(PSC_ARMHF_CXX NAMES "${CROSS_PREFIX}g++" arm-linux-gnueabihf-g++)
        if(NOT PSC_ARMHF_CXX)
            message(FATAL_ERROR
                "ARM cross-compiler '${CROSS_PREFIX}g++' not found in PATH. "
                "Refusing to fall back to host compiler. "
                "Install g++-arm-linux-gnueabihf inside your Linux VM, or pass "
                "-DTARGET_PSC_ARMHF=OFF for local x86_64 host desktop testing.")
        endif()
        set(CMAKE_CXX_COMPILER "${PSC_ARMHF_CXX}")
    endif()

    if(DEFINED PSC_SYSROOT AND NOT "${PSC_SYSROOT}" STREQUAL "")
        set(CMAKE_SYSROOT "${PSC_SYSROOT}")
        set(CMAKE_FIND_ROOT_PATH "${PSC_SYSROOT}")
    else()
        set(CMAKE_FIND_ROOT_PATH "/usr/arm-linux-gnueabihf" "/usr/lib/arm-linux-gnueabihf")
    endif()

    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
endif()

#===============================================================================
# 2. Project Declaration (Runs Compiler ABI Detection Using Above Settings)
#===============================================================================
project(classic_os VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Guard against stale CMakeCache.txt where host compiler was cached prior to TARGET_PSC_ARMHF=ON
if(TARGET_PSC_ARMHF)
    include(CheckCXXSymbolExists)
    check_cxx_symbol_exists(__arm__ "" CLASSIC_OS_COMPILER_IS_ARM32)
    if(NOT CLASSIC_OS_COMPILER_IS_ARM32)
        message(FATAL_ERROR
            "TARGET_PSC_ARMHF is ON, but active compiler '${CMAKE_CXX_COMPILER}' "
            "does not target 32-bit ARM (__arm__ is undefined). "
            "Delete your build directory (rm -rf build*) to clear stale CMakeCache.txt "
            "and re-run CMake with arm-linux-gnueabihf-g++ installed.")
    endif()

    # Match verified ELF attributes from Project Eris game_manager & psc_game_add:
    # Tag_CPU_name: "Cortex-A35", Tag_CPU_arch: v8, Tag_FP_arch: FP for ARMv8, NEON
    add_compile_options(
        -march=armv8-a+crc
        -mtune=cortex-a35
        -mfpu=neon-fp-armv8
        -mfloat-abi=hard
        -fPIE
        -O2
        -Wall
        -Wextra
        -ffunction-sections
        -fdata-sections
    )
    add_link_options(
        -pie
        -static-libstdc++
        -static-libgcc
        -Wl,--gc-sections
        -Wl,-rpath,/usr/sony/lib:/media/project_eris/lib
        -Wl,--allow-shlib-undefined
    )
else()
    message(STATUS "[Classic OS] Configuring HOST testing build (not for PlayStation Classic hardware)")
    add_compile_options(-O2 -Wall -Wextra -fPIE)
    add_link_options(-pie)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CLASSIC_OS_SOURCES
    src/main.cpp
    src/app.cpp
    src/config.cpp
    src/renderer.cpp
    src/texture_cache.cpp
    src/input.cpp
    src/library.cpp
    src/launcher.cpp
    src/ui_dashboard.cpp
)

add_executable(classic_os ${CLASSIC_OS_SOURCES})

if(DEFINED PSC_SYSROOT_LIB_DIR AND NOT "${PSC_SYSROOT_LIB_DIR}" STREQUAL "")
    target_link_directories(classic_os PRIVATE "${PSC_SYSROOT_LIB_DIR}")
    set(CLASSIC_OS_LIB_SEARCH_DIRS "${PSC_SYSROOT_LIB_DIR}")
elseif(TARGET_PSC_ARMHF)
    set(CLASSIC_OS_LIB_SEARCH_DIRS
        "/usr/lib/arm-linux-gnueabihf"
        "/usr/arm-linux-gnueabihf/lib")
else()
    set(CLASSIC_OS_LIB_SEARCH_DIRS
        "/usr/lib/x86_64-linux-gnu"
        "/usr/lib64"
        "/usr/lib")
endif()

# Resolve SDL2, libpng16, and sqlite3 (supports both -l<name> dev symlinks and direct SONAMEs)
find_library(SDL2_LIB NAMES SDL2 libSDL2-2.0.so.0 PATHS ${CLASSIC_OS_LIB_SEARCH_DIRS})
find_library(PNG16_LIB NAMES png16 png libpng16.so.16 PATHS ${CLASSIC_OS_LIB_SEARCH_DIRS})
find_library(SQLITE3_LIB NAMES sqlite3 libsqlite3.so.0 PATHS ${CLASSIC_OS_LIB_SEARCH_DIRS})

if(NOT SDL2_LIB)
    set(SDL2_LIB SDL2)
endif()
if(NOT PNG16_LIB)
    set(PNG16_LIB png16)
endif()
if(NOT SQLITE3_LIB)
    set(SQLITE3_LIB sqlite3)
endif()

target_link_libraries(classic_os PRIVATE
    ${SDL2_LIB}
    ${PNG16_LIB}
    ${SQLITE3_LIB}
    pthread
    dl
    m
)
