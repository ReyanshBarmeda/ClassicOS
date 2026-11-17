#!/bin/sh
#===============================================================================
# Classic OS — test suite for the boot scripts, installer/rollback and (optionally) the
# real ARM binary under qemu-user.
#
#   sh tests/run_tests.sh                 # Category 1: shell/logic tests with a fake binary (any POSIX host)
#   COS_QEMU=1 COS_ARM_LIBS=<dir> COS_SHIM=<qemu_shim.so> sh tests/run_tests.sh
#                                         # + Category 2: real armhf binary under qemu-arm
#
# Environment for the qemu part:
#   COS_QEMU_ROOT   glibc sysroot for qemu-arm -L           (default /usr/arm-linux-gnueabihf)
#   COS_ARM_LIBS    dir holding armhf libSDL2-2.0.so.0, libpng16.so.16, libsqlite3.so.0 (+libz.so.1)
#   COS_SHIM        tests/qemu_shim.c built for armhf (event injection / frame dump)
#   COS_SHOTS       output dir for screenshots (default: a temp dir)
#
# NOTHING in here runs on, or proves anything about, real PlayStation Classic hardware.
#===============================================================================
unset CDPATH
HERE="$(cd -- "$(dirname -- "$0")" && pwd)"
ROOT="$(cd -- "${HERE}/.." && pwd)"
PKG="${ROOT}/package"
T="$(mktemp -d "${TMPDIR:-/tmp}/cos_tests.XXXXXX")" || exit 2
PASS=0; FAIL=0
trap 'rm -rf "${T}"' EXIT

