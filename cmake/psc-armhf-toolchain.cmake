#===============================================================================
# Classic OS — CMake Cross-Compilation Toolchain File
# Target: Sony PlayStation Classic (SCPH-1000R) — ARMv8-A 32-bit hard-float
# Usage:
#   cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/psc-armhf-toolchain.cmake
#   cmake --build build-arm -j$(nproc)
#===============================================================================

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(TARGET_PSC_ARMHF ON CACHE BOOL "Cross-compile for Sony PlayStation Classic SCPH-1000R" FORCE)

if(NOT DEFINED CROSS_PREFIX OR "${CROSS_PREFIX}" STREQUAL "")
    set(CROSS_PREFIX "arm-linux-gnueabihf-")
endif()

find_program(PSC_TOOLCHAIN_CC  NAMES "${CROSS_PREFIX}gcc" arm-linux-gnueabihf-gcc)
find_program(PSC_TOOLCHAIN_CXX NAMES "${CROSS_PREFIX}g++" arm-linux-gnueabihf-g++)
find_program(PSC_TOOLCHAIN_AR  NAMES "${CROSS_PREFIX}ar"  arm-linux-gnueabihf-ar)
find_program(PSC_TOOLCHAIN_STRIP NAMES "${CROSS_PREFIX}strip" arm-linux-gnueabihf-strip)

if(NOT PSC_TOOLCHAIN_CXX)
    message(FATAL_ERROR
        "ARM cross-compiler '${CROSS_PREFIX}g++' not found in PATH. "
        "Cannot cross-compile Classic OS for PlayStation Classic without the armhf toolchain.")
endif()

set(CMAKE_C_COMPILER   "${PSC_TOOLCHAIN_CC}")
set(CMAKE_CXX_COMPILER "${PSC_TOOLCHAIN_CXX}")
if(PSC_TOOLCHAIN_AR)
    set(CMAKE_AR "${PSC_TOOLCHAIN_AR}" CACHE FILEPATH "ARM archiver")
endif()
if(PSC_TOOLCHAIN_STRIP)
    set(CMAKE_STRIP "${PSC_TOOLCHAIN_STRIP}" CACHE FILEPATH "ARM strip tool")
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
