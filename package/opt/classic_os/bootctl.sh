#!/bin/sh
#===============================================================================
# Classic OS — Reversible Boot Mode & Recovery Control Utility (bootctl.sh)
# Location on USB: /media/project_eris/opt/classic_os/bootctl.sh
# Strictly POSIX /bin/sh compatible (BusyBox ash safe, no Bashisms)
#
# Usage:
#   bootctl.sh --enable-primary     Classic OS becomes the auto-boot dashboard (clears crash state)
#   bootctl.sh --restore-bootmenu   Original Project Eris Boot Menu is the default again
#   bootctl.sh --enable-es-hook     Wrap EmulationStation's launch.sh so it starts Classic OS (opt-in)
#   bootctl.sh --restore-es-hook    Put the original EmulationStation launch.sh back
#   bootctl.sh --status             Print configuration, binary and crash-guard status
#   (--sync-es-hook-on / --sync-es-hook-off are the silent idempotent forms used by the supervisor)
#
# Nothing here touches anything outside ${PROJECT_ERIS_PATH} and /tmp.
# Test hooks (never needed on the console): CLASSIC_OS_ERIS_CFG, CLASSIC_OS_TMP, MOUNTPOINT
#===============================================================================

ERIS_CFG="${CLASSIC_OS_ERIS_CFG:-/var/volatile/project_eris.cfg}"
if [ -f "${ERIS_CFG}" ]; then
    . "${ERIS_CFG}"
fi

COS_TMP="${CLASSIC_OS_TMP:-/tmp}"
PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
MOUNTPOINT="${MOUNTPOINT:-/media}"
CLASSIC_OS_HOME="${PROJECT_ERIS_PATH}/opt/classic_os"
CLASSIC_OS_BIN="${CLASSIC_OS_HOME}/classic_os"
BOOT_CFG_DIR="${PROJECT_ERIS_PATH}/etc/project_eris/CFG"
BOOT_CFG_FILE="${BOOT_CFG_DIR}/classic_os_boot.cfg"
ES_LAUNCH_SCRIPT="${PROJECT_ERIS_PATH}/opt/emulationstation/launch.sh"
ES_BACKUP_SCRIPT="${PROJECT_ERIS_PATH}/opt/emulationstation/launch.sh.es_backup"
ES_MARKER="CLASSIC_OS_ES_WRAPPER"
ATTEMPT_MARKER="${BOOT_CFG_DIR}/.classic_os_boot_attempt"
CRASH_FILE="${BOOT_CFG_DIR}/.classic_os_crash_count"

# Defaults (a fresh install never changes what the console boots into until asked to)
CLASSIC_OS_PRIMARY_DASHBOARD="0"
CLASSIC_OS_HOOK_EMULATIONSTATION="0"
CLASSIC_OS_DEFAULT_EXIT_TARGET="launch_bootmenu"
CLASSIC_OS_MIN_UPTIME_SEC="2"
CLASSIC_OS_MAX_CRASHES="3"
CLASSIC_OS_READY_TIMEOUT_SEC="30"
CLASSIC_OS_VIDEO_DRIVER=""

if [ -f "${BOOT_CFG_FILE}" ]; then
    . "${BOOT_CFG_FILE}"
fi

