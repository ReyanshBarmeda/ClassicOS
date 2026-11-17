#!/bin/sh
#===============================================================================
# Classic OS — Stock UI Carousel Launcher Script
# Invoked via /var/volatile/launchtmp after /tmp/intercept suspends ui_menu
# Strictly POSIX /bin/sh compatible (BusyBox ash safe, no Bashisms)
#
# All process handling lives in opt/classic_os/launch.sh (single implementation).
# From the Stock UI carousel the default return target is the Stock UI.
#===============================================================================

ERIS_CFG="${CLASSIC_OS_ERIS_CFG:-/var/volatile/project_eris.cfg}"
if [ -f "${ERIS_CFG}" ]; then
    . "${ERIS_CFG}"
fi

PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
COS_TMP="${CLASSIC_OS_TMP:-/tmp}"
CLASSIC_OS_LAUNCH="${PROJECT_ERIS_PATH}/opt/classic_os/launch.sh"

if [ ! -f "${CLASSIC_OS_LAUNCH}" ]; then
    mkdir -p "${RUNTIME_LOG_PATH:-/media/logs}" 2>/dev/null
    echo "[CLASSIC OS](STOCKUI-LAUNCHER) ${CLASSIC_OS_LAUNCH} missing; returning to the Stock UI" \
        >> "${RUNTIME_LOG_PATH:-/media/logs}/classic_os_boot.log" 2>/dev/null
    echo "launch_stockui" > "${COS_TMP}/launchfilecommand"
    exit 1
fi

exec /bin/sh "${CLASSIC_OS_LAUNCH}" --return-target launch_stockui "$@"
