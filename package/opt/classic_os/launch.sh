#!/bin/sh
#===============================================================================
# Classic OS — Primary Dashboard Launcher & Reversible Boot Menu Hook
# Location on USB: /media/project_eris/opt/classic_os/launch.sh
# Target: Sony PlayStation Classic (SCPH-1000R) running Project Eris
# Strictly POSIX /bin/sh compatible (no Bashisms)
#===============================================================================

if [ -f "/var/volatile/project_eris.cfg" ]; then
    . "/var/volatile/project_eris.cfg"
fi

PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
RUNTIME_LOG_PATH="${RUNTIME_LOG_PATH:-/media/logs}"
CLASSIC_OS_HOME="${PROJECT_ERIS_PATH}/opt/classic_os"
CLASSIC_OS_BOOTCTL="${CLASSIC_OS_HOME}/bootctl.sh"
CLASSIC_OS_CFG="${PROJECT_ERIS_PATH}/etc/project_eris/CFG/classic_os_boot.cfg"

# Delegate reversible boot & hook management flags to bootctl.sh
if [ "$1" = "--enable-primary-bootmenu" ] || [ "$1" = "--enable-primary" ]; then
    exec "/bin/sh" "${CLASSIC_OS_BOOTCTL}" --enable-primary
elif [ "$1" = "--restore-emulationstation" ] || [ "$1" = "--restore-bootmenu" ]; then
    exec "/bin/sh" "${CLASSIC_OS_BOOTCTL}" --restore-bootmenu
elif [ "$1" = "--status" ]; then
    exec "/bin/sh" "${CLASSIC_OS_BOOTCTL}" --status
fi

DEFAULT_RETURN_TARGET="launch_bootmenu"
if [ -f "${CLASSIC_OS_CFG}" ]; then
    . "${CLASSIC_OS_CFG}"
    if [ -n "${CLASSIC_OS_DEFAULT_EXIT_TARGET}" ]; then
        DEFAULT_RETURN_TARGET="${CLASSIC_OS_DEFAULT_EXIT_TARGET}"
    fi
fi

# Strip internal --primary-boot flag if passed by 0095_classic_os_boot.sh
if [ "$1" = "--primary-boot" ]; then
    shift
fi

mkdir -p "${RUNTIME_LOG_PATH}"

# Pre-seed default return target in /tmp/launchfilecommand so exiting Classic OS
# cleanly hands control back to the Project Eris Boot Menu (or Stock UI)
echo "${DEFAULT_RETURN_TARGET}" > "/tmp/launchfilecommand"

# Verify native ARM executable exists before attempting launch
if [ ! -x "${CLASSIC_OS_HOME}/classic_os" ]; then
    chmod +x "${CLASSIC_OS_HOME}/classic_os" 2>/dev/null || true
fi
if [ ! -x "${CLASSIC_OS_HOME}/classic_os" ]; then
    echo "[CLASSIC OS](ERROR) Binary not found or not executable at ${CLASSIC_OS_HOME}/classic_os. Returning to Project Eris Boot Menu." >> "${RUNTIME_LOG_PATH}/classic_os.log"
    echo "launch_bootmenu" > "/tmp/launchfilecommand"
    exit 1
fi

# Prevent console power cutoff while dashboard is active
[ -d "/data/power" ] && echo 2 > "/data/power/disable"

export HOME="${CLASSIC_OS_HOME}"
export LD_LIBRARY_PATH="/usr/sony/lib:${PROJECT_ERIS_PATH}/lib:${CLASSIC_OS_HOME}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

cd "${CLASSIC_OS_HOME}" || exit 1
SDL_VIDEODRIVER=wayland "${CLASSIC_OS_HOME}/classic_os" \
    --project-eris-path "${PROJECT_ERIS_PATH}" \
    --log-folder "${RUNTIME_LOG_PATH}" \
    --return-target "${DEFAULT_RETURN_TARGET}" \
    "$@" > "${RUNTIME_LOG_PATH}/classic_os.log" 2>&1

EXIT_CODE=$?
[ -d "/data/power" ] && echo 1 > "/data/power/disable"
exit "${EXIT_CODE}"
