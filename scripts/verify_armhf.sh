#!/bin/sh
#===============================================================================
# Classic OS — ARM ELF & Package Verification (fails loudly, never "|| true")
#
# Usage:
#   sh scripts/verify_armhf.sh [path/to/classic_os]
#
# Environment:
#   CROSS_PREFIX     Toolchain prefix for readelf (default: arm-linux-gnueabihf-)
#   PSC_MAX_GLIBC    Highest GLIBC symbol version the console can load
#                    (default: 2.28 — the PlayStation Classic baseline).
#                    Set to "none" to skip the glibc check (local experiments only).
#   PSC_EXTRA_NEEDED Space-separated extra DT_NEEDED libraries to allow.
#
# Checks:
#   1. 32-bit little-endian ARM PIE, EABI5, hard-float, ARMv8 + FP for ARMv8
#   2. Interpreter /lib/ld-linux-armhf.so.3 and RUNPATH for Sony / Project Eris libs
#   3. DT_NEEDED limited to libraries the console provides (libstdc++/libgcc static)
#   4. No GLIBC symbol version newer than PSC_MAX_GLIBC
#   5. Package layout: every shipped file exists, scripts are POSIX-valid and
#      executable, assets were really copied, boot JSON is valid
#===============================================================================
set -eu
unset CDPATH

BIN="${1:-package/opt/classic_os/classic_os}"
CROSS_PREFIX="${CROSS_PREFIX:-arm-linux-gnueabihf-}"
PSC_MAX_GLIBC="${PSC_MAX_GLIBC:-2.28}"
PSC_EXTRA_NEEDED="${PSC_EXTRA_NEEDED:-}"

SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
PKG_DIR="${PROJECT_ROOT}/package"

FAILS=0
pass() { echo "  [ OK ] $*"; }
fail() { echo "  [FAIL] $*" >&2; FAILS=$((FAILS + 1)); }
warn() { echo "  [WARN] $*"; }

# check "<ok message>" "<failure message>" command [args...]
check() {
    _ok="$1"
    _bad="$2"
    shift 2
    if "$@"; then pass "${_ok}"; else fail "${_bad}"; fi
}

# has "<text>" "<extended regex>"
has() { printf '%s\n' "$1" | grep -Eq "$2"; }

if command -v "${CROSS_PREFIX}readelf" >/dev/null 2>&1; then
    READELF="${CROSS_PREFIX}readelf"
elif command -v readelf >/dev/null 2>&1; then
    READELF="readelf"
else
    echo "[ERROR] readelf not found (install binutils-arm-linux-gnueabihf)." >&2
    exit 2
fi

