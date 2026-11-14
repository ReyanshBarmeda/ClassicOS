#!/bin/sh
#===============================================================================
# Classic OS — Automated ARMv8-A 32-bit (armhf) Cross-Build & Packaging Script
# Runs inside the VirtualBox Linux VM (via Make or CMake) and verifies ELF ABI.
#===============================================================================
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_ROOT="$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_SYSTEM="${1:-make}" # "make" or "cmake"

cd "${PROJECT_ROOT}"

if ! command -v arm-linux-gnueabihf-g++ >/dev/null 2>&1; then
    echo "[ERROR] arm-linux-gnueabihf-g++ is not installed or not in PATH." >&2
    echo "        Run ./scripts/vbox_setup_armhf.sh first inside your VirtualBox Linux VM." >&2
    exit 1
fi

echo "=== Building Classic OS for PlayStation Classic (ARMv8-A 32-bit armhf) via ${BUILD_SYSTEM} ==="

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
    cp -rf assets/* package/opt/classic_os/assets/ 2>/dev/null || true
    chmod +x package/opt/classic_os/classic_os
    chmod +x package/opt/classic_os/launch.sh
    chmod +x package/opt/classic_os/bootctl.sh
    chmod +x package/etc/project_eris/FUNC/0095_classic_os_boot.sh
    chmod +x package/etc/project_eris/SUP/launchers/classic_os/launch.sh
else
    make clean
    make package TARGET_ARCH=armhf
fi

echo "=== Verifying ARM ELF Binary & ABI Attributes ==="
file package/opt/classic_os/classic_os
readelf -h -d -A package/opt/classic_os/classic_os | grep -E "Class:|Machine:|Flags:|NEEDED|Tag_CPU_name|Tag_CPU_arch|Tag_FP_arch" || true

echo "=== Validating POSIX Launcher & Boot Scripts ==="
sh -n package/opt/classic_os/launch.sh
sh -n package/opt/classic_os/bootctl.sh
sh -n package/etc/project_eris/FUNC/0095_classic_os_boot.sh
sh -n package/etc/project_eris/SUP/launchers/classic_os/launch.sh
echo "[OK] Package ready in: ${PROJECT_ROOT}/package/"
