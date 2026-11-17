#!/bin/sh
#===============================================================================
# Classic OS — Launcher: runs ONE Classic OS session and reports how it ended
# Location on USB: /media/project_eris/opt/classic_os/launch.sh
# Target: Sony PlayStation Classic (SCPH-1000R) running Project Eris
# Strictly POSIX /bin/sh compatible (BusyBox ash safe, no Bashisms)
#
# Used by every entry point (0095 supervisor, Boot Menu item, Stock UI launcher,
# optional EmulationStation wrapper), so process handling lives in exactly one place:
#   - exactly ONE --return-target is passed to the binary (CLI > config > default)
#   - pre-flight runtime check (missing library => never exec, stay in Project Eris)
#   - display environment discovery (no hard-coded video driver)
#   - hang watchdog: the app must present a first frame within READY_TIMEOUT seconds
#   - exit-code handling: any failure forces "launch_bootmenu" so Project Eris regains control
#
# Exit status: 0 = clean exit, non-zero = failure (the Project Eris menu token is already set)
#
# Test hooks (never needed on the console): CLASSIC_OS_ERIS_CFG, CLASSIC_OS_TMP
#===============================================================================

ERIS_CFG="${CLASSIC_OS_ERIS_CFG:-/var/volatile/project_eris.cfg}"
if [ -f "${ERIS_CFG}" ]; then
    . "${ERIS_CFG}"
fi

COS_TMP="${CLASSIC_OS_TMP:-/tmp}"
PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
RUNTIME_LOG_PATH="${RUNTIME_LOG_PATH:-/media/logs}"
CLASSIC_OS_HOME="${PROJECT_ERIS_PATH}/opt/classic_os"
CLASSIC_OS_BIN="${CLASSIC_OS_HOME}/classic_os"
CLASSIC_OS_BOOTCTL="${CLASSIC_OS_HOME}/bootctl.sh"
CLASSIC_OS_CFG="${PROJECT_ERIS_PATH}/etc/project_eris/CFG/classic_os_boot.cfg"
LAUNCH_TOKEN_FILE="${COS_TMP}/launchfilecommand"
READY_FILE="${COS_TMP}/classic_os_ready"
BOOT_LOG="${RUNTIME_LOG_PATH}/classic_os_boot.log"

log() {
    mkdir -p "${RUNTIME_LOG_PATH}" 2>/dev/null
    echo "[CLASSIC OS](LAUNCH) $*" >> "${BOOT_LOG}" 2>/dev/null || true
}

# Delegate reversible boot & hook management flags to bootctl.sh
case "$1" in
    --enable-primary-bootmenu|--enable-primary) exec /bin/sh "${CLASSIC_OS_BOOTCTL}" --enable-primary ;;
    --restore-emulationstation|--restore-bootmenu) exec /bin/sh "${CLASSIC_OS_BOOTCTL}" --restore-bootmenu ;;
    --status) exec /bin/sh "${CLASSIC_OS_BOOTCTL}" --status ;;
    --check) exec /bin/sh "${CLASSIC_OS_HOME}/check_runtime.sh" ;;
esac

#--- 1. Defaults from classic_os_boot.cfg --------------------------------------------
CLASSIC_OS_DEFAULT_EXIT_TARGET="launch_bootmenu"
CLASSIC_OS_READY_TIMEOUT_SEC="30"
if [ -f "${CLASSIC_OS_CFG}" ]; then
    . "${CLASSIC_OS_CFG}"
fi
RETURN_TARGET="${CLASSIC_OS_DEFAULT_EXIT_TARGET}"

