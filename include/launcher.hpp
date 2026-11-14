#ifndef CLASSIC_OS_LAUNCHER_HPP
#define CLASSIC_OS_LAUNCHER_HPP

#include "config.hpp"
#include "library.hpp"
#include "renderer.hpp"
#include "texture_cache.hpp"
#include "input.hpp"
#include <string>

namespace classic_os {

enum class ExitTarget {
    BootMenu,          // Writes "launch_bootmenu" to /tmp/launchfilecommand
    StockUI,           // Writes "launch_stockui" to /tmp/launchfilecommand
    RetroArchStandalone// Writes "launch_retroarch_standalone" to /tmp/launchfilecommand
};

class Launcher {
public:
    explicit Launcher(const RuntimeConfig& cfg);

    // Sets /data/power/disable (2 = disable power button cutoff during active session, 1 = restore)
    bool SetPowerButtonLock(int state) const;

    // Writes the supervisor state-machine transition token to /tmp/launchfilecommand
    bool WriteLaunchFileCommand(ExitTarget target) const;
    bool WriteRawLaunchCommand(const std::string& token) const;

    // Reversibly enables or disables Classic OS as the Project Eris primary auto-boot dashboard
    // by updating /media/project_eris/etc/project_eris/CFG/classic_os_boot.cfg and invoking bootctl.sh
    bool SetPrimaryBootDashboard(bool enable);
    bool IsPrimaryBootDashboardEnabled() const { return primary_boot_enabled_; }

    // Tears down SDL2/VRAM, launches the selected PS1 or RetroArch game synchronously,
    // and re-initializes the Classic OS SDL2 renderer upon game exit.
    bool LaunchGameAndRestore(
        const GameEntry& game,
        Renderer& renderer,
        TextureCache& cache,
        InputManager& input);

private:
    int ExecuteStockPcsx(const GameEntry& game) const;
    int ExecuteRetroArch(const GameEntry& game) const;
    void LogLaunchEvent(const std::string& message) const;

    const RuntimeConfig& cfg_;
    bool primary_boot_enabled_ = true;
};

} // namespace classic_os

#endif // CLASSIC_OS_LAUNCHER_HPP
