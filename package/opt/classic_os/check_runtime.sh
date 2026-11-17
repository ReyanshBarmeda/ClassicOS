#!/bin/sh
#===============================================================================
# Classic OS — on-console runtime pre-flight (POSIX sh / busybox ash)
# Location on USB: /media/project_eris/opt/classic_os/check_runtime.sh
#
# Verifies, on the machine that is actually going to run it, that the native
# executable can be loaded: file present + executable, ELF is 32-bit ARM, the
# dynamic loader exists, and every DT_NEEDED library resolves with the library
# search path Classic OS uses. Nothing is assumed from the development machine.
#
# Usage:  check_runtime.sh [--quiet]
# Exit :  0 = loadable, 1 = problem found (details in the report), 2 = bad usage
# Report: ${RUNTIME_LOG_PATH}/classic_os_runtime_check.log
#===============================================================================

PROJECT_ERIS_PATH="${PROJECT_ERIS_PATH:-/media/project_eris}"
RUNTIME_LOG_PATH="${RUNTIME_LOG_PATH:-/media/logs}"
CLASSIC_OS_HOME="${CLASSIC_OS_HOME:-${PROJECT_ERIS_PATH}/opt/classic_os}"
CLASSIC_OS_BIN="${CLASSIC_OS_BIN:-${CLASSIC_OS_HOME}/classic_os}"
LOADER="${CLASSIC_OS_LOADER:-/lib/ld-linux-armhf.so.3}"
QUIET=0
[ "$1" = "--quiet" ] && QUIET=1

SEARCH_PATH="/usr/sony/lib:${PROJECT_ERIS_PATH}/lib:${CLASSIC_OS_HOME}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
REPORT="${RUNTIME_LOG_PATH}/classic_os_runtime_check.log"
mkdir -p "${RUNTIME_LOG_PATH}" 2>/dev/null || REPORT="/dev/null"
: > "${REPORT}" 2>/dev/null || true

FAILS=0
say()  { [ "${QUIET}" = "1" ] || echo "$*"; echo "$*" >> "${REPORT}" 2>/dev/null || true; }
fail() { say "[FAIL] $*"; FAILS=$((FAILS + 1)); }
ok()   { say "[ OK ] $*"; }

say "Classic OS runtime check — $(date 2>/dev/null)"
say "kernel: $(uname -srm 2>/dev/null)"

# 1. binary present and executable
if [ ! -f "${CLASSIC_OS_BIN}" ]; then
    fail "binary missing: ${CLASSIC_OS_BIN}"
elif [ ! -x "${CLASSIC_OS_BIN}" ]; then
    # FAT/exFAT USB mounts often hide the exec bit; try to set it, then re-test.
    chmod +x "${CLASSIC_OS_BIN}" 2>/dev/null
    if [ -x "${CLASSIC_OS_BIN}" ]; then ok "binary made executable"
    else fail "binary is not executable and chmod failed (mount may be noexec): ${CLASSIC_OS_BIN}"; fi
else
    ok "binary present: ${CLASSIC_OS_BIN}"
fi

# 2. ELF header: magic, class 32 (byte 5 = 01), little endian (byte 6 = 01), machine ARM (0x28 at offset 18)
if [ -f "${CLASSIC_OS_BIN}" ]; then
    HDR=$(dd if="${CLASSIC_OS_BIN}" bs=1 count=20 2>/dev/null | od -An -tx1 | tr -d ' \n')
    ELF_MAGIC=$(echo "${HDR}" | cut -c1-8)     # 7f454c46
    ELF_CLASS=$(echo "${HDR}" | cut -c9-10)    # 01 = 32-bit
    ELF_DATA=$(echo "${HDR}" | cut -c11-12)    # 01 = little endian
    ELF_MACH=$(echo "${HDR}" | cut -c37-40)    # 2800 = EM_ARM (0x28, little endian)
    if [ "${ELF_MAGIC}" = "7f454c46" ] && [ "${ELF_CLASS}" = "01" ] && [ "${ELF_DATA}" = "01" ] && [ "${ELF_MACH}" = "2800" ]; then
        ok "ELF32 little-endian ARM (e_machine=0x28)"
    else
        fail "not a 32-bit little-endian ARM ELF (first 20 bytes: ${HDR})"
    fi