# ver_gt A B  -> true when version A is strictly newer than B (dotted numeric)
ver_gt() {
    [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" = "$1" ]
}

echo "=== Verifying ARM ELF binary: ${BIN} ==="

if [ ! -f "${BIN}" ] || [ ! -s "${BIN}" ]; then
    echo "[ERROR] ${BIN} is missing or empty." >&2
    exit 1
fi

HDR="$("${READELF}" -h -W "${BIN}")"
ATTR="$("${READELF}" -A -W "${BIN}" 2>/dev/null || true)"
DYN="$("${READELF}" -d -W "${BIN}" 2>/dev/null || true)"
SEGS="$("${READELF}" -l -W "${BIN}" 2>/dev/null || true)"
SECS="$("${READELF}" -S -W "${BIN}" 2>/dev/null || true)"

#--- 1. Architecture / ABI ------------------------------------------------------
echo "--- Architecture & ABI"
check "ELF32" "not a 32-bit ELF" has "${HDR}" 'Class:[[:space:]]+ELF32'
check "little-endian" "not little-endian" has "${HDR}" 'little endian'
check "Machine: ARM" "Machine is not 32-bit ARM (host binary packaged by mistake?)" \
    has "${HDR}" 'Machine:[[:space:]]+ARM$'
check "position-independent executable (PIE)" "not a PIE/DYN executable" \
    has "${HDR}" 'Type:[[:space:]]+DYN'
check "EABI5" "not EABI5" has "${HDR}" 'Version5 EABI'
check "hard-float ABI" "not hard-float ABI (-mfloat-abi=hard missing)" has "${HDR}" 'hard-float ABI'
check "Tag_CPU_arch: v8" "Tag_CPU_arch is not v8" has "${ATTR}" 'Tag_CPU_arch:[[:space:]]+v8$'
check "Tag_FP_arch: FP for ARMv8" "Tag_FP_arch is not 'FP for ARMv8'" \
    has "${ATTR}" 'Tag_FP_arch:[[:space:]]+FP for ARMv8'
# GCC writes Tag_ABI_VFP_args; clang/LLVM omits it and relies on the e_flags hard-float marker
# (checked above). Accept either, fail only if the tag is present and says something else.
if has "${ATTR}" 'Tag_ABI_VFP_args:'; then
    check "VFP register argument passing (hard-float)" "Tag_ABI_VFP_args is not 'VFP registers'" \
        has "${ATTR}" 'Tag_ABI_VFP_args:[[:space:]]+VFP registers'
else
    pass "hard-float marked in ELF header flags (no Tag_ABI_VFP_args; clang-style build)"
fi
if has "${SECS}" '[[:space:]]\.symtab[[:space:]]'; then
    warn "binary still has a .symtab (not stripped)"
fi

#--- 2. Loader & library search path ---------------------------------------------
echo "--- Loader & library search path"
check "interpreter /lib/ld-linux-armhf.so.3" "unexpected program interpreter" \
    has "${SEGS}" '/lib/ld-linux-armhf\.so\.3'
for p in /usr/sony/lib /media/project_eris/lib; do
    check "RUNPATH contains ${p}" "RUNPATH is missing ${p}" \
        has "$(printf '%s\n' "${DYN}" | grep -E 'R(UN)?PATH' || true)" "${p}"
done

#--- 3. DT_NEEDED allowlist -------------------------------------------------------
echo "--- Shared library dependencies (DT_NEEDED)"
NEEDED="$(printf '%s\n' "${DYN}" | sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p')"
ALLOWED="libSDL2-2.0.so.0 libpng16.so.16 libsqlite3.so.0 libm.so.6 libc.so.6 libdl.so.2 libpthread.so.0 librt.so.1 ld-linux-armhf.so.3 ${PSC_EXTRA_NEEDED}"
for lib in libSDL2-2.0.so.0 libpng16.so.16 libsqlite3.so.0; do
    check "links ${lib}" "expected DT_NEEDED ${lib} is missing" \
        has "${NEEDED}" "^$(printf '%s' "${lib}" | sed 's/\./\\./g')\$"
done
for lib in ${NEEDED}; do
    case "${lib}" in
        libstdc++.so.6|libgcc_s.so.1)
            fail "${lib} is dynamically required; -static-libstdc++ -static-libgcc did not take effect (the console has no compatible copy)"
            continue ;;
    esac
    found=0
    for ok in ${ALLOWED}; do
        if [ "${lib}" = "${ok}" ]; then found=1; fi
    done
    if [ "${found}" -ne 1 ]; then
        fail "unexpected DT_NEEDED ${lib} (allow it via PSC_EXTRA_NEEDED only if the console ships it)"
    fi
done

#--- 4. glibc symbol versions -------------------------------------------------------
echo "--- glibc compatibility (console baseline: ${PSC_MAX_GLIBC})"
DYNSYMS="$("${READELF}" -W --dyn-syms "${BIN}")"
SYMS="$(printf '%s\n' "${DYNSYMS}" | awk '$7 == "UND" {
        n = index($8, "@GLIBC_")
        if (n > 0) {
            v = substr($8, n + 7)
            if (v ~ /^[0-9][0-9.]*$/) print v, substr($8, 1, n - 1)
        }
    }')"
MAX_SEEN="$(printf '%s\n' "${SYMS}" | awk 'NF {print $1}' | sort -V | tail -n 1)"
echo "  highest GLIBC symbol version required: ${MAX_SEEN:-none}"
if [ "${PSC_MAX_GLIBC}" = "none" ]; then
    warn "glibc check skipped (PSC_MAX_GLIBC=none)"
elif [ -n "${MAX_SEEN}" ] && ver_gt "${MAX_SEEN}" "${PSC_MAX_GLIBC}"; then
    fail "binary needs GLIBC_${MAX_SEEN} but the console only provides up to GLIBC_${PSC_MAX_GLIBC}. Offending symbols:"
    printf '%s\n' "${SYMS}" | sort -u | while read -r v name; do
        if ver_gt "${v}" "${PSC_MAX_GLIBC}"; then
            echo "         ${name}@GLIBC_${v}"
        fi
    done | head -n 15 >&2
    echo "         Build against an older glibc (Debian 10 container / VM) instead of a newer distro." >&2
else
    pass "all GLIBC symbol versions <= ${PSC_MAX_GLIBC}"
fi
if printf '%s\n' "${DYNSYMS}" | awk '$7 == "UND"' | grep -Eq '@(GLIBCXX|CXXABI|GCC)_'; then
    fail "binary imports GLIBCXX/CXXABI/GCC symbols from a shared libstdc++/libgcc"
