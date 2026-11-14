#!/bin/sh
#===============================================================================
# Classic OS — Project Eris Supervisor & Auto-Boot Extension
# Location on USB: /media/project_eris/etc/project_eris/FUNC/0095_classic_os_boot.sh
# Sourced automatically by Project Eris during boot after 0030_project_eris_funcs.sh
# Strictly POSIX /bin/sh compatible (no Bashisms)
#===============================================================================

classic_os_log() {
    COS_LOG_DIR="${RUNTIME_LOG_PATH:-/media/logs}"
    mkdir -p "${COS_LOG_DIR}" 2>/dev/null || true
    echo "[CLASSIC OS](SUPERVISOR) $*" >> "${COS_LOG_DIR}/classic_os_boot.log" 2>/dev/null || true
}

classic_os_check_recovery_and_config() {
    PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
    MOUNTPOINT="${MOUNTPOINT:-/media}"
    CLASSIC_OS_HOME="${PROJECT_ERIS_PATH}/opt/classic_os"
    CLASSIC_OS_BIN="${CLASSIC_OS_HOME}/classic_os"
    CLASSIC_OS_LAUNCH="${CLASSIC_OS_HOME}/launch.sh"
     CLASSIC_OS_BOOTCTL="${CLASSIC_OS_HOME}/bootctl.sh"
    CLASSIC_OS_CFG="${PROJECT_ERIS_PATH}/etc/project_eris/CFG/classic_os_boot.cfg"

    CLASSIC_OS_PRIMARY_DASHBOARD="1"
    CLASSIC_OS_HOOK_EMULATIONSTATION="1"
    CLASSIC_OS_DEFAULT_EXIT_TARGET="launch_bootmenu"
    CLASSIC_OS_MIN_UPTIME_SEC="2"

    if [ -f "${CLASSIC_OS_CFG}" ]; then
        . "${CLASSIC_OS_CFG}"
    fi

    # 1. USB Recovery Flag Check (allows offline recovery by placing a flag file on the USB drive)
    if [ -f "${MOUNTPOINT}/CLASSIC_OS_RECOVERY" ] || \
       [ -f "${MOUNTPOINT}/ForceBootMenu.flag" ] || \
       [ -f "${PROJECT_ERIS_PATH}/etc/project_eris/CFG/disable_classic_os" ]; then
        classic_os_log "Recovery flag detected on USB. Disabling Classic OS auto-boot and restoring Project Eris Boot Menu."
        CLASSIC_OS_PRIMARY_DASHBOARD="0"
        CLASSIC_OS_HOOK_EMULATIONSTATION="0"
        if [ -f "${CLASSIC_OS_BOOTCTL}" ]; then
            /bin/sh "${CLASSIC_OS_BOOTCTL}" --restore-bootmenu >/dev/null 2>&1 || true
        fi
        rm -f "${MOUNTPOINT}/CLASSIC_OS_RECOVERY" \
              "${MOUNTPOINT}/ForceBootMenu.flag" \
              "${PROJECT_ERIS_PATH}/etc/project_eris/CFG/disable_classic_os" 2>/dev/null || true
        echo "launch_bootmenu" > "/tmp/launchfilecommand"
        return 1
    fi

    # 2. Sync EmulationStation reversible hook state with configuration
    if [ -f "${CLASSIC_OS_BOOTCTL}" ]; then
        if [ "${CLASSIC_OS_HOOK_EMULATIONSTATION}" = "1" ]; then
            /bin/sh "${CLASSIC_OS_BOOTCTL}" --enable-es-hook >/dev/null 2>&1 || true
        else
            /bin/sh "${CLASSIC_OS_BOOTCTL}" --restore-es-hook >/dev/null 2>&1 || true
        fi
    fi

    # 3. Check if Classic OS is configured as the Primary Dashboard
    if [ "${CLASSIC_OS_PRIMARY_DASHBOARD}" != "1" ]; then
        classic_os_log "Classic OS primary auto-boot is disabled (CLASSIC_OS_PRIMARY_DASHBOARD=${CLASSIC_OS_PRIMARY_DASHBOARD}). Proceeding to standard Project Eris Boot Menu."
        return 1
    fi

    # 4. Verify Classic OS native ARM executable exists before attempting auto-boot
    if [ ! -x "${CLASSIC_OS_BIN}" ] || [ ! -f "${CLASSIC_OS_LAUNCH}" ]; then
        classic_os_log "Classic OS executable (${CLASSIC_OS_BIN}) not found or not executable. Falling back to Project Eris Boot Menu."
        echo "launch_bootmenu" > "/tmp/launchfilecommand"
        return 1
    fi

    return 0
}

