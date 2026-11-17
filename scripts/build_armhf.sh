#!/bin/sh
#===============================================================================
# Classic OS — Automated ARMv8-A 32-bit (armhf) Cross-Build & Packaging Script
# Runs inside the VirtualBox Linux VM or the CI Debian 10 container (via Make or
# CMake) and then verifies the ELF ABI, glibc compatibility and package layout
# with scripts/verify_armhf.sh (the build fails if any check fails).
#===============================================================================
set -eu
unset CDPATH

SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_SYSTEM="${1:-make}" # "make" or "cmake"

cd "${PROJECT_ROOT}"

if ! command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1; then
    echo "[ERROR] arm-linux-gnueabihf-g++ is not installed or not in PATH." >&2
    echo "        Run ./scripts/vbox_setup_armhf.sh first inside your VirtualBox Linux VM." >&2
    exit 1
fi

echo "=== Building Classic OS for PlayStation Classic (ARMv8-A 32-bit armhf) via ${BUILD_SYSTEM} ==="
arm-linux-gnueabihf-g++ --version | head -n 1

if [ "${BUILD_SYSTEM}" = "cmake" ]; then
    rm -rf build-arm
    cmake -B build-arm \
        -DCMAKE_TOOLCHAIN_FILE=cmake/psc-armhf-toolchain.cmake \
        -DTARGET_PSC_ARMHF=ON \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build build-arm --parallel
    mkdir -p build
    cp -f build-arm/classic_os build/classic_os
    arm-linux-gnueabihf-strip build/classic_os
    mkdir -p package/opt/classic_os/assets
    cp -f build/classic_os package/opt/classic_os/classic_os
    cp -rf assets/* package/opt/classic_os/assets/
    chmod +x package/opt/classic_os/classic_os
    chmod +x package/opt/classic_os/launch.sh
    chmod +x package/opt/classic_os/bootctl.sh
    chmod +x package/opt/classic_os/check_runtime.sh
    chmod +x package/etc/project_eris/FUNC/0095_classic_os_boot.sh
    chmod +x package/etc/project_eris/SUP/launchers/classic_os/launch.sh
elif [ "${BUILD_SYSTEM}" = "make" ]; then
    make clean
    make package TARGET_ARCH=armhf
else
    echo "[ERROR] Unknown build system '${BUILD_SYSTEM}'. Use 'make' or 'cmake'." >&2
    exit 1
fi

file package/opt/classic_os/classic_os || true

# Verifies ARM ELF/ABI, DT_NEEDED, glibc symbol versions, package contents and
# POSIX validity of the launcher & boot scripts. Exits non-zero on any failure.
sh "${SCRIPT_DIR}/verify_armhf.sh" package/opt/classic_os/classic_os

echo "[OK] Package ready in: ${PROJECT_ROOT}/package/"