# Rewrites the config with ALL keys (so none of the user's tuning is lost), via a temp file.
write_boot_cfg() {
    mkdir -p "${BOOT_CFG_DIR}" 2>/dev/null
    _tmp="${BOOT_CFG_FILE}.tmp"
    cat << EOF > "${_tmp}" || return 1
#===============================================================================
# Classic OS — Project Eris Primary Boot & Supervisor Configuration
# Location on USB: /media/project_eris/etc/project_eris/CFG/classic_os_boot.cfg
#===============================================================================

# 1 = Boot straight into Classic OS on power-on, 0 = original Project Eris Boot Menu
CLASSIC_OS_PRIMARY_DASHBOARD="${CLASSIC_OS_PRIMARY_DASHBOARD}"
# 1 = wrap EmulationStation's launch.sh (backup: launch.sh.es_backup), 0 = leave it alone
CLASSIC_OS_HOOK_EMULATIONSTATION="${CLASSIC_OS_HOOK_EMULATIONSTATION}"
# Token handed to Project Eris when Classic OS exits: launch_bootmenu | launch_stockui | launch_retroarch_standalone
CLASSIC_OS_DEFAULT_EXIT_TARGET="${CLASSIC_OS_DEFAULT_EXIT_TARGET}"
# Exiting sooner than this many seconds counts as a crash
CLASSIC_OS_MIN_UPTIME_SEC="${CLASSIC_OS_MIN_UPTIME_SEC}"
# Consecutive failed boots before auto-boot is switched off again
CLASSIC_OS_MAX_CRASHES="${CLASSIC_OS_MAX_CRASHES}"
# Seconds the app may take to present its first frame before it is stopped
CLASSIC_OS_READY_TIMEOUT_SEC="${CLASSIC_OS_READY_TIMEOUT_SEC}"
# Force an SDL2 video backend (empty = auto: wayland -> kmsdrm -> SDL default)
CLASSIC_OS_VIDEO_DRIVER="${CLASSIC_OS_VIDEO_DRIVER}"
EOF
    mv -f "${_tmp}" "${BOOT_CFG_FILE}"
}

is_es_wrapper() {
    [ -f "${ES_LAUNCH_SCRIPT}" ] && grep -q "${ES_MARKER}" "${ES_LAUNCH_SCRIPT}" 2>/dev/null
}

# Wrap EmulationStation's launch.sh. The original is backed up first; if the backup cannot be
# written, nothing is replaced.
install_es_hook() {
    [ -d "${PROJECT_ERIS_PATH}/opt/emulationstation" ] || return 1
    [ -f "${ES_LAUNCH_SCRIPT}" ] || return 1
    if is_es_wrapper; then
        return 0    # already installed; the backup holds the original
    fi
    cp -f "${ES_LAUNCH_SCRIPT}" "${ES_BACKUP_SCRIPT}" 2>/dev/null || return 1
    cmp -s "${ES_LAUNCH_SCRIPT}" "${ES_BACKUP_SCRIPT}" || return 1
    _tmp="${ES_LAUNCH_SCRIPT}.cos_tmp"
    cat << 'EOF' > "${_tmp}" || return 1
#!/bin/sh
# CLASSIC_OS_ES_WRAPPER — reversible EmulationStation -> Classic OS wrapper
# Restore the original with: sh /media/project_eris/opt/classic_os/bootctl.sh --restore-es-hook
if [ -f "/var/volatile/project_eris.cfg" ]; then
    . "/var/volatile/project_eris.cfg"
fi
PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
CLASSIC_OS_LAUNCH="${PROJECT_ERIS_PATH}/opt/classic_os/launch.sh"
ES_BACKUP="${PROJECT_ERIS_PATH}/opt/emulationstation/launch.sh.es_backup"

if [ "$1" = "--original-es" ] && [ -f "${ES_BACKUP}" ]; then
    shift
    exec /bin/sh "${ES_BACKUP}" "$@"
fi

if [ -x "${PROJECT_ERIS_PATH}/opt/classic_os/classic_os" ] && [ -f "${CLASSIC_OS_LAUNCH}" ]; then
    exec /bin/sh "${CLASSIC_OS_LAUNCH}" --return-target launch_bootmenu "$@"
elif [ -f "${ES_BACKUP}" ]; then
    exec /bin/sh "${ES_BACKUP}" "$@"
else
    echo "launch_bootmenu" > "/tmp/launchfilecommand"
    exit 0
fi
EOF
    mv -f "${_tmp}" "${ES_LAUNCH_SCRIPT}" || return 1
    chmod +x "${ES_LAUNCH_SCRIPT}" 2>/dev/null
    return 0
}

# Only ever restores over OUR wrapper, never over a launch.sh the user (or an Eris update) changed.
restore_es_hook() {
    if is_es_wrapper && [ -f "${ES_BACKUP_SCRIPT}" ]; then
        cp -f "${ES_BACKUP_SCRIPT}" "${ES_LAUNCH_SCRIPT}" || return 1
        chmod +x "${ES_LAUNCH_SCRIPT}" 2>/dev/null
    fi
    return 0
}