ok()  { PASS=$((PASS + 1)); echo "  ok   $*"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL $*"; }
expect() { # expect <name> <got> <want>
    if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2', want '$3')"; fi
}
expect_file()    { if [ -f "$2" ]; then ok "$1"; else bad "$1 (missing $2)"; fi; }
expect_no_file() { if [ ! -f "$2" ]; then ok "$1"; else bad "$1 (unexpected $2)"; fi; }

#--- fixture: a fake Project Eris USB drive with a fake (shell-script) classic_os ----------------
new_usb() { # $1 = name
    U="${T}/$1"; PE="${U}/project_eris"; LOGS="${U}/logs"
    rm -rf "${U}"
    mkdir -p "${PE}/etc/project_eris/FUNC" "${PE}/etc/project_eris/CFG" "${PE}/opt/classic_os" "${PE}/opt/emulationstation" "${LOGS}"
    cp "${PKG}"/opt/classic_os/*.sh "${PE}/opt/classic_os/"
    cp "${PKG}/etc/project_eris/FUNC/0095_classic_os_boot.sh" "${PE}/etc/project_eris/FUNC/"
    cp "${PKG}/etc/project_eris/CFG/classic_os_boot.cfg" "${PE}/etc/project_eris/CFG/"
    cat > "${PE}/etc/project_eris/FUNC/0030_project_eris_funcs.sh" <<'EOF'
#!/bin/sh
execute_project_eris_func() {
    echo "ERIS_MENU token=$(cat "${CLASSIC_OS_TMP}/launchfilecommand" 2>/dev/null)" >> "${RUNTIME_LOG_PATH}/fake_eris.log"
}
EOF
    cat > "${PE}/opt/classic_os/classic_os" <<'EOF'
#!/bin/sh
# fake binary; behaviour from FAKE_MODE, argv recorded
echo "$*" > "${FAKE_ARGS}"
echo run >> "${FAKE_RUNS}"
while [ $# -gt 0 ]; do [ "$1" = "--ready-file" ] && READY="$2"; shift; done
case "${FAKE_MODE}" in
    ok)     echo ok > "${READY}"; sleep 2; exit 0 ;;
    crash)  echo ok > "${READY}"; sleep 1; exit 1 ;;
    quick0) exit 0 ;;
    quick1) exit 1 ;;
    hang)   exec sleep 30 ;;
    segv)   echo ok > "${READY}"; sleep 1; kill -SEGV $$ ;;
esac
EOF
    chmod +x "${PE}/opt/classic_os/classic_os"
    export FAKE_ARGS="${U}/args" FAKE_RUNS="${U}/runs"
    : > "${FAKE_RUNS}"
}
set_cfg() { # key value  (append override; later assignment wins when sourced)
    echo "$1=\"$2\"" >> "${PE}/etc/project_eris/CFG/classic_os_boot.cfg"
}
runs() { wc -l < "${FAKE_RUNS}" | tr -d ' '; }

# One power cycle: fresh /tmp, same USB. Prints nothing; read ${LOGS}/fake_eris.log afterwards.
boot() {
    CYCLE=$((${CYCLE:-0} + 1))
    mkdir -p "${U}/tmp.${CYCLE}"
    ( export PROJECT_ERIS_PATH="${PE}" MOUNTPOINT="${U}" RUNTIME_LOG_PATH="${LOGS}" \
             CLASSIC_OS_TMP="${U}/tmp.${CYCLE}" CLASSIC_OS_ERIS_CFG=/nonexistent CLASSIC_OS_SKIP_PREFLIGHT=1
      . "${PE}/etc/project_eris/FUNC/0095_classic_os_boot.sh"
      execute_project_eris_func ) >/dev/null 2>&1
    LAST_TMP="${U}/tmp.${CYCLE}"
}
launch() { # launch.sh with fake binary: launch [args...]
    CYCLE=$((${CYCLE:-0} + 1)); mkdir -p "${U}/tmp.${CYCLE}"; LAST_TMP="${U}/tmp.${CYCLE}"
    ( export PROJECT_ERIS_PATH="${PE}" RUNTIME_LOG_PATH="${LOGS}" CLASSIC_OS_TMP="${LAST_TMP}" \
             CLASSIC_OS_ERIS_CFG=/nonexistent CLASSIC_OS_SKIP_PREFLIGHT=1
      sh "${PE}/opt/classic_os/launch.sh" "$@" ) >/dev/null 2>&1
    LAUNCH_RC=$?
}
token() { cat "${LAST_TMP}/launchfilecommand" 2>/dev/null; }
count_arg() { # occurrences of $1 in recorded argv
    tr ' ' '\n' < "${FAKE_ARGS}" | grep -c -- "^$1\$"
}

echo "=== Category 1a: launch.sh argument handling and exit codes (fake binary)"
new_usb a; export FAKE_MODE=ok
launch --primary-boot --return-target launch_bootmenu
expect "clean exit rc" "${LAUNCH_RC}" 0
expect "exactly one --return-target reaches the binary" "$(count_arg --return-target)" 1
expect "--primary-boot is not forwarded" "$(count_arg --primary-boot)" 0
expect "token kept as requested" "$(token)" launch_bootmenu
launch --return-target launch_stockui
expect "stock-ui token kept on clean exit" "$(token)" launch_stockui
expect "one --return-target (stockui)" "$(count_arg --return-target)" 1
launch --return-target=launch_stockui extra-arg
expect "--return-target=VALUE form: one flag" "$(count_arg --return-target)" 1
expect "other args still forwarded" "$(count_arg extra-arg)" 1
set_cfg CLASSIC_OS_DEFAULT_EXIT_TARGET launch_stockui
launch
expect "config default used when no flag" "$(token)" launch_stockui
launch --return-target bogus
expect "unsupported target falls back to boot menu" "$(token)" launch_bootmenu

export FAKE_MODE=crash; launch --return-target launch_stockui
expect "crash rc propagated" "${LAUNCH_RC}" 1
expect "crash forces boot-menu token" "$(token)" launch_bootmenu
export FAKE_MODE=segv; launch --return-target launch_stockui
expect "segfault rc 139" "${LAUNCH_RC}" 139
expect "segfault forces boot-menu token" "$(token)" launch_bootmenu
new_usb b; set_cfg CLASSIC_OS_READY_TIMEOUT_SEC 2; export FAKE_MODE=hang
START=$(date +%s); launch --return-target launch_stockui; END=$(date +%s)
expect "watchdog rc 124" "${LAUNCH_RC}" 124
expect "watchdog fired within 8s" "$([ $((END - START)) -le 8 ] && echo yes || echo no)" yes
expect "watchdog forces boot-menu token" "$(token)" launch_bootmenu
new_usb c; rm -f "${PE}/opt/classic_os/classic_os"; launch --return-target launch_stockui
expect "missing binary rc" "${LAUNCH_RC}" 126
expect "missing binary -> boot-menu token" "$(token)" launch_bootmenu

echo "=== Category 1b: supervisor (0095): hook, min-uptime, crash loop, recovery"
new_usb d; set_cfg CLASSIC_OS_PRIMARY_DASHBOARD 1; set_cfg CLASSIC_OS_MIN_UPTIME_SEC 1; export FAKE_MODE=ok
boot
expect "clean session ran Classic OS once" "$(runs)" 1
expect "original Eris function ran afterwards" "$(grep -c ERIS_MENU "${LOGS}/fake_eris.log")" 1
expect_no_file "attempt marker cleared after first frame" "${PE}/etc/project_eris/CFG/.classic_os_boot_attempt"
expect_no_file "no crash counted" "${PE}/etc/project_eris/CFG/.classic_os_crash_count"
expect "supervisor passed one --return-target" "$(count_arg --return-target)" 1

new_usb e; set_cfg CLASSIC_OS_PRIMARY_DASHBOARD 1; set_cfg CLASSIC_OS_MIN_UPTIME_SEC 2; export FAKE_MODE=quick0
boot
expect "min-uptime: rc=0 exit within the guard window counts as a crash" "$(cat "${PE}/etc/project_eris/CFG/.classic_os_crash_count" 2>/dev/null)" 1
expect "min-uptime: Eris menu gets control" "$(grep -c 'ERIS_MENU token=launch_bootmenu' "${LOGS}/fake_eris.log")" 1
expect "min-uptime logged" "$(grep -c 'treating as a crash' "${LOGS}/classic_os_boot.log")" 1

new_usb f; set_cfg CLASSIC_OS_PRIMARY_DASHBOARD 1; set_cfg CLASSIC_OS_MIN_UPTIME_SEC 1; export FAKE_MODE=crash
boot; boot
expect "2 failures: still trying auto-boot" "$(runs)" 2
boot
expect "3rd failure ran" "$(runs)" 3
expect "breaker tripped: auto-boot disabled in cfg" "$(sh -c ". '${PE}/etc/project_eris/CFG/classic_os_boot.cfg'; echo \$CLASSIC_OS_PRIMARY_DASHBOARD")" 0
boot
expect "after trip the binary is no longer started" "$(runs)" 3
expect "after trip every boot still reaches the Eris menu" "$(grep -c ERIS_MENU "${LOGS}/fake_eris.log")" 4

new_usb g; set_cfg CLASSIC_OS_PRIMARY_DASHBOARD 1; set_cfg CLASSIC_OS_MIN_UPTIME_SEC 1; export FAKE_MODE=ok
echo 2 > "${PE}/etc/project_eris/CFG/.classic_os_crash_count"; echo attempt > "${PE}/etc/project_eris/CFG/.classic_os_boot_attempt"
boot
expect "leftover attempt marker (hang/power cut) trips the breaker at limit" "$(runs)" 0
expect "...and boots the Eris menu" "$(grep -c 'ERIS_MENU' "${LOGS}/fake_eris.log")" 1
new_usb h; set_cfg CLASSIC_OS_PRIMARY_DASHBOARD 1; set_cfg CLASSIC_OS_MIN_UPTIME_SEC 1; export FAKE_MODE=ok
echo 2 > "${PE}/etc/project_eris/CFG/.classic_os_crash_count"
boot
expect_no_file "a good session resets the crash counter" "${PE}/etc/project_eris/CFG/.classic_os_crash_count"

new_usb i; set_cfg CLASSIC_OS_PRIMARY_DASHBOARD 1; export FAKE_MODE=ok; : > "${U}/CLASSIC_OS_RECOVERY"
boot
expect "recovery flag: Classic OS not started" "$(runs)" 0
expect_no_file "recovery flag consumed" "${U}/CLASSIC_OS_RECOVERY"
expect "recovery flag: auto-boot disabled persistently" "$(sh -c ". '${PE}/etc/project_eris/CFG/classic_os_boot.cfg'; echo \$CLASSIC_OS_PRIMARY_DASHBOARD")" 0
expect "recovery flag: Eris menu runs" "$(grep -c ERIS_MENU "${LOGS}/fake_eris.log")" 1

new_usb j; set_cfg CLASSIC_OS_PRIMARY_DASHBOARD 1; rm -f "${PE}/opt/classic_os/classic_os"
boot
expect "missing binary: Eris menu runs" "$(grep -c ERIS_MENU "${LOGS}/fake_eris.log")" 1

new_usb k; set_cfg CLASSIC_OS_PRIMARY_DASHBOARD 0; export FAKE_MODE=ok
boot
expect "primary disabled: Classic OS not auto-started" "$(runs)" 0
expect "primary disabled: Eris menu runs" "$(grep -c ERIS_MENU "${LOGS}/fake_eris.log")" 1

new_usb l; set_cfg CLASSIC_OS_MIN_UPTIME_SEC 1; export FAKE_MODE=ok; set_cfg CLASSIC_OS_PRIMARY_DASHBOARD 0
# "launch_classic_os" token from a menu: pre-seed then boot
CYCLE=$((${CYCLE:-0} + 1)); mkdir -p "${U}/tmp.${CYCLE}"; echo launch_classic_os > "${U}/tmp.${CYCLE}/launchfilecommand"; echo 1 > "${U}/tmp.${CYCLE}/classic_os_boot_completed"
( export PROJECT_ERIS_PATH="${PE}" MOUNTPOINT="${U}" RUNTIME_LOG_PATH="${LOGS}" CLASSIC_OS_TMP="${U}/tmp.${CYCLE}" CLASSIC_OS_ERIS_CFG=/nonexistent CLASSIC_OS_SKIP_PREFLIGHT=1
  . "${PE}/etc/project_eris/FUNC/0095_classic_os_boot.sh"; execute_project_eris_func ) >/dev/null 2>&1
expect "launch_classic_os token starts one session" "$(runs)" 1
expect "...then hands over to the Eris function" "$(grep -c ERIS_MENU "${LOGS}/fake_eris.log")" 1

new_usb m; printf '#!/bin/sh\n# an Eris version without that function\nsomething_else() { :; }\n' > "${PE}/etc/project_eris/FUNC/0030_project_eris_funcs.sh"
set_cfg CLASSIC_OS_PRIMARY_DASHBOARD 1
HOOKED="$( ( export PROJECT_ERIS_PATH="${PE}" MOUNTPOINT="${U}" RUNTIME_LOG_PATH="${LOGS}" CLASSIC_OS_TMP="${U}" ; . "${PE}/etc/project_eris/FUNC/0095_classic_os_boot.sh"; command -v execute_project_eris_func || echo none ) 2>/dev/null )"
expect "unknown Eris layout: no hook, Eris untouched" "${HOOKED}" none
expect "unknown Eris layout is logged" "$(grep -c 'hook NOT applied' "${LOGS}/classic_os_boot.log")" 1

echo "=== Category 1c: bootctl.sh"
new_usb n
BC() { PROJECT_ERIS_PATH="${PE}" MOUNTPOINT="${U}" CLASSIC_OS_TMP="${U}" CLASSIC_OS_ERIS_CFG=/nonexistent sh "${PE}/opt/classic_os/bootctl.sh" "$@" >/dev/null 2>&1; }
cfgval() { sh -c ". '${PE}/etc/project_eris/CFG/classic_os_boot.cfg'; eval echo \"\$$1\""; }
set_cfg CLASSIC_OS_MAX_CRASHES 5; set_cfg CLASSIC_OS_VIDEO_DRIVER kmsdrm
BC --enable-primary
expect "enable-primary" "$(cfgval CLASSIC_OS_PRIMARY_DASHBOARD)" 1
expect "enable-primary keeps user's MAX_CRASHES" "$(cfgval CLASSIC_OS_MAX_CRASHES)" 5
expect "enable-primary keeps VIDEO_DRIVER" "$(cfgval CLASSIC_OS_VIDEO_DRIVER)" kmsdrm
BC --restore-bootmenu
expect "restore-bootmenu" "$(cfgval CLASSIC_OS_PRIMARY_DASHBOARD)" 0
printf '#!/bin/sh\necho original-ES\n' > "${PE}/opt/emulationstation/launch.sh"; cp "${PE}/opt/emulationstation/launch.sh" "${U}/es.orig"
BC --enable-es-hook
expect "ES hook installed" "$(grep -c CLASSIC_OS_ES_WRAPPER "${PE}/opt/emulationstation/launch.sh")" 1
cmp -s "${U}/es.orig" "${PE}/opt/emulationstation/launch.sh.es_backup" && ok "ES original backed up byte-for-byte" || bad "ES backup differs"
BC --enable-es-hook
cmp -s "${U}/es.orig" "${PE}/opt/emulationstation/launch.sh.es_backup" && ok "second enable does not overwrite the backup with the wrapper" || bad "backup overwritten by wrapper"
BC --restore-es-hook
cmp -s "${U}/es.orig" "${PE}/opt/emulationstation/launch.sh" && ok "ES original restored byte-for-byte" || bad "ES restore differs"
printf '#!/bin/sh\necho eris-updated-ES\n' > "${PE}/opt/emulationstation/launch.sh"
BC --restore-es-hook
expect "restore never clobbers a launch.sh we did not wrap" "$(sed -n 2p "${PE}/opt/emulationstation/launch.sh")" "echo eris-updated-ES"

echo "=== Category 1d: installer + rollback on a fake USB"
mkinst() { # build a temp release layout with a fake ARM ELF as the binary
    R="${T}/release"; rm -rf "${R}"; mkdir -p "${R}"
    cp "${ROOT}/installer/install.sh" "${ROOT}/installer/uninstall.sh" "${R}/"
    mkdir -p "${R}/payload"; cp -r "${PKG}/opt" "${PKG}/etc" "${R}/payload/"
    if [ ! -f "${R}/payload/opt/classic_os/classic_os" ] || [ "$(dd if="${R}/payload/opt/classic_os/classic_os" bs=1 count=4 2>/dev/null | od -An -tx1 | tr -d ' ')" != "7f454c46" ]; then
        # no real ARM build available: forge a minimal ARM ELF header (installer only checks the header)
        printf '\177ELF\001\001\001\000\000\000\000\000\000\000\000\000\002\000\050\000' > "${R}/payload/opt/classic_os/classic_os"
        dd if=/dev/zero bs=1 count=64 >> "${R}/payload/opt/classic_os/classic_os" 2>/dev/null
    fi
}
mkinst
new_usb p; U2="${U}"; PE2="${PE}"
mkdir -p "${PE2}/etc/project_eris/SUP/launchers/classic_os"
printf '#!/bin/sh\n# OLD stock launcher\n' > "${PE2}/etc/project_eris/SUP/launchers/classic_os/launch.sh"
printf '#!/bin/sh\n# OLD 0095\n' > "${PE2}/etc/project_eris/FUNC/0095_classic_os_boot.sh"
rm -f "${PE2}/opt/classic_os/"*; rmdir "${PE2}/opt/classic_os"
printf 'user cfg\nCLASSIC_OS_MAX_CRASHES="9"\n' > "${PE2}/etc/project_eris/CFG/classic_os_boot.cfg"
mkdir -p "${U2}/games" "${U2}/roms"; echo precious > "${U2}/games/save.bin"; echo precious > "${U2}/roms/rom.bin"; echo x > "${U2}/OTHER_FILE.txt"
( cd "${U2}" && find . -type f -not -path './project_eris/*' -not -path './logs/*' | sort | xargs cksum ) > "${T}/outside_before.txt"
( cd "${PE2}" && find . -type f | sort | xargs cksum ) > "${T}/pe_before.txt"
sh "${R}/install.sh" "${U2}" --dry-run >/dev/null 2>&1
( cd "${PE2}" && find . -type f | sort | xargs cksum ) > "${T}/pe_after_dry.txt"
cmp -s "${T}/pe_before.txt" "${T}/pe_after_dry.txt" && ok "--dry-run changes nothing" || bad "--dry-run modified the drive"
sh "${R}/install.sh" "${U2}" >/dev/null 2>&1
expect "install exit code" "$?" 0
expect_file "classic_os installed" "${PE2}/opt/classic_os/classic_os"
expect "existing classic_os_boot.cfg kept" "$(sed -n 1p "${PE2}/etc/project_eris/CFG/classic_os_boot.cfg")" "user cfg"
expect "no auto-boot by default (no --primary)" "$(grep -c 'CLASSIC_OS_PRIMARY_DASHBOARD="1"' "${PE2}/etc/project_eris/CFG/classic_os_boot.cfg")" 0
BKID="$(cat "${PE2}/.classic_os_backup/LATEST")"
grep -q "^# OLD 0095" "${PE2}/.classic_os_backup/${BKID}/files/etc/project_eris/FUNC/0095_classic_os_boot.sh" && ok "old 0095 backed up" || bad "old 0095 not backed up"
grep -q "^# OLD stock launcher" "${PE2}/.classic_os_backup/${BKID}/files/etc/project_eris/SUP/launchers/classic_os/launch.sh" && ok "old stock launcher backed up" || bad "old launcher not backed up"
expect "new 0095 installed" "$(grep -c 'Safety contract' "${PE2}/etc/project_eris/FUNC/0095_classic_os_boot.sh")" 1
echo "edited by user" >> "${PE2}/opt/classic_os/bootctl.sh"
sh "${R}/uninstall.sh" "${U2}" --dry-run >/dev/null 2>&1
expect_file "uninstall --dry-run keeps files" "${PE2}/opt/classic_os/launch.sh"
sh "${R}/uninstall.sh" "${U2}" >/dev/null 2>&1
expect "uninstall exit code" "$?" 0
expect "old 0095 restored" "$(sed -n 2p "${PE2}/etc/project_eris/FUNC/0095_classic_os_boot.sh")" "# OLD 0095"
expect "old stock launcher restored" "$(sed -n 2p "${PE2}/etc/project_eris/SUP/launchers/classic_os/launch.sh")" "# OLD stock launcher"
expect_no_file "installed launch.sh removed" "${PE2}/opt/classic_os/launch.sh"
expect_file "user-edited file is kept, not deleted" "${PE2}/opt/classic_os/bootctl.sh"
expect "user cfg untouched by rollback" "$(sed -n 1p "${PE2}/etc/project_eris/CFG/classic_os_boot.cfg")" "user cfg"
( cd "${U2}" && find . -type f -not -path './project_eris/*' -not -path './logs/*' | sort | xargs cksum ) > "${T}/outside_after.txt"
cmp -s "${T}/outside_before.txt" "${T}/outside_after.txt" && ok "nothing outside project_eris/ was touched (games, roms, other files)" || bad "files outside project_eris changed"

# refusals
new_usb q; rm -rf "${PE}/etc/project_eris"
sh "${R}/install.sh" "${U}" >/dev/null 2>&1; expect "refuses a drive without Project Eris" "$?" 1
new_usb r
cp /bin/ls "${R}/payload/opt/classic_os/classic_os.host"; mv -f "${R}/payload/opt/classic_os/classic_os" "${T}/real_bin"; mv "${R}/payload/opt/classic_os/classic_os.host" "${R}/payload/opt/classic_os/classic_os"
sh "${R}/install.sh" "${U}" >/dev/null 2>&1; expect "refuses a non-ARM (host) binary" "$?" 1
mv -f "${T}/real_bin" "${R}/payload/opt/classic_os/classic_os"
echo "if then" >> "${R}/payload/opt/classic_os/launch.sh"
new_usb s; sh "${R}/install.sh" "${U}" >/dev/null 2>&1; expect "refuses a script with a syntax error" "$?" 1
[ ! -d "${PE}/.classic_os_backup" ] && ok "refusal happens before anything is written (no backup dir)" || bad "refused install left a backup dir"
mkinst; sed -i 's/$/\r/' "${R}/payload/etc/project_eris/FUNC/0095_classic_os_boot.sh" "${R}/payload/opt/classic_os/launch.sh"
new_usb t; sh "${R}/install.sh" "${U}" >/dev/null 2>&1
CRFILES="$(grep -l "$(printf '\r')" "${PE}/etc/project_eris/FUNC/0095_classic_os_boot.sh" "${PE}/opt/classic_os/launch.sh" 2>/dev/null | wc -l | tr -d ' ')"
expect "installer strips CRLF from scripts (Windows checkout): files still containing CR" "${CRFILES}" 0

#--- Category 2: real armhf binary under qemu-user ------------------------------------------------
if [ "${COS_QEMU:-0}" = "1" ]; then
    echo "=== Category 2: real armhf classic_os under qemu-arm (NOT real hardware)"
    QR="${COS_QEMU_ROOT:-/usr/arm-linux-gnueabihf}"; LIBS="${COS_ARM_LIBS:?set COS_ARM_LIBS}"; SHIM="${COS_SHIM:-}"
    SHOTS="${COS_SHOTS:-${T}/shots}"; mkdir -p "${SHOTS}"
    if [ ! -f "${PKG}/opt/classic_os/classic_os" ]; then bad "no ${PKG}/opt/classic_os/classic_os (run the build first)"; else
    new_usb q2; cp "${PKG}/opt/classic_os/classic_os" "${PE}/opt/classic_os/classic_os"; cp -r "${PKG}/opt/classic_os/assets" "${PE}/opt/classic_os/assets"
    set_cfg CLASSIC_OS_VIDEO_DRIVER dummy
    qlaunch() { # qlaunch "<events>" <seed-target> [extra env assignments...]
        EV="$1"; SEED="$2"; shift 2
        CYCLE=$((${CYCLE:-0} + 1)); LAST_TMP="/tmp"; rm -f /tmp/launchfilecommand /tmp/classic_os_ready "${LOGS}/inject.log"
        ( cd "${PE}/opt/classic_os"
          env PROJECT_ERIS_PATH="${PE}" RUNTIME_LOG_PATH="${LOGS}" MOUNTPOINT="${U}" CLASSIC_OS_ERIS_CFG=/nonexistent \
              CLASSIC_OS_RUN_PREFIX="qemu-arm -L ${QR}" CLASSIC_OS_LOADER="${QR}/lib/ld-linux-armhf.so.3" LD_LIBRARY_PATH="${LIBS}" \
              QEMU_SET_ENV="LD_PRELOAD=${SHIM}" COS_TEST_EVENTS="${EV}" COS_TEST_LOG="${LOGS}/inject.log" "$@" \
              sh ./launch.sh --primary-boot --return-target "${SEED}" ) >/dev/null 2>&1
        LAUNCH_RC=$?; QTOKEN="$(cat /tmp/launchfilecommand 2>/dev/null)"
    }
    PROJECT_ERIS_PATH="${PE}" RUNTIME_LOG_PATH="${LOGS}" CLASSIC_OS_RUN_PREFIX="qemu-arm -L ${QR}" CLASSIC_OS_LOADER="${QR}/lib/ld-linux-armhf.so.3" \
        LD_LIBRARY_PATH="${LIBS}" sh "${PE}/opt/classic_os/check_runtime.sh" --quiet; expect "check_runtime passes with the test libraries" "$?" 0
    PROJECT_ERIS_PATH="${PE}" RUNTIME_LOG_PATH="${LOGS}" CLASSIC_OS_RUN_PREFIX="qemu-arm -L ${QR}" CLASSIC_OS_LOADER="${QR}/lib/ld-linux-armhf.so.3" \
        LD_LIBRARY_PATH="${T}/emptylibs" sh "${PE}/opt/classic_os/check_runtime.sh" --quiet; expect "check_runtime FAILS when libraries are missing" "$?" 1
    qlaunch "" launch_bootmenu CLASSIC_OS_MAX_FRAMES=25
    expect "app starts, renders a frame, exits cleanly (rc)" "${LAUNCH_RC}" 0
    expect "first-frame handshake logged" "$(grep -c 'first frame presented' "${LOGS}/classic_os_boot.log")" 1
    expect "video backend used" "$(sed -n 's/^SDL Video Driver  : //p' "${LOGS}/classic_os_diagnostics.log")" dummy
    expect "software renderer created" "$(sed -n 's/^SDL Renderer Name : //p' "${LOGS}/classic_os_diagnostics.log")" software
    qlaunch "" launch_bootmenu CLASSIC_OS_MAX_FRAMES=30 COS_TEST_DUMP="${SHOTS}/home.bmp" COS_TEST_DUMP_FRAME=25
    expect_file "frame dump written (home view)" "${SHOTS}/home.bmp"
    qlaunch "J9" launch_bootmenu CLASSIC_OS_MAX_FRAMES=30 COS_TEST_DUMP="${SHOTS}/exit_menu.bmp" COS_TEST_DUMP_FRAME=25
    expect_file "frame dump written (exit menu after Start)" "${SHOTS}/exit_menu.bmp"
    qlaunch "J9 J2" launch_stockui;              expect "raw pad  Start,Cross            -> Boot Menu token" "${QTOKEN}" launch_bootmenu
    qlaunch "J9 H4 H0 J2" launch_bootmenu;       expect "raw pad  Start,Down(hat),Cross  -> Stock UI token" "${QTOKEN}" launch_stockui
    qlaunch "J9 A1+ A10 J2" launch_bootmenu;     expect "raw pad  Start,Stick down,Cross -> Stock UI token" "${QTOKEN}" launch_stockui
    qlaunch "C6 C0" launch_stockui;              expect "SDL ctrl Start,A                -> Boot Menu token" "${QTOKEN}" launch_bootmenu
    qlaunch "J9 H4 H0 H4 H0 J2" launch_bootmenu;    expect "raw pad  Start,Down,Down,Cross  -> RetroArch token" "${QTOKEN}" launch_retroarch_standalone
    # failure paths with the REAL binary
    qlaunch "" launch_stockui CLASSIC_OS_VIDEODRIVER=nosuchdriver CLASSIC_OS_MAX_FRAMES=5
    expect "no usable video backend: rc != 0" "$([ "${LAUNCH_RC}" -ne 0 ] && echo yes || echo no)" yes
    expect "no usable video backend: Boot Menu token" "${QTOKEN}" launch_bootmenu
    mv "${PE}/opt/classic_os/assets" "${U}/assets.off"
    qlaunch "" launch_bootmenu CLASSIC_OS_MAX_FRAMES=15; expect "missing assets/fonts: still starts (built-in font fallback)" "${LAUNCH_RC}" 0
    mv "${U}/assets.off" "${PE}/opt/classic_os/assets"
    qlaunch "" launch_bootmenu LD_LIBRARY_PATH="${T}/emptylibs" CLASSIC_OS_MAX_FRAMES=5
    expect "missing libraries: pre-flight blocks the start (rc 127)" "${LAUNCH_RC}" 127
    expect "missing libraries: Boot Menu token" "${QTOKEN}" launch_bootmenu
    fi
else
    echo "=== Category 2 skipped (set COS_QEMU=1 COS_ARM_LIBS=... COS_SHIM=...)"
fi

echo
echo "RESULT: ${PASS} passed, ${FAIL} failed"
[ "${FAIL}" -eq 0 ]
