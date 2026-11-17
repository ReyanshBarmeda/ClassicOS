# Classic OS — Boot Readiness (PlayStation Classic + Project Eris)

> **Status: NOT verified on real PlayStation Classic hardware.** Everything below was checked
> on a development machine and under an emulator. A successful boot on the console has not
> been observed by anyone, and nothing here should be read as saying it has.

## 1. Verification matrix

| Category | What was checked | Result |
|---|---|---|
| **1. Local** | ELF is 32-bit ARM EABI5, hard-float, ARMv8; interpreter `/lib/ld-linux-armhf.so.3`; highest glibc symbol version 2.17 (baseline 2.28); DT_NEEDED list; `sh -n` on every script; no CR characters in shipped scripts/JSON; package layout and exec bits; boot supervisor, `launch.sh`, `bootctl.sh`, installer and uninstaller against a fake Project Eris drive | Pass (`sh tests/run_tests.sh`: 94 checks, 0 failures, including the qemu category below) |
| **2. Emulator** | The real ARM binary under `qemu-arm` with a self-built SDL2 2.30 (`dummy` video driver, software renderer): startup, first-frame handshake, PNG icon rendering, controller navigation (raw pad and SDL game-controller events) to each exit target, failure paths | Pass. Screenshots looked correct |
| **3. Real hardware** | Boot from Project Eris, display, controller, return to Eris, crash recovery | **PENDING — not tested** |

Limits of Category 1/2 (read these before trusting the result):
- Events are injected at the SDL level. The kernel evdev → SDL controller-mapping path is **not** exercised.
- The emulator uses an Ubuntu glibc 2.39 loader and SDL 2.30, not the console's glibc 2.28 and Sony's own libraries.
- The shipped test binary was built with zig/clang targeting glibc 2.28, not with the repo's normal GCC/Debian 10 CI toolchain. The CI workflow builds the "official" binary; the workflow changes in this branch could not be run from the authoring environment.
- The Debian 10 CI build compiles against SDL 2.0.9 headers. Every SDL function the code uses exists in 2.0.9, but that compile has not been run here; the first CI run is its test.
- Minimum-uptime detection has about 1 second granularity (`launch.sh` polls once per second).

## 2. What changed (by requirement)

| Requirement | Change |
|---|---|
| Duplicate `--return-target` | `launch.sh` parses and removes any caller-supplied `--return-target` (also `--return-target=X`) and passes exactly one to the binary. The Stock UI launcher is now a thin delegate and no longer builds its own command line |
| Unenforced minimum uptime | `0095_classic_os_boot.sh` treats an exit sooner than `CLASSIC_OS_MIN_UPTIME_SEC` (any exit code) as a failed boot |
| Crash-loop recovery | Persistent crash counter and attempt marker on the USB. After `CLASSIC_OS_MAX_CRASHES` consecutive failures auto-boot is switched off (`bootctl.sh`) and Project Eris regains control. A power cut before the first frame counts as a failed attempt. A first-frame handshake (`--ready-file`) and a hang watchdog (`CLASSIC_OS_READY_TIMEOUT_SEC`) catch hangs |
| Project Eris launcher integration | Boot hook, Stock UI launcher and Boot Menu entry all go through the single `opt/classic_os/launch.sh`. Every failure path writes `launch_bootmenu` to `/tmp/launchfilecommand` |
| glibc 2.28 / armhf dependencies | `check_runtime.sh` runs before every start (ELF header, loader, every shared library, every symbol version) and refuses to run the app if something is missing, leaving Project Eris in control. `scripts/verify_armhf.sh` enforces the glibc ceiling in CI |
| SDL2 init / display backend | No hard-coded Wayland. Video backend chain: configured driver, `SDL_VIDEODRIVER`, wayland (only if a compositor socket exists), kmsdrm, SDL default. Headless backends (`dummy`, `offscreen`) are rejected unless explicitly requested. Video and joystick are initialised separately, so a missing pad does not stop the display. Every attempt is logged to `classic_os_diagnostics.log` |
| Controller mapping / return to Eris | Existing mapping kept; exit targets verified under emulation (section 1) |
| Recovery to the original Eris menu | `CLASSIC_OS_RECOVERY` file in the USB root, `bootctl.sh --restore-bootmenu`, the circuit breaker, and `uninstall.sh` |
| Safe defaults | The shipped `classic_os_boot.cfg` has auto-boot **off** (`PRIMARY_DASHBOARD="0"`) and does not wrap EmulationStation (`HOOK_EMULATIONSTATION="0"`) |
| Windows line endings | `.gitattributes` forces LF; the installer strips CR from scripts; `verify_armhf.sh` fails if any shipped script has CR |
| Install / rollback | `installer/install.sh` and `installer/uninstall.sh` (backup, manifest, no recursive deletes) |

