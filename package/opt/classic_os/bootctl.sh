#!/bin/sh
#===============================================================================
# Classic OS — Reversible Boot Mode & Recovery Control Utility (bootctl.sh)
# Location on USB: /media/project_eris/opt/classic_os/bootctl.sh
# Strictly POSIX /bin/sh compatible (no Bashisms)
#
# Usage:
#   bootctl.sh --enable-primary     Set Classic OS as primary auto-boot dashboard
#   bootctl.sh --restore-bootmenu   Restore original Project Eris Boot Menu as primary
#   bootctl.sh --enable-es-hook     Reversibly hook EmulationStation launch.sh -> Classic OS
#   bootctl.sh --restore-es-hook    Restore original EmulationStation launch.sh from backup
#   bootctl.sh --status             Print current boot configuration & binary status
#===============================================================================

if [ -f "/var/volatile/project_eris.cfg" ]; then
    . "/var/volatile/project_eris.cfg"
fi

PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
MOUNTPOINT="${MOUNTPOINT:-/media}"
CLASSIC_OS_HOME="${PROJECT_ERIS_PATH}/opt/classic_os"
CLASSIC_OS_BIN="${CLASSIC_OS_HOME}/classic_os"
BOOT_CFG_DIR="${PROJECT_ERIS_PATH}/etc/project_eris/CFG"
BOOT_CFG_FILE="${BOOT_CFG_DIR}/classic_os_boot.cfg"
ES_LAUNCH_SCRIPT="${PROJECT_ERIS_PATH}/opt/emulationstation/launch.sh"
ES_BACKUP_SCRIPT="${PROJECT_ERIS_PATH}/opt/emulationstation/launch.sh.es_backup"

CLASSIC_OS_PRIMARY_DASHBOARD="1"
CLASSIC_OS_HOOK_EMULATIONSTATION="1"
CLASSIC_OS_DEFAULT_EXIT_TARGET="launch_bootmenu"
CLASSIC_OS_MIN_UPTIME_SEC="2"

if [ -f "${BOOT_CFG_FILE}" ]; then
    . "${BOOT_CFG_FILE}"
fi

write_boot_cfg() {
    mkdir -p "${BOOT_CFG_DIR}"
    cat << EOF > "${BOOT_CFG_FILE}"
#===============================================================================
# Classic OS — Project Eris Primary Boot & Supervisor Configuration
# Location on USB: /media/project_eris/etc/project_eris/CFG/classic_os_boot.cfg
#===============================================================================

CLASSIC_OS_PRIMARY_DASHBOARD="${CLASSIC_OS_PRIMARY_DASHBOARD}"
CLASSIC_OS_HOOK_EMULATIONSTATION="${CLASSIC_OS_HOOK_EMULATIONSTATION}"
CLASSIC_OS_DEFAULT_EXIT_TARGET="${CLASSIC_OS_DEFAULT_EXIT_TARGET}"
CLASSIC_OS_MIN_UPTIME_SEC="${CLASSIC_OS_MIN_UPTIME_SEC}"
EOF
}