#--- 2. Argument parsing: ONE --return-target, --primary-boot is internal -------------
# The remaining arguments are forwarded to the binary unchanged. A caller-supplied
# --return-target replaces the configured default instead of being added to it.
n=$#
i=0
while [ "${i}" -lt "${n}" ]; do
    arg="$1"
    shift
    case "${arg}" in
        --primary-boot)
            ;;
        --return-target)
            if [ "${i}" -lt $((n - 1)) ]; then
                RETURN_TARGET="$1"
                shift
                i=$((i + 1))
            fi
            ;;
        --return-target=*)
            RETURN_TARGET="${arg#--return-target=}"
            ;;
        *)
            set -- "$@" "${arg}"
            ;;
    esac
    i=$((i + 1))
done

case "${RETURN_TARGET}" in
    launch_bootmenu|launch_stockui|launch_retroarch_standalone) ;;
    *)
        log "Ignoring unsupported return target '${RETURN_TARGET}', using launch_bootmenu"
        RETURN_TARGET="launch_bootmenu"
        ;;
esac

mkdir -p "${RUNTIME_LOG_PATH}" 2>/dev/null

# Failure path used everywhere below: Project Eris must always get control back.
fail_to_bootmenu() {
    log "ERROR: $1 — returning to Project Eris Boot Menu"
    echo "launch_bootmenu" > "${LAUNCH_TOKEN_FILE}"
    [ -d "/data/power" ] && echo 1 > "/data/power/disable"
    exit "${2:-1}"
}

# Pre-seed the post-exit supervisor token (what Project Eris reads when we return)
echo "${RETURN_TARGET}" > "${LAUNCH_TOKEN_FILE}"

#--- 3. Pre-flight --------------------------------------------------------------------
[ -f "${CLASSIC_OS_BIN}" ] || fail_to_bootmenu "binary not found: ${CLASSIC_OS_BIN}" 126
if [ ! -x "${CLASSIC_OS_BIN}" ]; then
    chmod +x "${CLASSIC_OS_BIN}" 2>/dev/null || true
fi

# RUNNER prefixes the binary. Normally empty. If the mount hides the exec bit and chmod cannot
# fix it, run the binary through the ARM dynamic loader instead (works whenever the mount allows
# exec mappings). CLASSIC_OS_RUN_PREFIX is a test hook (e.g. "qemu-arm -L <sysroot>").
RUNNER="${CLASSIC_OS_RUN_PREFIX}"
if [ -z "${RUNNER}" ] && [ ! -x "${CLASSIC_OS_BIN}" ]; then
    LOADER="${CLASSIC_OS_LOADER:-/lib/ld-linux-armhf.so.3}"
    [ -e "${LOADER}" ] || fail_to_bootmenu "binary is not executable and no ELF loader at ${LOADER}" 126
    RUNNER="${LOADER} --library-path /usr/sony/lib:${PROJECT_ERIS_PATH}/lib:${CLASSIC_OS_HOME}/lib"
    log "binary lacks the exec bit; running through ${LOADER}"
fi

if [ "${CLASSIC_OS_SKIP_PREFLIGHT}" = "1" ]; then
    log "WARNING: pre-flight runtime check skipped (CLASSIC_OS_SKIP_PREFLIGHT=1, test use only)"
elif [ -f "${CLASSIC_OS_HOME}/check_runtime.sh" ]; then
    if ! PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH}" RUNTIME_LOG_PATH="${RUNTIME_LOG_PATH}" \
         /bin/sh "${CLASSIC_OS_HOME}/check_runtime.sh" --quiet; then
        fail_to_bootmenu "runtime check failed (see ${RUNTIME_LOG_PATH}/classic_os_runtime_check.log)" 127
    fi
fi

#--- 4. Environment ---------------------------------------------------------------------
export HOME="${CLASSIC_OS_HOME}"
export LD_LIBRARY_PATH="/usr/sony/lib:${PROJECT_ERIS_PATH}/lib:${CLASSIC_OS_HOME}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