fi

# 3. dynamic loader
if [ -e "${LOADER}" ]; then ok "ELF interpreter present: ${LOADER}"
else fail "ELF interpreter missing: ${LOADER} (not an armhf userland?)"; fi

# 4. every DT_NEEDED and every versioned symbol must resolve (glibc loader's trace mode —
#    no code of the app is run). Test hook: CLASSIC_OS_RUN_PREFIX="qemu-arm -L <sysroot>".
if [ -f "${CLASSIC_OS_BIN}" ] && [ -e "${LOADER}" ]; then
    if [ -n "${CLASSIC_OS_RUN_PREFIX}" ]; then
        # qemu-user: pass the trace variables to the *guest* only (QEMU_SET_ENV), otherwise
        # qemu's own host loader would print the trace instead of the ARM binary's.
        TRACE=$(QEMU_SET_ENV="LD_TRACE_LOADED_OBJECTS=1,LD_LIBRARY_PATH=${SEARCH_PATH}" ${CLASSIC_OS_RUN_PREFIX} "${CLASSIC_OS_BIN}" 2>&1)
    elif [ -x "${CLASSIC_OS_BIN}" ]; then
        TRACE=$(LD_LIBRARY_PATH="${SEARCH_PATH}" LD_TRACE_LOADED_OBJECTS=1 "${CLASSIC_OS_BIN}" 2>&1)
    else
        TRACE=$("${LOADER}" --library-path "${SEARCH_PATH}" --list "${CLASSIC_OS_BIN}" 2>&1)
    fi
    echo "${TRACE}" >> "${REPORT}" 2>/dev/null || true
    if [ -z "${TRACE}" ]; then
        fail "loader trace produced no output (binary cannot be loaded)"
    else
        BAD=0
        if echo "${TRACE}" | grep -q "=> not found"; then
            BAD=1
            for lib in $(echo "${TRACE}" | grep "=> not found" | sed 's/^[[:space:]]*\([^ ]*\) =>.*/\1/'); do
                say "[FAIL] shared library not provided by this console: ${lib}"
            done
        fi
        if echo "${TRACE}" | grep -q "version .* not found"; then
            BAD=1
            echo "${TRACE}" | grep "version .* not found" | sed 's/^[[:space:]]*/[FAIL] glibc too old for this binary: /' | while read -r line; do say "${line}"; done
        fi
        if echo "${TRACE}" | grep -qi "cannot execute\|exec format\|not a dynamic\|cannot open shared object"; then
            BAD=1
            say "[FAIL] loader could not load the binary (see report)"
        fi
        if [ "${BAD}" -eq 0 ]; then ok "all shared libraries and symbol versions resolve"; else FAILS=$((FAILS + 1)); fi
    fi
fi

# 5. display environment (informational — the app probes backends itself)
say "info: XDG_RUNTIME_DIR='${XDG_RUNTIME_DIR}' WAYLAND_DISPLAY='${WAYLAND_DISPLAY}' SDL_VIDEODRIVER='${SDL_VIDEODRIVER}'"
if [ -n "${XDG_RUNTIME_DIR}" ] && [ -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY:-wayland-0}" ]; then
    say "info: wayland socket present"
else
    say "info: no wayland socket in current environment"
fi
[ -e /dev/dri/card0 ] && say "info: /dev/dri/card0 present (kmsdrm possible)" || say "info: /dev/dri/card0 absent"
[ -e /dev/fb0 ]       && say "info: /dev/fb0 present" || say "info: /dev/fb0 absent"
ls /dev/input 2>/dev/null | tr '\n' ' ' | sed 's/^/info: \/dev\/input: /' >> "${REPORT}" 2>/dev/null

if [ "${FAILS}" -ne 0 ]; then
    say "RESULT: ${FAILS} problem(s) — Classic OS will not be started; Project Eris Boot Menu stays in control."
    exit 1
fi
say "RESULT: runtime check passed"
exit 0