clear_crash_state() {
    rm -f "${ATTEMPT_MARKER}" "${CRASH_FILE}" 2>/dev/null
}

ACTION="${1:---status}"

case "${ACTION}" in
    --enable-primary)
        CLASSIC_OS_PRIMARY_DASHBOARD="1"
        write_boot_cfg || { echo "[CLASSIC OS](ERROR) cannot write ${BOOT_CFG_FILE}"; exit 1; }
        if [ "${CLASSIC_OS_HOOK_EMULATIONSTATION}" = "1" ]; then install_es_hook; fi
        clear_crash_state
        rm -f "${MOUNTPOINT}/CLASSIC_OS_RECOVERY" \
              "${MOUNTPOINT}/ForceBootMenu.flag" \
              "${BOOT_CFG_DIR}/disable_classic_os" 2>/dev/null
        echo "[CLASSIC OS] Primary dashboard auto-boot ENABLED (${BOOT_CFG_FILE})."
        ;;
    --restore-bootmenu|--disable-primary)
        CLASSIC_OS_PRIMARY_DASHBOARD="0"
        write_boot_cfg || { echo "[CLASSIC OS](ERROR) cannot write ${BOOT_CFG_FILE}"; exit 1; }
        restore_es_hook
        echo "launch_bootmenu" > "${COS_TMP}/launchfilecommand" 2>/dev/null
        echo "[CLASSIC OS] Primary dashboard auto-boot DISABLED. Original Project Eris Boot Menu restored."
        ;;
    --enable-es-hook)
        CLASSIC_OS_HOOK_EMULATIONSTATION="1"
        write_boot_cfg || exit 1
        if install_es_hook; then
            echo "[CLASSIC OS] EmulationStation wrapper installed (original saved as ${ES_BACKUP_SCRIPT})."
        else
            echo "[CLASSIC OS](WARN) EmulationStation not found or backup failed; launch.sh left untouched."
            exit 1
        fi
        ;;
    --restore-es-hook)
        CLASSIC_OS_HOOK_EMULATIONSTATION="0"
        write_boot_cfg || exit 1
        restore_es_hook
        echo "[CLASSIC OS] Original EmulationStation launch.sh restored."
        ;;
    --sync-es-hook-on)
        install_es_hook >/dev/null 2>&1
        ;;
    --sync-es-hook-off)
        restore_es_hook >/dev/null 2>&1
        ;;
    --status|*)
        _crashes=$(cat "${CRASH_FILE}" 2>/dev/null)
        echo "========================================================="
        echo " Classic OS — Boot & Supervisor Configuration Status"
        echo "========================================================="
        echo "Config File          : ${BOOT_CFG_FILE}$([ -f "${BOOT_CFG_FILE}" ] || echo ' (absent, defaults)')"
        echo "Primary Auto-Boot    : $([ "${CLASSIC_OS_PRIMARY_DASHBOARD}" = "1" ] && echo "ENABLED (Classic OS)" || echo "DISABLED (Project Eris Boot Menu)")"
        echo "ES Launch Hook       : $([ "${CLASSIC_OS_HOOK_EMULATIONSTATION}" = "1" ] && echo "ENABLED" || echo "DISABLED") (wrapper installed: $(is_es_wrapper && echo yes || echo no))"
        echo "Default Exit Target  : ${CLASSIC_OS_DEFAULT_EXIT_TARGET}"
        echo "Crash guard          : min uptime ${CLASSIC_OS_MIN_UPTIME_SEC}s, limit ${CLASSIC_OS_MAX_CRASHES}, failed boots recorded: ${_crashes:-0}"
        echo "Boot attempt pending : $([ -f "${ATTEMPT_MARKER}" ] && echo "YES (last boot never reached a first frame)" || echo no)"
        echo "ARM Binary Present   : $([ -f "${CLASSIC_OS_BIN}" ] && echo "YES (${CLASSIC_OS_BIN})" || echo "NO (Boot Menu fallback active)")"
        echo "========================================================="
        ;;
esac
exit 0