# Display: nothing is forced here. The binary probes wayland -> kmsdrm -> SDL default and
# logs every attempt to classic_os_diagnostics.log. If a Wayland compositor is running but
# its environment was not inherited, locate its socket so SDL's wayland driver can connect.
if [ -z "${XDG_RUNTIME_DIR}" ] || [ ! -d "${XDG_RUNTIME_DIR}" ]; then
    for d in /var/run/user/* /run/user/* /var/volatile /var/run /run /tmp; do
        for s in "${d}"/wayland-*; do
            if [ -S "${s}" ]; then
                export XDG_RUNTIME_DIR="${d}"
                export WAYLAND_DISPLAY="${s##*/}"
                break 2
            fi
        done
    done
fi
log "display env: XDG_RUNTIME_DIR='${XDG_RUNTIME_DIR}' WAYLAND_DISPLAY='${WAYLAND_DISPLAY}' SDL_VIDEODRIVER='${SDL_VIDEODRIVER}'"

#--- 5. Run, with hang watchdog -----------------------------------------------------------
# Keep one previous log so a failed run is still inspectable after the next attempt.
[ -f "${RUNTIME_LOG_PATH}/classic_os.log" ] && mv -f "${RUNTIME_LOG_PATH}/classic_os.log" "${RUNTIME_LOG_PATH}/classic_os.log.prev" 2>/dev/null

# Prevent console power cutoff while the dashboard is active
[ -d "/data/power" ] && echo 2 > "/data/power/disable"

rm -f "${READY_FILE}"
cd "${CLASSIC_OS_HOME}" || fail_to_bootmenu "cannot cd to ${CLASSIC_OS_HOME}" 1

log "starting ${CLASSIC_OS_BIN} (return target: ${RETURN_TARGET})"
${RUNNER} "${CLASSIC_OS_BIN}" \
    --project-eris-path "${PROJECT_ERIS_PATH}" \
    --log-folder "${RUNTIME_LOG_PATH}" \
    --return-target "${RETURN_TARGET}" \
    --ready-file "${READY_FILE}" \
    "$@" > "${RUNTIME_LOG_PATH}/classic_os.log" 2>&1 &
APP_PID=$!

trap 'kill -TERM "${APP_PID}" 2>/dev/null' TERM INT

WATCHDOG_KILLED=0
waited=0
while kill -0 "${APP_PID}" 2>/dev/null && [ ! -f "${READY_FILE}" ]; do
    if [ "${waited}" -ge "${CLASSIC_OS_READY_TIMEOUT_SEC}" ]; then
        log "watchdog: no first frame after ${waited}s — stopping pid ${APP_PID}"
        kill -TERM "${APP_PID}" 2>/dev/null
        sleep 2
        kill -KILL "${APP_PID}" 2>/dev/null
        WATCHDOG_KILLED=1
        break
    fi
    sleep 1
    waited=$((waited + 1))
done

if [ -f "${READY_FILE}" ]; then
    # Display and input came up: this boot attempt succeeded (the supervisor's crash-loop
    # marker is cleared so a later power-cut is not mistaken for a failed boot).
    [ -n "${CLASSIC_OS_ATTEMPT_MARKER}" ] && rm -f "${CLASSIC_OS_ATTEMPT_MARKER}"
    log "first frame presented after ${waited}s"
fi

wait "${APP_PID}"
EXIT_CODE=$?
trap - TERM INT

[ -d "/data/power" ] && echo 1 > "/data/power/disable"

#--- 6. Exit-code handling --------------------------------------------------------------------
if [ "${WATCHDOG_KILLED}" = "1" ]; then
    EXIT_CODE=124
fi
if [ "${EXIT_CODE}" -ne 0 ]; then
    case "${EXIT_CODE}" in
        124) why="hung before first frame (watchdog)" ;;
        126|127) why="could not be executed (loader/library problem)" ;;
        139) why="segmentation fault" ;;
        134) why="aborted" ;;
        *)   why="exit code ${EXIT_CODE}" ;;
    esac
    fail_to_bootmenu "classic_os ${why}" "${EXIT_CODE}"
fi

log "classic_os exited cleanly; supervisor token: $(cat "${LAUNCH_TOKEN_FILE}" 2>/dev/null)"
exit 0