install_es_hook() {
    if [ -f "${ES_LAUNCH_SCRIPT}" ] && [ ! -f "${ES_BACKUP_SCRIPT}" ]; then
        if ! grep -q "CLASSIC_OS_ES_WRAPPER" "${ES_LAUNCH_SCRIPT}" 2>/dev/null; then
            cp -f "${ES_LAUNCH_SCRIPT}" "${ES_BACKUP_SCRIPT}"
        fi
    fi
    if [ -d "${PROJECT_ERIS_PATH}/opt/emulationstation" ]; then
        cat << 'EOF' > "${ES_LAUNCH_SCRIPT}"
#!/bin/sh
# CLASSIC_OS_ES_WRAPPER — Reversible EmulationStation -> Classic OS Wrapper
if [ -f "/var/volatile/project_eris.cfg" ]; then
    . "/var/volatile/project_eris.cfg"
fi
PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
CLASSIC_OS_LAUNCH="${PROJECT_ERIS_PATH}/opt/classic_os/launch.sh"
ES_BACKUP="${PROJECT_ERIS_PATH}/opt/emulationstation/launch.sh.es_backup"

if [ "$1" = "--original-es" ] && [ -f "${ES_BACKUP}" ]; then
    shift
    exec "/bin/sh" "${ES_BACKUP}" "$@"
fi

if [ -x "${PROJECT_ERIS_PATH}/opt/classic_os/classic_os" ] && [ -f "${CLASSIC_OS_LAUNCH}" ]; then
    exec "/bin/sh" "${CLASSIC_OS_LAUNCH}" --return-target "launch_bootmenu" "$@"
elif [ -f "${ES_BACKUP}" ]; then
    exec "/bin/sh" "${ES_BACKUP}" "$@"
else
    echo "launch_bootmenu" > "/tmp/launchfilecommand"
    exit 0
fi
EOF
        chmod +x "${ES_LAUNCH_SCRIPT}"
    fi
}

restore_es_hook() {
    if [ -f "${ES_BACKUP_SCRIPT}" ]; then
        cp -f "${ES_BACKUP_SCRIPT}" "${ES_LAUNCH_SCRIPT}"
        chmod +x "${ES_LAUNCH_SCRIPT}"
    fi
}

ACTION="${1:---status}"

case "${ACTION}" in
    --enable-primary)
        CLASSIC_OS_PRIMARY_DASHBOARD="1"
        CLASSIC_OS_HOOK_EMULATIONSTATION="1"
        write_boot_cfg
        install_es_hook
        rm -f "${MOUNTPOINT}/CLASSIC_OS_RECOVERY" \
              "${MOUNTPOINT}/ForceBootMenu.flag" \
              "${BOOT_CFG_DIR}/disable_classic_os" 2>/dev/null || true
        echo "[CLASSIC OS] Primary dashboard auto-boot ENABLED (${BOOT_CFG_FILE})."
        ;;
    --restore-bootmenu|--disable-primary)
        CLASSIC_OS_PRIMARY_DASHBOARD="0"
        CLASSIC_OS_HOOK_EMULATIONSTATION="0"
        write_boot_cfg
        restore_es_hook
        echo "launch_bootmenu" > "/tmp/launchfilecommand" 2>/dev/null || true
        echo "[CLASSIC OS] Primary dashboard auto-boot DISABLED. Original Project Eris Boot Menu restored."
        ;;
    --enable-es-hook)
        CLASSIC_OS_HOOK_EMULATIONSTATION="1"
        write_boot_cfg
        install_es_hook
        echo "[CLASSIC OS] EmulationStation reversible wrapper installed (backup: ${ES_BACKUP_SCRIPT})."
        ;;
    --restore-es-hook)
        CLASSIC_OS_HOOK_EMULATIONSTATION="0"
        write_boot_cfg
        restore_es_hook
        echo "[CLASSIC OS] Restored original EmulationStation launch.sh."
        ;;
    --status|*)
        echo "========================================================="
        echo " Classic OS — Boot & Supervisor Configuration Status"
        echo "========================================================="
        echo "Config File          : ${BOOT_CFG_FILE}"
        echo "Primary Auto-Boot    : $([ "${CLASSIC_OS_PRIMARY_DASHBOARD}" = "1" ] && echo "ENABLED (Classic OS)" || echo "DISABLED (Project Eris Boot Menu)")"
        echo "ES Launch Hook       : $([ "${CLASSIC_OS_HOOK_EMULATIONSTATION}" = "1" ] && echo "ENABLED" || echo "DISABLED")"
        echo "Default Exit Target  : ${CLASSIC_OS_DEFAULT_EXIT_TARGET}"
        echo "ARM Binary Present   : $([ -x "${CLASSIC_OS_BIN}" ] && echo "YES (${CLASSIC_OS_BIN})" || echo "NO (Fallback to Boot Menu active)")"
        echo "========================================================="
        ;;
esac
