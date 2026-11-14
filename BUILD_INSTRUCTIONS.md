# Classic OS — Build, Boot Architecture & USB Deployment Guide

This guide covers:
1. **Project Eris Supervisor & Primary Boot Architecture** (how Classic OS auto-boots like EmulationStation while remaining 100% reversible).
2. **Summary of the 4 Source Fixes** applied to the package.
3. **Host Desktop Testing (`x86_64`) vs. Actual PlayStation Classic ARM Compilation (`armhf`)**.
4. **VirtualBox Lightweight Linux VM Setup & Cross-Compilation** (no WSL2 required).
5. **USB Deployment, Reversible Dashboard Selection & Recovery Paths**.

---

## 1. Project Eris Supervisor Inspection & Primary Boot Architecture

### Why `/tmp/launchfilecommand` Alone Does Not Configure Cold-Boot Auto-Start
In our original package, only two scripts existed:
- `package/opt/classic_os/launch.sh`
- `package/etc/project_eris/SUP/launchers/classic_os/launch.sh`

Both scripts wrote `echo "launch_bootmenu" > /tmp/launchfilecommand` (or `launch_stockui`) before running `./classic_os`. However:
- `/tmp` on the PlayStation Classic is a volatile RAM disk (`tmpfs`) wiped on every power cycle.
- In Project Eris's supervisor script (`/media/project_eris/etc/project_eris/FUNC/0030_project_eris_funcs.sh`, [Project Eris Boot Sequence Overview](https://www.reddit.com/r/PlaystationClassic/comments/yjf0li/ps_classic_with_project_eris_just_boots_into/)), `/tmp/launchfilecommand` is the **post-exit handoff token** read by `execute_project_eris_func()` *after* a launched UI process returns. Writing to `/tmp/launchfilecommand` inside `launch.sh` or `src/app.cpp` only tells the Project Eris supervisor what to open *after* Classic OS exits—it never causes Project Eris to select Classic OS at cold boot.

### Exact 5-Stage Boot Sequence
Without modifying Sony NAND firmware, the bootloader, or overwriting core Project Eris files on disk, Classic OS integrates into Project Eris's native function-module pipeline:

1. **Stage 1 — Original PlayStation Classic Powers On**:
   Sony's stock ROM bootloader and kernel boot from internal NAND and trigger the Project Eris USB init hook (`lolhack` / payload supervisor) once the `SONY`-labeled USB drive is mounted at `/media`.