## 3. Dependency report (measured on the test binary)

- DT_NEEDED: `libSDL2-2.0.so.0`, `libpng16.so.16`, `libsqlite3.so.0`, `libm.so.6`, `libpthread.so.0`, `libdl.so.2`, `libc.so.6`, `ld-linux-armhf.so.3`
- RUNPATH: `/usr/sony/lib:/media/project_eris/lib`
- Highest GLIBC symbol version used: **2.17** (versions seen: 2.4, 2.7, 2.16, 2.17). Console baseline: 2.28
- libPNG: needs symbol version `PNG16_0` (simplified API: `png_image_begin_read_from_file`, `png_image_finish_read`, `png_image_free`)
- SDL2 functions beyond the basics: `SDL_GetNumVideoDrivers`, `SDL_GetVideoDriver`, `SDL_GameControllerAddMappingsFromRW`, `SDL_GameControllerFromInstanceID`, `SDL_RenderSetLogicalSize`
- SQLite: `sqlite3_open_v2` (read-only)
- `libSDL2_ttf` and `libSDL2_image` are loaded with `dlopen` and are optional; a built-in bitmap font is the fallback
- libstdc++/libc++ is linked statically

**Unknown without access to the console:** the exact SDL2, libpng and sqlite versions in `/usr/sony/lib`, whether libz is present, and whether Sony's SDL2 was built with a Wayland, kmsdrm or other video backend. `check_runtime.sh` reports exactly what is missing at the moment of boot.

## 4. Hardware compatibility assumptions (to confirm on the console)

1. Project Eris sources `etc/project_eris/FUNC/*.sh` at boot and defines `execute_project_eris_func` in `0030_project_eris_funcs.sh`. The boot hook extracts and wraps that function by name. **If your Project Eris version names or lays it out differently, the hook logs "hook not applied" to `classic_os_boot.log` and Classic OS can still be started from the Eris menu, but auto-boot will not work.** This is the single largest unverified assumption.
2. `/tmp/launchfilecommand` accepts `launch_bootmenu`, `launch_stockui`, `launch_retroarch_standalone`.
3. The USB stays mounted at `/media`, and allows execution of files on it (or `chmod +x` works). If the exec bit cannot be set, `launch.sh` falls back to running the binary through the ELF loader.
4. The console is 32-bit ARM userland (ARMv8 core, hard-float).

## 5. First boot on the console (recommended order)

1. Install with `install.sh` (auto-boot stays off).
2. Boot into Project Eris and start Classic OS from the Boot Menu / Stock UI entry. Press the exit item to return.
3. Only after that works, enable auto-boot: `sh /media/project_eris/opt/classic_os/bootctl.sh --enable-primary` (or the toggle in the Exit menu).
4. If anything goes wrong: put an empty `CLASSIC_OS_RECOVERY` file in the USB root, or run `uninstall.sh`.

## 6. If it still does not boot — send these

From the USB (`logs/` folder next to `project_eris/`), after one failed attempt:
- `logs/classic_os_boot.log`
- `logs/classic_os_runtime_check.log`
- `logs/classic_os.log` and `logs/classic_os.log.prev`
- `logs/classic_os_diagnostics.log`
- `project_eris/etc/project_eris/FUNC/0030_project_eris_funcs.sh`
- `project_eris/etc/project_eris/CFG/boot_sequence.cfg` (if present)
- A one-line description of what the screen shows (black, Project Eris menu, stock UI, hang)

## 7. Build and test

```sh
make package            # needs arm-linux-gnueabihf-g++ (CI uses a Debian 10 container)
sh scripts/verify_armhf.sh
make test               # Category 1: shell/installer tests, runs on any POSIX host
```
Category 2 (qemu) is optional: `COS_QEMU=1 COS_ARM_LIBS=<dir> COS_SHIM=<qemu_shim.so> sh tests/run_tests.sh` (see the header of `tests/run_tests.sh`; the shim source is `tests/qemu_shim.c`, test-only, never installed).

The CI workflow (`.github/workflows/build-armhf.yml`) lints scripts, runs the Category 1 tests, builds in Debian 10, verifies the ELF, and uploads `ClassicOS-ARMHF-<sha>.zip` (installer, payload, docs) with a SHA-256 file.
