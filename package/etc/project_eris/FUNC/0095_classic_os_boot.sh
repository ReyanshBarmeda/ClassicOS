#!/bin/sh
#===============================================================================
# Classic OS — Project Eris Supervisor & Auto-Boot Extension
# Location on USB: /media/project_eris/etc/project_eris/FUNC/0095_classic_os_boot.sh
# Sourced automatically by Project Eris after 0030_project_eris_funcs.sh
# Strictly POSIX /bin/sh compatible (BusyBox ash safe, no Bashisms)
#
# Safety contract — the original Project Eris menu must always stay reachable:
#   * 0030_project_eris_funcs.sh is never modified; we only wrap execute_project_eris_func
#     in RAM, and ONLY if the original function can be extracted and parsed. If it cannot,
#     nothing is wrapped and Project Eris runs exactly as if this file were absent.
#   * Any failure of Classic OS (non-zero exit, exit before CLASSIC_OS_MIN_UPTIME_SEC, hang
#     before the first frame, binary/library missing) hands control to "launch_bootmenu".
#   * A boot attempt that never reaches its first frame (hang / power cut) is counted on the
#     USB drive; after CLASSIC_OS_MAX_CRASHES in a row auto-boot is switched off (bootctl).
#   * USB recovery flags always win: /media/CLASSIC_OS_RECOVERY, /media/ForceBootMenu.flag,
#     <project_eris>/etc/project_eris/CFG/disable_classic_os
#
# Test hooks (never needed on the console): CLASSIC_OS_TMP
#===============================================================================

COS_TMP="${CLASSIC_OS_TMP:-/tmp}"

classic_os_log() {
    COS_LOG_DIR="${RUNTIME_LOG_PATH:-/media/logs}"
    mkdir -p "${COS_LOG_DIR}" 2>/dev/null || true
    echo "[CLASSIC OS](SUPERVISOR) $*" >> "${COS_LOG_DIR}/classic_os_boot.log" 2>/dev/null || true
}

# Persistent (USB) crash-loop state. /tmp is RAM and wiped every power cycle, so it cannot
# detect a boot that hangs or is power-cycled.
classic_os_state_init() {
    PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
    MOUNTPOINT="${MOUNTPOINT:-/media}"
    COS_STATE_DIR="${PROJECT_ERIS_PATH}/etc/project_eris/CFG"
    COS_ATTEMPT_MARKER="${COS_STATE_DIR}/.classic_os_boot_attempt"
    COS_CRASH_FILE="${COS_STATE_DIR}/.classic_os_crash_count"
}

classic_os_crash_count() {
    _n=$(cat "${COS_CRASH_FILE}" 2>/dev/null)
    case "${_n}" in
        ''|*[!0-9]*) _n=0 ;;
    esac
    echo "${_n}"
}

classic_os_crash_reset() {
    rm -f "${COS_ATTEMPT_MARKER}" "${COS_CRASH_FILE}" 2>/dev/null || true
}

# Records one failed attempt; trips the breaker when the limit is reached.
classic_os_crash_record() {
    _n=$(classic_os_crash_count)
    _n=$((_n + 1))
    echo "${_n}" > "${COS_CRASH_FILE}" 2>/dev/null || true
    rm -f "${COS_ATTEMPT_MARKER}" 2>/dev/null || true
    if [ "${_n}" -ge "${CLASSIC_OS_MAX_CRASHES}" ]; then
        classic_os_log "Crash-loop guard TRIPPED (${_n}/${CLASSIC_OS_MAX_CRASHES} consecutive failed boots). Disabling Classic OS auto-boot; Project Eris Boot Menu is now the default. Re-enable with: sh ${CLASSIC_OS_BOOTCTL} --enable-primary"
        if [ -f "${CLASSIC_OS_BOOTCTL}" ]; then
            /bin/sh "${CLASSIC_OS_BOOTCTL}" --restore-bootmenu >/dev/null 2>&1 || true
        fi
        echo "launch_bootmenu" > "${COS_TMP}/launchfilecommand"
        return 1
    fi
    classic_os_log "Failed boot attempt recorded (${_n}/${CLASSIC_OS_MAX_CRASHES})."
    return 0
}

