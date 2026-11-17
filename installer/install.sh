#!/bin/sh
#===============================================================================
# Classic OS — Non-destructive installer for a Project Eris USB drive
#
# Usage:  sh install.sh <usb-root> [--primary] [--dry-run]
#   <usb-root>   mount point of the SONY USB drive (the folder that contains project_eris/)
#                Linux: /media/$USER/SONY   macOS: /Volumes/SONY   Git Bash on Windows: /e
#   --primary    also enable auto-boot (default: OFF — start it from the Eris menu first)
#   --dry-run    show what would happen, change nothing
#
# Safety rules (all enforced below):
#   * only files inside <usb-root>/project_eris are touched; nothing else on the drive,
#     and never the console's internal storage
#   * refuses to run unless Project Eris is already installed there
#   * every existing file it would replace is copied to
#       <usb-root>/project_eris/.classic_os_backup/<timestamp>/files/... FIRST
#   * your existing classic_os_boot.cfg is kept (never overwritten)
#   * no recursive deletes, no wildcards: uninstall.sh restores from the MANIFEST
#   * scripts are checked with `sh -n` and have CR characters stripped (Windows line endings
#     would break Project Eris, which sources FUNC/*.sh at boot)
#===============================================================================
set -u
unset CDPATH

SELF_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
# release zip: ./payload ; source checkout: ../package
if [ -d "${SELF_DIR}/payload" ]; then PAYLOAD="${SELF_DIR}/payload"; else PAYLOAD="${SELF_DIR}/../package"; fi
USB=""
PRIMARY=0
DRY=0

for a in "$@"; do
    case "$a" in
        --primary) PRIMARY=1 ;;
        --dry-run) DRY=1 ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        -*) echo "unknown option: $a" >&2; exit 2 ;;
        *) USB="$a" ;;
    esac
done

die() { echo "[ERROR] $*" >&2; exit 1; }
say() { echo "[install] $*"; }

[ -n "${USB}" ] || die "usage: sh install.sh <usb-root> [--primary] [--dry-run]"
USB="${USB%/}"
PE="${USB}/project_eris"
[ -d "${PAYLOAD}/opt" ]               || die "payload (opt/, etc/) not found next to install.sh"
[ -d "${PE}/etc/project_eris" ]       || die "${PE}/etc/project_eris not found — Project Eris must already be installed on this drive. Nothing was changed."
[ -w "${PE}" ]                        || die "${PE} is not writable (read-only mount?)"

#--- pre-flight on the payload (before anything is written) ---------------------------
BIN="${PAYLOAD}/opt/classic_os/classic_os"
[ -f "${BIN}" ] || die "payload has no classic_os binary (this is a source-only package; build it first)"
HDR="$(dd if="${BIN}" bs=1 count=20 2>/dev/null | od -An -tx1 | tr -d ' \n')"
[ "$(echo "${HDR}" | cut -c1-8)" = "7f454c46" ] || die "classic_os is not an ELF file"
[ "$(echo "${HDR}" | cut -c9-10)" = "01" ]      || die "classic_os is not a 32-bit ELF"
[ "$(echo "${HDR}" | cut -c37-40)" = "2800" ]   || die "classic_os is not an ARM ELF (host binary packaged by mistake?)"