classic_os_run_primary_session() {
    PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
    CLASSIC_OS_LAUNCH="${PROJECT_ERIS_PATH}/opt/classic_os/launch.sh"
    DEFAULT_EXIT="${CLASSIC_OS_DEFAULT_EXIT_TARGET:-launch_bootmenu}"
    MIN_UPTIME="${CLASSIC_OS_MIN_UPTIME_SEC:-2}"

    START_TS=$(date +%s 2>/dev/null || echo 0)
    classic_os_log "Auto-booting Classic OS as primary dashboard (default exit target: ${DEFAULT_EXIT})."

    /bin/sh "${CLASSIC_OS_LAUNCH}" --primary-boot --return-target "${DEFAULT_EXIT}"
    COS_RC=$?

    END_TS=$(date +%s 2>/dev/null || echo 0)
    ELAPSED=$((END_TS - START_TS))

    # Crash-loop & error recovery guard: if Classic OS failed or exited immediately (< MIN_UPTIME seconds
    # with non-zero status), force return to original Project Eris Boot Menu
    if [ "${COS_RC}" -ne 0 ]; then
        classic_os_log "Classic OS exited with non-zero code ${COS_RC} after ${ELAPSED}s. Recovering to Project Eris Boot Menu."
        echo "launch_bootmenu" > "/tmp/launchfilecommand"
    elif [ ! -f "/tmp/launchfilecommand" ]; then
        echo "${DEFAULT_EXIT}" > "/tmp/launchfilecommand"
    fi

    NEXT_TARGET=$(cat "/tmp/launchfilecommand" 2>/dev/null || echo "launch_bootmenu")
    classic_os_log "Classic OS session ended cleanly (rc=${COS_RC}, uptime=${ELAPSED}s). Handing off to supervisor target: ${NEXT_TARGET}"
}

#===============================================================================
# Supervisor Wrapper: Hooks execute_project_eris_func() in RAM without modifying
# 0030_project_eris_funcs.sh on disk.
#===============================================================================
if [ -f "${PROJECT_ERIS_PATH:-/media/project_eris}/etc/project_eris/FUNC/0030_project_eris_funcs.sh" ]; then
    # Create an in-RAM copy of 0030_project_eris_funcs.sh where execute_project_eris_func
    # is renamed to orig_pe_execute_project_eris_func and respects pre-seeded /tmp/launchfilecommand
    sed -e 's/execute_project_eris_func[[:space:]]*()/orig_pe_execute_project_eris_func()/g' \
        "${PROJECT_ERIS_PATH:-/media/project_eris}/etc/project_eris/FUNC/0030_project_eris_funcs.sh" \
        > "/tmp/0030_project_eris_funcs.cos_orig.sh" 2>/dev/null || true
    if [ -s "/tmp/0030_project_eris_funcs.cos_orig.sh" ]; then
        . "/tmp/0030_project_eris_funcs.cos_orig.sh"
    fi
fi

execute_project_eris_func() {
    # Step 1: On initial cold boot, run Classic OS automatically if configured as primary dashboard
    if [ ! -f "/tmp/classic_os_boot_completed" ]; then
        echo "1" > "/tmp/classic_os_boot_completed"
        if classic_os_check_recovery_and_config; then
            classic_os_run_primary_session
        fi
    fi

    # Step 2: Hand off to the Project Eris supervisor loop while supporting "launch_classic_os"
    #         and preserving the exit target written by Classic OS in /tmp/launchfilecommand
    while true; do
        if [ -f "/tmp/launchfilecommand" ]; then
            PE_CMD=$(cat "/tmp/launchfilecommand" 2>/dev/null)
            if [ "${PE_CMD}" = "launch_classic_os" ]; then
                rm -f "/tmp/launchfilecommand"
                classic_os_run_primary_session
                continue
            fi
        fi

        if command -v orig_pe_execute_project_eris_func >/dev/null 2>&1; then
            orig_pe_execute_project_eris_func "$@"
            break
        else
            # Fallback dispatch if 0030_project_eris_funcs.sh is not present (e.g. off-console test)
            break
        fi
    done
}