classic_os_check_recovery_and_config() {
    PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
    MOUNTPOINT="${MOUNTPOINT:-/media}"
    CLASSIC_OS_HOME="${PROJECT_ERIS_PATH}/opt/classic_os"
    CLASSIC_OS_BIN="${CLASSIC_OS_HOME}/classic_os"
    CLASSIC_OS_LAUNCH="${CLASSIC_OS_HOME}/launch.sh"
    CLASSIC_OS_BOOTCTL="${CLASSIC_OS_HOME}/bootctl.sh"
    CLASSIC_OS_CFG="${PROJECT_ERIS_PATH}/etc/project_eris/CFG/classic_os_boot.cfg"
    classic_os_state_init

    CLASSIC_OS_PRIMARY_DASHBOARD="1"
    CLASSIC_OS_HOOK_EMULATIONSTATION="0"
    CLASSIC_OS_DEFAULT_EXIT_TARGET="launch_bootmenu"
    CLASSIC_OS_MIN_UPTIME_SEC="2"
    CLASSIC_OS_MAX_CRASHES="3"

    if [ -f "${CLASSIC_OS_CFG}" ]; then
        . "${CLASSIC_OS_CFG}"
    fi
    case "${CLASSIC_OS_MIN_UPTIME_SEC}" in ''|*[!0-9]*) CLASSIC_OS_MIN_UPTIME_SEC="2" ;; esac
    case "${CLASSIC_OS_MAX_CRASHES}" in ''|*[!0-9]*|0) CLASSIC_OS_MAX_CRASHES="3" ;; esac

    # 1. USB recovery flag check (offline recovery: put a flag file on the USB drive)
    if [ -f "${MOUNTPOINT}/CLASSIC_OS_RECOVERY" ] || \
       [ -f "${MOUNTPOINT}/ForceBootMenu.flag" ] || \
       [ -f "${PROJECT_ERIS_PATH}/etc/project_eris/CFG/disable_classic_os" ]; then
        classic_os_log "Recovery flag detected on USB. Disabling Classic OS auto-boot and restoring Project Eris Boot Menu."
        CLASSIC_OS_PRIMARY_DASHBOARD="0"
        if [ -f "${CLASSIC_OS_BOOTCTL}" ]; then
            /bin/sh "${CLASSIC_OS_BOOTCTL}" --restore-bootmenu >/dev/null 2>&1 || true
        fi
        rm -f "${MOUNTPOINT}/CLASSIC_OS_RECOVERY" \
              "${MOUNTPOINT}/ForceBootMenu.flag" \
              "${PROJECT_ERIS_PATH}/etc/project_eris/CFG/disable_classic_os" 2>/dev/null || true
        classic_os_crash_reset
        echo "launch_bootmenu" > "${COS_TMP}/launchfilecommand"
        return 1
    fi

    # 2. Keep the (opt-in) EmulationStation wrapper in sync with the configuration
    if [ -f "${CLASSIC_OS_BOOTCTL}" ]; then
        if [ "${CLASSIC_OS_HOOK_EMULATIONSTATION}" = "1" ]; then
            /bin/sh "${CLASSIC_OS_BOOTCTL}" --sync-es-hook-on >/dev/null 2>&1 || true
        else
            /bin/sh "${CLASSIC_OS_BOOTCTL}" --sync-es-hook-off >/dev/null 2>&1 || true
        fi
    fi

    # 3. Is Classic OS configured as the primary dashboard?
    if [ "${CLASSIC_OS_PRIMARY_DASHBOARD}" != "1" ]; then
        classic_os_log "Classic OS primary auto-boot is disabled (CLASSIC_OS_PRIMARY_DASHBOARD=${CLASSIC_OS_PRIMARY_DASHBOARD}). Proceeding to standard Project Eris Boot Menu."
        return 1
    fi

    # 4. Native executable and launcher must exist
    if [ ! -f "${CLASSIC_OS_BIN}" ] || [ ! -f "${CLASSIC_OS_LAUNCH}" ]; then
        classic_os_log "Classic OS executable (${CLASSIC_OS_BIN}) or launcher missing. Falling back to Project Eris Boot Menu."
        echo "launch_bootmenu" > "${COS_TMP}/launchfilecommand"
        return 1
    fi
    [ -x "${CLASSIC_OS_BIN}" ] || chmod +x "${CLASSIC_OS_BIN}" 2>/dev/null || true

    # 5. Crash-loop guard: a marker left over from the previous boot means that attempt never
    #    presented a first frame (hang, power cut, supervisor killed).
    if [ -f "${COS_ATTEMPT_MARKER}" ]; then
        classic_os_log "Previous Classic OS boot attempt never reached its first frame."
        if ! classic_os_crash_record; then
            return 1
        fi
    fi

    return 0
}