else
    pass "no shared libstdc++/libgcc symbol imports"
fi

#--- 5. Package layout -----------------------------------------------------------------
echo "--- Package layout (${PKG_DIR})"
REQUIRED_FILES="
opt/classic_os/classic_os
opt/classic_os/launch.sh
opt/classic_os/bootctl.sh
opt/classic_os/check_runtime.sh
etc/project_eris/CFG/classic_os_boot.cfg
etc/project_eris/FUNC/0095_classic_os_boot.sh
etc/project_eris/SUP/launchers/classic_os/launch.sh
etc/project_eris/SUP/launchers/classic_os/launcher.cfg
etc/project_eris/SUP/launchers/classic_os/classicos.png
etc/boot_menu/boot_items/004_classic_os.json
"
for rel in ${REQUIRED_FILES}; do
    check "present: ${rel}" "missing from package: ${rel}" [ -f "${PKG_DIR}/${rel}" ]
done

EXEC_FILES="
opt/classic_os/classic_os
opt/classic_os/launch.sh
opt/classic_os/bootctl.sh
opt/classic_os/check_runtime.sh
etc/project_eris/FUNC/0095_classic_os_boot.sh
etc/project_eris/SUP/launchers/classic_os/launch.sh
"
for rel in ${EXEC_FILES}; do
    check "executable: ${rel}" "not executable: ${rel}" [ -x "${PKG_DIR}/${rel}" ]
done

SH_FILES="
opt/classic_os/launch.sh
opt/classic_os/bootctl.sh
opt/classic_os/check_runtime.sh
etc/project_eris/FUNC/0095_classic_os_boot.sh
etc/project_eris/SUP/launchers/classic_os/launch.sh
etc/project_eris/CFG/classic_os_boot.cfg
etc/project_eris/SUP/launchers/classic_os/launcher.cfg
"
for rel in ${SH_FILES}; do
    if [ -f "${PKG_DIR}/${rel}" ]; then
        check "POSIX sh syntax: ${rel}" "sh -n failed: ${rel}" sh -n "${PKG_DIR}/${rel}"
    fi
done

# Windows line endings in a script sourced by Project Eris at boot break the console's startup
has_cr() { grep -q "$(printf '\r')" "$1"; }
for rel in ${SH_FILES} etc/boot_menu/boot_items/004_classic_os.json; do
    if [ -f "${PKG_DIR}/${rel}" ]; then
        if has_cr "${PKG_DIR}/${rel}"; then fail "CR (Windows line ending) found in ${rel}"; else pass "LF line endings: ${rel}"; fi
    fi
done

JSON_FILE="${PKG_DIR}/etc/boot_menu/boot_items/004_classic_os.json"
json_valid() { python3 -m json.tool "$1" >/dev/null 2>&1; }
if [ -f "${JSON_FILE}" ]; then
    if command -v python3 >/dev/null 2>&1; then
        check "valid JSON: 004_classic_os.json" "invalid JSON: 004_classic_os.json" json_valid "${JSON_FILE}"
    else
        warn "python3 not found; skipped boot menu JSON validation"
    fi
fi

# Every source asset must have been copied (the Makefile copies with '|| true')
if [ -d "${PROJECT_ROOT}/assets" ]; then
    MISSING=0
    for a in $(cd "${PROJECT_ROOT}/assets" && find . -type f | sort); do
        if [ ! -f "${PKG_DIR}/opt/classic_os/assets/${a#./}" ]; then
            fail "asset not packaged: assets/${a#./}"
            MISSING=1
        fi
    done
    if [ "${MISSING}" -eq 0 ]; then
        pass "all assets/ files packaged under opt/classic_os/assets/"
    fi
fi

#--- Summary ---------------------------------------------------------------------------------
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    {
        echo "### Classic OS ARM verification"
        echo ""
        echo "| Check | Result |"
        echo "| --- | --- |"
        echo "| Highest GLIBC symbol required | ${MAX_SEEN:-none} (limit ${PSC_MAX_GLIBC}) |"
        echo "| DT_NEEDED | $(printf '%s' "${NEEDED}" | tr '\n' ' ') |"
        echo "| Failed checks | ${FAILS} |"
    } >> "${GITHUB_STEP_SUMMARY}"
fi

if [ "${FAILS}" -ne 0 ]; then
    echo "[FAILED] ${FAILS} verification check(s) failed." >&2
    exit 1
fi
echo "[OK] All ARM binary and package checks passed."
