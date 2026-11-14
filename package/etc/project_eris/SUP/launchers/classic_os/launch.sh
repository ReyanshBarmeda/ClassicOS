#!/bin/sh
#===============================================================================
# Classic OS — Stock UI Carousel Launcher Script
# Invoked via /var/volatile/launchtmp after /tmp/intercept suspends ui_menu
# Strictly POSIX /bin/sh compatible (no Bashisms)
#===============================================================================

if [ -f "/var/volatile/project_eris.cfg" ]; then
    . "/var/volatile/project_eris.cfg"
fi

PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
RUNTIME_LOG_PATH="${RUNTIME_LOG_PATH:-/media/logs}"
CLASSIC_OS_HOME="${PROJECT_ERIS_PATH}/opt/classic_os"

mkdir -p "${RUNTIME_LOG_PATH}"

[ -d "/data/power" ] && echo 2 > "/data/power/disable"

# Default return target when launched from Stock UI Carousel is launch_stockui
# (User can also choose Return to Boot Menu inside Classic OS Exit Menu)
echo "launch_stockui" > "/tmp/launchfilecommand"

export HOME="${CLASSIC_OS_HOME}"
export LD_LIBRARY_PATH="/usr/sony/lib:${PROJECT_ERIS_PATH}/lib:${CLASSIC_OS_HOME}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

EXIT_CODE=0
if [ -x "${CLASSIC_OS_HOME}/classic_os" ]; then
    cd "${CLASSIC_OS_HOME}" || exit 1
    SDL_VIDEODRIVER=wayland "${CLASSIC_OS_HOME}/classic_os" \
        --project-eris-path "${PROJECT_ERIS_PATH}" \
        --log-folder "${RUNTIME_LOG_PATH}" \
        --return-target "launch_stockui" > "${RUNTIME_LOG_PATH}/classic_os.log" 2>&1
    EXIT_CODE=$?
elif [ -x "/var/volatile/launchtmp/classic_os" ]; then
    cd "/var/volatile/launchtmp" || exit 1
    SDL_VIDEODRIVER=wayland "./classic_os" \
        --project-eris-path "${PROJECT_ERIS_PATH}" \
        --log-folder "${RUNTIME_LOG_PATH}" \
        --return-target "launch_stockui" > "${RUNTIME_LOG_PATH}/classic_os.log" 2>&1
    EXIT_CODE=$?
fi

[ -d "/data/power" ] && echo 1 > "/data/power/disable"
exit "${EXIT_CODE}"