classic_os_run_primary_session() {
    PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
    CLASSIC_OS_HOME="${PROJECT_ERIS_PATH}/opt/classic_os"
    CLASSIC_OS_LAUNCH="${CLASSIC_OS_HOME}/launch.sh"
    CLASSIC_OS_BOOTCTL="${CLASSIC_OS_HOME}/bootctl.sh"
    CLASSIC_OS_CFG="${PROJECT_ERIS_PATH}/etc/project_eris/CFG/classic_os_boot.cfg"
    classic_os_state_init

    # Re-read the (possibly changed) config when entered via the "launch_classic_os" token
    CLASSIC_OS_DEFAULT_EXIT_TARGET="${CLASSIC_OS_DEFAULT_EXIT_TARGET:-launch_bootmenu}"
    CLASSIC_OS_MIN_UPTIME_SEC="${CLASSIC_OS_MIN_UPTIME_SEC:-2}"
    CLASSIC_OS_MAX_CRASHES="${CLASSIC_OS_MAX_CRASHES:-3}"
    [ -f "${CLASSIC_OS_CFG}" ] && . "${CLASSIC_OS_CFG}"
    case "${CLASSIC_OS_MIN_UPTIME_SEC}" in ''|*[!0-9]*) CLASSIC_OS_MIN_UPTIME_SEC="2" ;; esac
    case "${CLASSIC_OS_MAX_CRASHES}" in ''|*[!0-9]*|0) CLASSIC_OS_MAX_CRASHES="3" ;; esac
    DEFAULT_EXIT="${CLASSIC_OS_DEFAULT_EXIT_TARGET}"

    if [ ! -f "${CLASSIC_OS_LAUNCH}" ]; then
        classic_os_log "Launcher ${CLASSIC_OS_LAUNCH} missing. Recovering to Project Eris Boot Menu."
        echo "launch_bootmenu" > "${COS_TMP}/launchfilecommand"
        return 1
    fi

    # Mark the attempt; launch.sh removes the marker as soon as the first frame is on screen.
    echo "attempt" > "${COS_ATTEMPT_MARKER}" 2>/dev/null || true
    export CLASSIC_OS_ATTEMPT_MARKER="${COS_ATTEMPT_MARKER}"

    START_TS=$(date +%s 2>/dev/null)
    classic_os_log "Starting Classic OS (default exit target: ${DEFAULT_EXIT}, min uptime ${CLASSIC_OS_MIN_UPTIME_SEC}s, crash limit ${CLASSIC_OS_MAX_CRASHES})."

    # launch.sh passes exactly one --return-target to the binary (this value)
    /bin/sh "${CLASSIC_OS_LAUNCH}" --primary-boot --return-target "${DEFAULT_EXIT}"
    COS_RC=$?
    unset CLASSIC_OS_ATTEMPT_MARKER

    END_TS=$(date +%s 2>/dev/null)
    ELAPSED=-1
    case "${START_TS}${END_TS}" in
        ''|*[!0-9]*) classic_os_log "WARN: clock unavailable, minimum-uptime guard skipped." ;;
        *) ELAPSED=$((END_TS - START_TS)) ;;
    esac

    # Failure = non-zero exit OR exited sooner than CLASSIC_OS_MIN_UPTIME_SEC (crash-on-start loop).
    FAILED=0
    if [ "${COS_RC}" -ne 0 ]; then
        FAILED=1
        classic_os_log "Classic OS exited with non-zero code ${COS_RC} after ${ELAPSED}s."
    elif [ "${ELAPSED}" -ge 0 ] && [ "${ELAPSED}" -lt "${CLASSIC_OS_MIN_UPTIME_SEC}" ]; then
        FAILED=1
        classic_os_log "Classic OS exited after only ${ELAPSED}s (< CLASSIC_OS_MIN_UPTIME_SEC=${CLASSIC_OS_MIN_UPTIME_SEC}); treating as a crash."
    fi

    if [ "${FAILED}" -eq 1 ]; then
        echo "launch_bootmenu" > "${COS_TMP}/launchfilecommand"
        classic_os_crash_record
        classic_os_log "Recovering to Project Eris Boot Menu."
        return 1
    fi

    classic_os_crash_reset
    NEXT_TARGET=$(cat "${COS_TMP}/launchfilecommand" 2>/dev/null)
    if [ -z "${NEXT_TARGET}" ]; then
        NEXT_TARGET="${DEFAULT_EXIT}"
        echo "${NEXT_TARGET}" > "${COS_TMP}/launchfilecommand"
    fi
    classic_os_log "Classic OS session ended cleanly (rc=${COS_RC}, uptime=${ELAPSED}s). Handing off to supervisor target: ${NEXT_TARGET}"
    return 0
}