2. **Stage 2 — Existing Project Eris Boot Process Starts**:
   Project Eris initializes `/media/project_eris`, exports `/media/project_eris/etc/project_eris/CFG/project_eris_cfg.INI` to `/var/volatile/project_eris.cfg` ([Project Eris Config Overview](https://www.reddit.com/r/PlaystationClassic/comments/g6ch0k/playstation_classic_project_eris_boot_menu/)), and sources every script in `/media/project_eris/etc/project_eris/FUNC/*.sh` in lexical order (`0000_shared.sh`, `0010_Init.sh`, `0030_project_eris_funcs.sh`, ..., `0095_classic_os_boot.sh`) before executing the functions listed in `/media/project_eris/etc/project_eris/CFG/boot_sequence.cfg` (ending with `pretty_logo_func` and `execute_project_eris_func`).
3. **Stage 3 — Project Eris Supervisor Selects Classic OS as Primary Dashboard**:
   When `0095_classic_os_boot.sh` is sourced after `0030_project_eris_funcs.sh`, it copies the original `execute_project_eris_func` definition in RAM to `orig_pe_execute_project_eris_func` and wraps `execute_project_eris_func()`. When `boot_sequence.cfg` calls `execute_project_eris_func` at the end of Stage 2, the wrapper inspects:
   - Recovery override flags (`/media/CLASSIC_OS_RECOVERY`, `/media/ForceBootMenu.flag`, `/media/project_eris/etc/project_eris/CFG/disable_classic_os`).
   - `/media/project_eris/etc/project_eris/CFG/classic_os_boot.cfg` (`CLASSIC_OS_PRIMARY_DASHBOARD="1"`).
   - Executable verification (`[ -x /media/project_eris/opt/classic_os/classic_os ]`).
   In addition, `bootctl.sh --enable-es-hook` optionally installs a reversible EmulationStation launch wrapper at `/media/project_eris/opt/emulationstation/launch.sh` (backing up the original to `launch.sh.es_backup`) so Selecting *EmulationStation* in the Boot Menu or booting with `BOOT_TARGET=emulationstation` also routes cleanly into Classic OS when `CLASSIC_OS_HOOK_EMULATIONSTATION="1"`.
4. **Stage 4 — Classic OS Native ARM Executable Launches Automatically**:
   `0095_classic_os_boot.sh` executes `/media/project_eris/opt/classic_os/launch.sh --primary-boot`, which kills background stock UI audio (`killall -9 sonyapp ui_menu`), exports the SDL2/runtime environment, pre-seeds `/tmp/launchfilecommand` with `launch_bootmenu` (the default return target), and executes `/media/project_eris/opt/classic_os/classic_os`.
5. **Stage 5 — Exiting Classic OS Returns Control to Project Eris Boot Menu or Stock UI**:
   When the user selects an exit action in Classic OS's **Exit / Power** overlay (`SELECT` button):
   - **Exit to Project Eris / Boot Menu**: writes `launch_bootmenu` to `/tmp/launchfilecommand` and exits `classic_os` cleanly (`rc = 0`).
   - **Exit to Stock PlayStation UI**: writes `launch_stockui` to `/tmp/launchfilecommand` and exits `classic_os` cleanly (`rc = 0`).
   - **Power Off Console**: writes `poweroff` to `/tmp/launchfilecommand` and runs `sync && poweroff`.
   Control returns immediately to `orig_pe_execute_project_eris_func`, which reads `/tmp/launchfilecommand` and opens the Project Eris Boot Menu or Stock UI carousel. If `classic_os` ever crashes or exits with a non-zero code (`rc != 0`), `0095_classic_os_boot.sh` forces `echo "launch_bootmenu" > /tmp/launchfilecommand` so the console never enters a crash loop.

### Manual Launch Methods Preserved
Even when `CLASSIC_OS_PRIMARY_DASHBOARD="0"` (console boots to the standard Project Eris Boot Menu or Stock UI), Classic OS can be launched manually at any time via:
1. **Stock UI Carousel Launcher**: `/media/project_eris/etc/project_eris/SUP/launchers/classic_os/launch.sh` (appears as the **Classic OS** tile on the Stock UI carousel, [Project Eris SUP Launchers](https://www.reddit.com/r/PlaystationClassic/comments/gcflbo/project_eris_help_please/)).
2. **Project Eris Boot Menu Item**: `/media/project_eris/etc/boot_menu/boot_items/004_classic_os.json` (for extensible Boot Menu setups, [PSClassic Boot Menu JSON Format](https://github.com/donluca/PSClassic-Debian)) or via the EmulationStation tile when `CLASSIC_OS_HOOK_EMULATIONSTATION="1"`.
3. **Direct Shell Execution**: `/media/project_eris/opt/classic_os/launch.sh` over SSH/serial.

---

## 2. Summary of the Four Source Fixes Applied

1. **CMake Cross-Compiler Initialization Order (`CMakeLists.txt` & `cmake/psc-armhf-toolchain.cmake`)**:
   - Moved `option(TARGET_PSC_ARMHF ... ON)`, `CMAKE_SYSTEM_NAME Linux`, `CMAKE_SYSTEM_PROCESSOR arm`, and `find_program(PSC_ARMHF_CXX arm-linux-gnueabihf-g++)` **before** `project(classic_os VERSION 1.0.0 LANGUAGES CXX)`.
   - Added a post-`project()` `check_cxx_symbol_exists(__arm__ "" COMPILER_IS_ARM_TARGET)` check so a stale host `CMakeCache.txt` can never masquerade as an ARM build.
2. **Removed Silent Host Compiler Fallback (`Makefile` & `CMakeLists.txt`)**:
   - `Makefile` defaults to `TARGET_ARCH ?= armhf` and terminates immediately with `$(error ...)` if `arm-linux-gnueabihf-g++` is not in `PATH`.
   - `make package` verifies `readelf -h build/classic_os | grep -q "Machine:.*ARM"` and refuses to package host binaries.
3. **Read-Only SQLite Game Database Connections (`src/library.cpp` & `include/sdl_sqlite_abi.hpp`)**:
   - Replaced every `sqlite3_open(...)` call across `ScanRegionalDb`, `ScanBleemSyncDb`, and `ScanStockInternalPlaylist` with `sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr)`.
4. **POSIX-Compatible Shell Scripts (`package/opt/classic_os/*.sh` & `package/etc/project_eris/**/*.sh`)**:
   - Replaced all bashisms (`source`, `&>`) with POSIX `. "/var/volatile/project_eris.cfg"` and `> "${RUNTIME_LOG_PATH}/classic_os.log" 2>&1`. All scripts validate cleanly with `dash -n` / `sh -n`.

---

## 3. Host Desktop Testing vs. Actual ARM Compilation

| Feature | Host Desktop Testing (`x86_64`) | PlayStation Classic Build (`armhf`) |
| :--- | :--- | :--- |
| **Purpose** | Test UI layout, controller navigation, and SQLite DB scanning on your Linux PC/VM | Deploy and run natively on the PlayStation Classic (SCPH-1000R) |
| **Compiler** | Host `g++` (`x86_64-linux-gnu`) | `arm-linux-gnueabihf-g++` (32-bit ARMv8-A hard-float) |
| **Make Command** | `make host` *(or `make TARGET_ARCH=host`)* | `make` or `make package` |
| **CMake Command** | `cmake -B build-host -DTARGET_PSC_ARMHF=OFF && cmake --build build-host` | `cmake -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/psc-armhf-toolchain.cmake -DTARGET_PSC_ARMHF=ON && cmake --build build-arm` |
| **Output Path** | `build-host/classic_os` | `build/classic_os` -> `package/opt/classic_os/classic_os` |

---

## 4. VirtualBox Lightweight Linux VM Cross-Compilation Workflow (No WSL2)

### Recommended VM Configuration
- **Hypervisor**: [Oracle VirtualBox](https://www.virtualbox.org/) on Windows, macOS, or Linux.
- **Guest OS**: [Debian 10 (Buster) 64-bit netinst](https://www.debian.org/releases/buster/) (matches the PlayStation Classic's glibc 2.28 baseline) or [Debian 11 (Bullseye) 64-bit netinst](https://www.debian.org/releases/bullseye/).
- **Resources**: 2 vCPUs, 2048 MB RAM, 12 GB dynamically allocated VDI disk.

### Step 1 — Provision the ARMhf Cross-Toolchain & Libraries in the VM
Inside your VirtualBox Linux VM terminal, unzip `classic_os.zip` and run:
```sh
unzip classic_os.zip
cd classic_os
chmod +x scripts/vbox_setup_armhf.sh scripts/build_armhf.sh
./scripts/vbox_setup_armhf.sh
```

### Step 2 — Build & Package the ARM Binary
Run the automated build & verification script:
```sh
./scripts/build_armhf.sh make
# Or using CMake:
./scripts/build_armhf.sh cmake
```
This compiles `classic_os` with `-march=armv8-a+crc -mtune=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard`, strips symbols, copies `classic_os` into `package/opt/classic_os/classic_os`, verifies `Machine: ARM` via `readelf`, and runs `sh -n` across all boot and launcher scripts.

---

## 5. USB Installation, Reversible Dashboard Selection & Recovery Paths

> **Important Safety Note**: Nothing is installed onto the PlayStation Classic's internal NAND flash. All files reside exclusively on your removable Project Eris `SONY` USB drive.

### Installing onto Your Project Eris USB Drive
1. Power off your PlayStation Classic and insert your Project Eris USB drive (labeled `SONY`) into your computer.
2. Copy the contents of `package/` onto the `project_eris` folder at the root of the USB drive (`<USB_ROOT>/project_eris/`):
   ```sh
   # Example assuming your SONY USB drive is mounted at /mnt/usb:
   cp -rf package/opt/* /mnt/usb/project_eris/opt/
   cp -rf package/etc/* /mnt/usb/project_eris/etc/
   sync
   ```
   This installs:
   - `/media/project_eris/opt/classic_os/classic_os` (native ARM executable)
   - `/media/project_eris/opt/classic_os/launch.sh` (primary/manual launcher script)
   - `/media/project_eris/opt/classic_os/bootctl.sh` (reversible boot mode utility)
   - `/media/project_eris/opt/classic_os/assets/...` (dashboard icons & fonts)
   - `/media/project_eris/etc/project_eris/CFG/classic_os_boot.cfg` (persistent primary boot configuration)
   - `/media/project_eris/etc/project_eris/FUNC/0095_classic_os_boot.sh` (supervisor auto-boot hook)
   - `/media/project_eris/etc/project_eris/SUP/launchers/classic_os/*` (Stock UI carousel launcher)
   - `/media/project_eris/etc/boot_menu/boot_items/004_classic_os.json` (Boot Menu launcher descriptor)

### Switching Between Primary Auto-Boot and Standard Project Eris Boot Menu
You have **four independent ways** to switch back to the original Project Eris Boot Menu at any time:

1. **In-App Controller Toggle (Inside Classic OS)**:
   - Press `SELECT` (or navigate to **Exit / Power**), highlight **`4. Primary Auto-Boot Mode: [ENABLED — Classic OS]`**, and press `X` (Cross). It immediately updates `classic_os_boot.cfg` to `[DISABLED — Project Eris Boot Menu]`.
   - Select **`1. Exit to Project Eris / Boot Menu`** to return immediately to the Project Eris Boot Menu.
2. **USB Config File Edit (On Any PC/Mac/Phone)**:
   - Open `<USB_ROOT>/project_eris/etc/project_eris/CFG/classic_os_boot.cfg` in any text editor and change:
     ```ini
     CLASSIC_OS_PRIMARY_DASHBOARD="0"
     ```
3. **Zero-Config USB Recovery Flag File**:
   - Create an empty file named **`CLASSIC_OS_RECOVERY`** (or **`ForceBootMenu.flag`**) at the root of your `SONY` USB drive (`<USB_ROOT>/CLASSIC_OS_RECOVERY`). Whenever this file is present, `0095_classic_os_boot.sh` bypasses Classic OS auto-boot and goes straight to the Project Eris Boot Menu.
4. **Command-Line Utility (`bootctl.sh`)**:
   - Run `/media/project_eris/opt/classic_os/bootctl.sh --restore-bootmenu` (or `./package/opt/classic_os/bootctl.sh --restore-bootmenu` on your mounted USB drive) to disable primary auto-boot and restore any wrapped `emulationstation/launch.sh.es_backup`, or `--enable-primary` to re-enable it.