TMP_CLEAN="${TMPDIR:-/tmp}/classic_os_install.$$"
mkdir -p "${TMP_CLEAN}" || die "cannot create temp dir"
cleanup() { rm -f "${TMP_CLEAN}"/* 2>/dev/null; rmdir "${TMP_CLEAN}" 2>/dev/null; }
trap cleanup EXIT

# Text files get CRs removed; the cleaned copy is what is syntax-checked and installed.
is_text() { case "$1" in *.sh|*.cfg|*.json) return 0 ;; esac; return 1; }

FILES="$(cd "${PAYLOAD}" && find . -type f | sed 's|^\./||' | sort)"
for rel in ${FILES}; do
    if is_text "${rel}"; then
        tr -d '\r' < "${PAYLOAD}/${rel}" > "${TMP_CLEAN}/check.txt"
        case "${rel}" in *.sh) sh -n "${TMP_CLEAN}/check.txt" 2>/dev/null || die "syntax error in ${rel}; refusing to install" ;; esac
    fi
done
say "payload checked: ARM ELF binary, $(echo "${FILES}" | wc -l | tr -d ' ') files, scripts parse cleanly"

STAMP="$(date +%Y%m%d-%H%M%S 2>/dev/null || echo manual)"
BK_ROOT="${PE}/.classic_os_backup"
BK="${BK_ROOT}/${STAMP}"
MANIFEST="${BK}/MANIFEST"

if [ "${DRY}" = "1" ]; then say "DRY RUN — nothing will be written"; fi
[ "${DRY}" = "1" ] || { mkdir -p "${BK}/files" || die "cannot create backup folder ${BK}"; : > "${MANIFEST}"; }

REPLACED=0; ADDED=0; KEPT=0
install_one() {   # $1 = relative path
    rel="$1"
    src="${PAYLOAD}/${rel}"
    dst="${PE}/${rel}"

    # The user's own config is never overwritten.
    if [ "${rel}" = "etc/project_eris/CFG/classic_os_boot.cfg" ] && [ -f "${dst}" ]; then
        say "keep existing ${rel}"
        KEPT=$((KEPT + 1)); return 0
    fi

    if [ "${DRY}" = "1" ]; then
        if [ -f "${dst}" ]; then say "would back up and replace ${rel}"; else say "would add ${rel}"; fi
        return 0
    fi

    dstdir="$(dirname "${dst}")"
    # Directories we create are recorded (deepest last) so uninstall can rmdir them if empty.
    d="${dstdir}"; newdirs=""
    while [ ! -d "${d}" ] && [ "${d}" != "${PE}" ] && [ "${d}" != "/" ]; do
        newdirs="${d} ${newdirs}"; d="$(dirname "${d}")"
    done
    mkdir -p "${dstdir}" || die "cannot create ${dstdir}"
    for nd in ${newdirs}; do echo "DIR ${nd#"${PE}"/}" >> "${MANIFEST}"; done

    if [ -f "${dst}" ]; then
        mkdir -p "${BK}/files/$(dirname "${rel}")" || die "cannot create backup dir"
        cp -p "${dst}" "${BK}/files/${rel}" || die "backup of ${rel} failed; nothing replaced"
        cmp -s "${dst}" "${BK}/files/${rel}" || die "backup of ${rel} does not match original; nothing replaced"
        echo "REPLACED ${rel}" >> "${MANIFEST}"
        REPLACED=$((REPLACED + 1))
    else
        echo "NEW ${rel}" >> "${MANIFEST}"
        ADDED=$((ADDED + 1))
    fi

    tmp="${dst}.cos_new"
    if is_text "${rel}"; then tr -d '\r' < "${src}" > "${tmp}" || die "write failed: ${rel}"
    else cp "${src}" "${tmp}" || die "write failed: ${rel}"; fi
    case "${rel}" in *.sh|opt/classic_os/classic_os) chmod +x "${tmp}" 2>/dev/null ;; esac
    mv -f "${tmp}" "${dst}" || die "cannot place ${rel}"

    # verify what landed on the drive
    if is_text "${rel}"; then tr -d '\r' < "${src}" > "${TMP_CLEAN}/exp.txt"; else cp "${src}" "${TMP_CLEAN}/exp.txt"; fi
    cmp -s "${TMP_CLEAN}/exp.txt" "${dst}" || die "verification failed for ${rel} (bad USB drive?). Run uninstall.sh to roll back."
    echo "CKSUM ${rel} $(cksum < "${dst}" | tr ' ' ':')" >> "${MANIFEST}"
}

# 0095 goes in LAST: until it exists, Project Eris behaves exactly as before.
ORDERED="$(echo "${FILES}" | grep -v '^etc/project_eris/FUNC/0095_classic_os_boot.sh$')
etc/project_eris/FUNC/0095_classic_os_boot.sh"
for rel in ${ORDERED}; do install_one "${rel}"; done

if [ "${DRY}" = "1" ]; then say "dry run finished"; exit 0; fi

echo "BACKUP ${STAMP}" >> "${MANIFEST}"
echo "${STAMP}" > "${BK_ROOT}/LATEST"

if [ "${PRIMARY}" = "1" ]; then
    PROJECT_ERIS_PATH="${PE}" MOUNTPOINT="${USB}" CLASSIC_OS_ERIS_CFG=/nonexistent \
        sh "${PE}/opt/classic_os/bootctl.sh" --enable-primary || say "WARN: could not enable auto-boot"
fi

say "done: ${ADDED} added, ${REPLACED} replaced (backed up), ${KEPT} kept."
say "backup + manifest: ${BK}"
if [ "${PRIMARY}" = "1" ]; then
    say "auto-boot is ENABLED. Recovery: create an empty file named CLASSIC_OS_RECOVERY in the drive root."
else
    say "auto-boot is OFF. Start Classic OS from the Project Eris menu, then enable it with:"
    say "  sh /media/project_eris/opt/classic_os/bootctl.sh --enable-primary"
fi
say "Eject the drive safely before unplugging it."