#===============================================================================
# Supervisor wrapper: hooks execute_project_eris_func() in RAM without modifying
# 0030_project_eris_funcs.sh on disk. Only that one function is extracted and
# renamed (the rest of 0030 is NOT re-sourced, so no startup code runs twice).
#===============================================================================
COS_PE_ROOT="${PROJECT_ERIS_PATH:-/media/project_eris}"
COS_PE_FUNCS="${COS_PE_ROOT}/etc/project_eris/FUNC/0030_project_eris_funcs.sh"
COS_ORIG_FN="${COS_TMP}/classic_os_orig_execute.sh"
COS_HOOKED=0
rm -f "${COS_ORIG_FN}" 2>/dev/null
if [ -f "${COS_PE_FUNCS}" ]; then
    sed -n '/^execute_project_eris_func[[:space:]]*()/,/^}/p' "${COS_PE_FUNCS}" | \
        sed 's/^execute_project_eris_func[[:space:]]*()/orig_pe_execute_project_eris_func()/' \
        > "${COS_ORIG_FN}" 2>/dev/null
    if [ -s "${COS_ORIG_FN}" ] && /bin/sh -n "${COS_ORIG_FN}" 2>/dev/null; then
        . "${COS_ORIG_FN}"
        if command -v orig_pe_execute_project_eris_func >/dev/null 2>&1; then
            COS_HOOKED=1
        fi
    fi
fi

if [ "${COS_HOOKED}" != "1" ]; then
    # Fail safe: leave Project Eris completely untouched. Classic OS stays reachable through its
    # Boot Menu item / Stock UI launcher, but never replaces or breaks the original menu.
    classic_os_log "Supervisor hook NOT applied: execute_project_eris_func() not found/parsable in ${COS_PE_FUNCS}. Project Eris left untouched."
else
    execute_project_eris_func() {
        # Step 1: on the first call of this power cycle, auto-start Classic OS if configured
        if [ ! -f "${COS_TMP}/classic_os_boot_completed" ]; then
            echo "1" > "${COS_TMP}/classic_os_boot_completed"
            if classic_os_check_recovery_and_config; then
                classic_os_run_primary_session
            fi
        fi

        # Step 2: hand off to the original Project Eris function, honouring a "launch_classic_os"
        #         token (written by menu items) and the exit token Classic OS left behind.
        while true; do
            if [ -f "${COS_TMP}/launchfilecommand" ]; then
                PE_CMD=$(cat "${COS_TMP}/launchfilecommand" 2>/dev/null)
                if [ "${PE_CMD}" = "launch_classic_os" ]; then
                    rm -f "${COS_TMP}/launchfilecommand"
                    classic_os_run_primary_session
                    continue
                fi
            fi
            orig_pe_execute_project_eris_func "$@"
            break
        done
    }
fi
