#!/bin/sh
#===============================================================================
# Classic OS — Uninstall / rollback (reverses install.sh using its MANIFEST)
#
# Usage:  sh uninstall.sh <usb-root> [backup-id] [--dry-run]
#   backup-id  folder name under project_eris/.classic_os_backup (default: LATEST)
#
# What it does, per MANIFEST line:
#   NEW <file>       removed — but only if it is still byte-identical to what was installed
#                    (a file you edited is left in place and reported)
#   REPLACED <file>  the backed-up original is copied back
#   DIR <dir>        removed with rmdir (only succeeds if empty)
# Plus: if EmulationStation's launch.sh is still Classic OS's wrapper, the original is restored.
# Never recursive, never wildcards, never touches anything outside <usb-root>/project_eris.
# Logs (/logs) and your games are not touched.
#
# Offline panic button (no scripts needed): create an empty file CLASSIC_OS_RECOVERY in the
# drive root, or rename FUNC/0095_classic_os_boot.sh to 0095_classic_os_boot.sh.off.
#===============================================================================
set -u
unset CDPATH

USB=""; ID=""; DRY=0
for a in "$@"; do
    case "$a" in
        --dry-run) DRY=1 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        -*) echo "unknown option: $a" >&2; exit 2 ;;
        *) if [ -z "${USB}" ]; then USB="$a"; else ID="$a"; fi ;;
    esac
done
die() { echo "[ERROR] $*" >&2; exit 1; }
say() { echo "[uninstall] $*"; }

[ -n "${USB}" ] || die "usage: sh uninstall.sh <usb-root> [backup-id] [--dry-run]"
USB="${USB%/}"
PE="${USB}/project_eris"
BK_ROOT="${PE}/.classic_os_backup"
[ -d "${BK_ROOT}" ] || die "no ${BK_ROOT} — nothing recorded by install.sh on this drive"
[ -n "${ID}" ] || ID="$(cat "${BK_ROOT}/LATEST" 2>/dev/null)"
[ -n "${ID}" ] || die "no LATEST backup recorded; pass a backup-id"
BK="${BK_ROOT}/${ID}"
MANIFEST="${BK}/MANIFEST"
[ -f "${MANIFEST}" ] || die "manifest not found: ${MANIFEST}"
say "rolling back ${ID}$([ "${DRY}" = "1" ] && echo ' (DRY RUN)')"

# Step 1: remove the supervisor hook first, so Project Eris is back to normal immediately.
HOOK="etc/project_eris/FUNC/0095_classic_os_boot.sh"

# EmulationStation wrapper (created later by bootctl.sh, not by install.sh)
ES="${PE}/opt/emulationstation/launch.sh"
if [ -f "${ES}" ] && grep -q CLASSIC_OS_ES_WRAPPER "${ES}" 2>/dev/null && [ -f "${ES}.es_backup" ]; then
    if [ "${DRY}" = "1" ]; then say "would restore original ${ES}"
    else
        cp -f "${ES}.es_backup" "${ES}" && chmod +x "${ES}" 2>/dev/null && rm -f "${ES}.es_backup" && say "restored original EmulationStation launch.sh"
    fi
fi

FAIL=0
handle() {   # $1 kind, $2 path
    kind="$1"; rel="$2"; dst="${PE}/${rel}"
    case "${kind}" in
        NEW)
            [ -f "${dst}" ] || return 0
            want="$(grep "^CKSUM ${rel} " "${MANIFEST}" | tail -n 1 | cut -d' ' -f3-)"
            have="$(cksum < "${dst}" | tr ' ' ':')"
            if [ -n "${want}" ] && [ "${want}" != "${have}" ]; then
                say "KEEP ${rel} (modified since install)"; return 0
            fi
            if [ "${DRY}" = "1" ]; then say "would remove ${rel}"; else rm -f "${dst}" && say "removed ${rel}"; fi
            ;;
        REPLACED)
            if [ ! -f "${BK}/files/${rel}" ]; then say "[WARN] backup missing for ${rel}"; FAIL=1; return 0; fi
            if [ "${DRY}" = "1" ]; then say "would restore ${rel}"; else
                cp -p "${BK}/files/${rel}" "${dst}.cos_restore" && mv -f "${dst}.cos_restore" "${dst}" && say "restored ${rel}" || { say "[WARN] could not restore ${rel}"; FAIL=1; }
            fi
            ;;
    esac
}

# hook first, then everything else in manifest order (here-docs keep FAIL in this shell)
HOOK_LINES="$(grep -E "^(NEW|REPLACED) ${HOOK}\$" "${MANIFEST}")"
REST_LINES="$(grep -E "^(NEW|REPLACED) " "${MANIFEST}" | grep -v " ${HOOK}\$")"
while read -r kind rel; do [ -n "${kind}" ] && handle "${kind}" "${rel}"; done <<EOF
${HOOK_LINES}
EOF
while read -r kind rel; do [ -n "${kind}" ] && handle "${kind}" "${rel}"; done <<EOF
${REST_LINES}
EOF

# crash-guard state files and the backup pointer files are ours too
if [ "${DRY}" != "1" ]; then
    rm -f "${PE}/etc/project_eris/CFG/.classic_os_boot_attempt" "${PE}/etc/project_eris/CFG/.classic_os_crash_count" 2>/dev/null
fi

# directories created by the installer (deepest first), only if empty
DIRS="$(grep "^DIR " "${MANIFEST}" | sed 's/^DIR //' | awk '{a[NR]=$0} END{for(i=NR;i>=1;i--) print a[i]}')"
while read -r d; do
    [ -n "${d}" ] && [ "${DRY}" != "1" ] && rmdir "${PE}/${d}" 2>/dev/null
done <<EOF
${DIRS}
EOF

if [ "${DRY}" != "1" ]; then
    say "backup kept for reference: ${BK}"
fi
say "finished. Project Eris is back to its previous state (your classic_os_boot.cfg, if any, was left in place)."
exit "${FAIL}"
