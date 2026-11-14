#!/bin/sh
#===============================================================================
# Classic OS — Lightweight VirtualBox Linux VM Provisioning Script (No WSL2)
# Target Host Guest OS: Debian 12 (Bookworm) 64-bit netinst / Ubuntu Server 22.04+
# Target Hardware: Sony PlayStation Classic (SCPH-1000R) — ARMv8-A 32-bit armhf
#===============================================================================
set -eu

echo "[1/4] Enabling 32-bit ARM hard-float (armhf) multiarch in dpkg..."
sudo dpkg --add-architecture armhf

echo "[2/4] Updating apt package lists..."
sudo apt-get update

echo "[3/4] Installing ARMv8-A 32-bit cross-compiler, CMake, and multiarch armhf dev libraries..."
sudo apt-get install -y --no-install-recommends \
    build-essential \
    make \
    cmake \
    pkg-config \
    file \
    zip \
    unzip \
    gcc-arm-linux-gnueabihf \
    g++-arm-linux-gnueabihf \
    binutils-arm-linux-gnueabihf \
    libc6-dev-armhf-cross \
    libsdl2-dev:armhf \
    libsdl2-image-dev:armhf \
    libsdl2-ttf-dev:armhf \
    libpng-dev:armhf \
    libsqlite3-dev:armhf

echo "[4/4] Verifying ARM cross-compiler installation..."
arm-linux-gnueabihf-g++ --version | head -n 1
echo "[OK] VirtualBox ARMhf cross-compilation environment is ready."
