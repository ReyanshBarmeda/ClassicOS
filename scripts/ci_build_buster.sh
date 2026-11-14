#!/bin/sh
#===============================================================================
# Classic OS — CI build inside a Debian 10 (Buster) container
#
# Why Buster: the PlayStation Classic baseline is glibc 2.28 (see
# BUILD_INSTRUCTIONS.md). Debian 10 ships glibc 2.28, so the sysroot, headers and
# static libstdc++ used for linking never reference newer GLIBC symbol versions.
# Newer distros (Ubuntu 22.04 = glibc 2.35) stamp GLIBC_2.34+ on dlopen,
# pthread_once and __libc_start_main, which the console cannot load.
#
# Usage (from the repository root, as root inside debian:buster):
#   docker run --rm -e HOST_UID=$(id -u) -e HOST_GID=$(id -g) \
#     -v "$PWD":/src -w /src debian:buster sh scripts/ci_build_buster.sh
#===============================================================================
set -eu

export DEBIAN_FRONTEND=noninteractive

echo "=== [1/4] Pointing apt at archive.debian.org (Debian 10 is end-of-life) ==="
cat > /etc/apt/sources.list <<'EOF'
deb http://archive.debian.org/debian buster main
deb http://archive.debian.org/debian-security buster/updates main
EOF
cat > /etc/apt/apt.conf.d/99classicos-archive <<'EOF'
Acquire::Check-Valid-Until "false";
Acquire::Retries "3";
EOF

echo "=== [2/4] Enabling armhf multiarch and updating package lists ==="
dpkg --add-architecture armhf
apt-get update

echo "=== [3/4] Installing ARM cross-toolchain and armhf libraries ==="
apt-get install -y --no-install-recommends \
    ca-certificates \
    make \
    file \
    binutils-arm-linux-gnueabihf \
    gcc-arm-linux-gnueabihf \
    g++-arm-linux-gnueabihf \
    libc6-dev-armhf-cross \
    libsdl2-dev:armhf \
    libpng-dev:armhf \
    libsqlite3-dev:armhf

echo "Build environment:"
echo "  glibc (build sysroot host): $(ldd --version | head -n 1)"
echo "  $(arm-linux-gnueabihf-g++ --version | head -n 1)"

echo "=== [4/4] Building, packaging and verifying ==="
status=0
sh scripts/build_armhf.sh make || status=$?

# Containers run as root; hand build outputs back to the CI user so later steps
# (and the checkout cleanup) can read and remove them.
if [ -n "${HOST_UID:-}" ] && [ -n "${HOST_GID:-}" ]; then
    for d in build package; do
        [ -d "${d}" ] && chown -R "${HOST_UID}:${HOST_GID}" "${d}"
    done
fi

exit "${status}"
