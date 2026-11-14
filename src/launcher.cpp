#include "launcher.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace classic_os {

Launcher::Launcher(const RuntimeConfig& cfg)
    : cfg_(cfg), primary_boot_enabled_(cfg.primary_dashboard_enabled) {}

bool Launcher::SetPowerButtonLock(int state) const {
    // Verified in SUP/scripts/intercept & gamemanager_launch/launch.sh:
    // echo 2 > /data/power/disable (locks power button during active session)
    // echo 1 > /data/power/disable (restores normal power button handling)
    std::ofstream out(cfg_.power_disable_file, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << state;
    return out.good();
}

bool Launcher::WriteLaunchFileCommand(ExitTarget target) const {
    switch (target) {
        case ExitTarget::BootMenu:
            // Verified in opt/emulationstation/launch.sh: echo "launch_bootmenu" > "/tmp/launchfilecommand"
            return WriteRawLaunchCommand("launch_bootmenu");
        case ExitTarget::StockUI:
            // Verified in SUP/launchers/gamemanager_launch/launch.sh: echo "launch_stockui" > "/tmp/launchfilecommand"
            return WriteRawLaunchCommand("launch_stockui");
        case ExitTarget::RetroArchStandalone:
            // Verified in SUP/launchers/retroarch_launch/launch.sh: echo "launch_retroarch_standalone" > "/tmp/launchfilecommand"
            return WriteRawLaunchCommand("launch_retroarch_standalone");
    }
    return false;
}

bool Launcher::WriteRawLaunchCommand(const std::string& token) const {
    std::ofstream out(cfg_.launch_cmd_file, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << token << "\n";
    out.flush();
    LogLaunchEvent("Wrote supervisor command '" + token + "' to " + cfg_.launch_cmd_file);
    return out.good();
}

bool Launcher::SetPrimaryBootDashboard(bool enable) {
    primary_boot_enabled_ = enable;

    // 1. If bootctl.sh is present on USB, invoke it to update classic_os_boot.cfg and ES wrapper atomically
    struct stat st{};
    if (stat(cfg_.bootctl_script.c_str(), &st) == 0) {
        const pid_t pid = fork();
        if (pid == 0) {
            const char* flag = enable ? "--enable-primary" : "--restore-bootmenu";
            std::vector<char*> args;
            args.push_back(const_cast<char*>("/bin/sh"));
            args.push_back(const_cast<char*>(cfg_.bootctl_script.c_str()));
            args.push_back(const_cast<char*>(flag));
            args.push_back(nullptr);
            execv("/bin/sh", args.data());
            _exit(127);
        } else if (pid > 0) {
            int wstatus = 0;
            waitpid(pid, &wstatus, 0);
            if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
                LogLaunchEvent(std::string("Updated primary boot dashboard via bootctl.sh: ") +
                               (enable ? "ENABLED" : "DISABLED (Restored Project Eris Boot Menu)"));
                return true;
            }
        }
    }

    // 2. Direct fallback write to classic_os_boot.cfg
    std::ofstream out(cfg_.classic_os_boot_cfg, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        LogLaunchEvent("Could not open " + cfg_.classic_os_boot_cfg + " for writing (USB not mounted)");
        return false;
    }
    out << "# Classic OS — Project Eris Primary Boot & Supervisor Configuration\n"
        << "CLASSIC_OS_PRIMARY_DASHBOARD=\"" << (enable ? "1" : "0") << "\"\n"
        << "CLASSIC_OS_HOOK_EMULATIONSTATION=\"" << (enable ? "1" : "0") << "\"\n"
        << "CLASSIC_OS_DEFAULT_EXIT_TARGET=\"" << cfg_.return_target << "\"\n"
        << "CLASSIC_OS_MIN_UPTIME_SEC=\"2\"\n";
    out.flush();
    LogLaunchEvent(std::string("Wrote CLASSIC_OS_PRIMARY_DASHBOARD=") + (enable ? "1" : "0") +
                   " to " + cfg_.classic_os_boot_cfg);
    return out.good();
}

bool Launcher::LaunchGameAndRestore(
    const GameEntry& game,
    Renderer& renderer,
    TextureCache& cache,
    InputManager& input) {

    if (!game.file_verified) {
        LogLaunchEvent("Skipped launch for '" + game.title + "': target file not mounted (" + game.launch_path + ")");
        return false;
    }

    LogLaunchEvent("Preparing to launch '" + game.title + "' (" + game.launch_path + ")");

    // 1. Release all SDL2 textures, audio/input handles, renderer, and Wayland window
    //    so the emulator receives 100% of VRAM and exclusive display access
    cache.ClearAll();
    input.Shutdown();
    renderer.Shutdown();

    // 2. Lock console power button (echo 2 > /data/power/disable) matching SUP/scripts/intercept
    SetPowerButtonLock(2);

    // 3. Execute Stock PCSX (with sdl_remap_arm.so) or RetroArch (via ra_launch.sh)
    int status = -1;
    if (game.backend == LaunchBackend::StockPcsx) {
        status = ExecuteStockPcsx(game);
    } else {
        status = ExecuteRetroArch(game);
    }

    // 4. Restore power state and re-initialize SDL2 dashboard
    SetPowerButtonLock(1);
    renderer.Init(cfg_);
    cache.InitFonts(cfg_);
    input.Init(cfg_);

    LogLaunchEvent("Returned from '" + game.title + "' with status " + std::to_string(status));
    return (status == 0);
}

int Launcher::ExecuteStockPcsx(const GameEntry& game) const {
    struct stat st{};
    if (stat(cfg_.stock_pcsx_bin.c_str(), &st) != 0) {
        LogLaunchEvent("Stock PCSX binary not found at " + cfg_.stock_pcsx_bin);
        return -1;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        // Verified in SUP/scripts/intercept line 232:
        // LD_PRELOAD="${PROJECT_ERIS_PATH}/lib/sdl_remap_arm.so" "/usr/sony/bin/pcsx" ... -cdfile "${INTERCEPT_GAME_PATH}"
        setenv("LD_PRELOAD", cfg_.sdl_remap_lib.c_str(), 1);
        std::vector<char*> args;
        args.push_back(const_cast<char*>(cfg_.stock_pcsx_bin.c_str()));
        args.push_back(const_cast<char*>("-cdfile"));
        args.push_back(const_cast<char*>(game.launch_path.c_str()));
        args.push_back(nullptr);
        execv(cfg_.stock_pcsx_bin.c_str(), args.data());
        _exit(127);
    }

    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    return WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
}

int Launcher::ExecuteRetroArch(const GameEntry& game) const {
    struct stat st{};
    if (stat(cfg_.ra_launch_script.c_str(), &st) != 0) {
        LogLaunchEvent("RetroArch launch script not found at " + cfg_.ra_launch_script);
        return -1;
    }

    const std::string core_full_path = cfg_.retroarch_path + "/config/retroarch/cores/" + game.core_so;
    const pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        // Verified in opt/emulationstation/bin/es_launch_ra:
        // "${PROJECT_ERIS_PATH}/etc/project_eris/SUP/scripts/ra_launch.sh" -L "$core" -v "$game"
        std::vector<char*> args;
        args.push_back(const_cast<char*>("/bin/sh"));
        args.push_back(const_cast<char*>(cfg_.ra_launch_script.c_str()));
        args.push_back(const_cast<char*>("-L"));
        args.push_back(const_cast<char*>(core_full_path.c_str()));
        args.push_back(const_cast<char*>("-v"));
        args.push_back(const_cast<char*>(game.launch_path.c_str()));
        args.push_back(nullptr);
        execv("/bin/sh", args.data());
        _exit(127);
    }

    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    return WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : -1;
}

void Launcher::LogLaunchEvent(const std::string& message) const {
    const std::string log_path = cfg_.runtime_log_path + "/classic_os_launcher.log";
    std::ofstream out(log_path, std::ios::out | std::ios::app);
    if (out.is_open()) {
        out << "[CLASSIC OS](LAUNCHER) " << message << "\n";
    }
}

} // namespace classic_os
