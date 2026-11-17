#ifndef CLASSIC_OS_CONFIG_HPP
#define CLASSIC_OS_CONFIG_HPP

#include <string>
#include <unordered_map>

namespace classic_os {

struct RuntimeConfig {
    // Verified Project Eris paths from /var/volatile/project_eris.cfg
    std::string project_eris_path   = "/media/project_eris";
    std::string mountpoint          = "/media";
    std::string retroarch_path      = "/media/project_eris/opt/retroarch";
    std::string runtime_log_path    = "/media/logs";
    std::string themes_path         = "/media/project_eris/etc/project_eris/THEME";
    std::string selected_theme      = "modmyclassic";
    std::string vol_gaadata         = "/var/volatile/gaadatatmp";
    std::string databases_path      = "/media/project_eris/etc/project_eris/SYS/databases";
    std::string roms_path           = "/media/roms";
    std::string games_path          = "/media/games";
    std::string internal_gaadata    = "/gaadata";
    std::string es_systems_cfg      = "/media/project_eris/opt/emulationstation/.emulationstation/es_systems.cfg";
    std::string controller_db       = "/tmp/._bleemsync/etc/boot_menu/gamecontrollerdb.txt";
    std::string sdl_remap_lib       = "/media/project_eris/lib/sdl_remap_arm.so";
    std::string stock_pcsx_bin      = "/usr/sony/bin/pcsx";
    std::string ra_launch_script    = "/media/project_eris/etc/project_eris/SUP/scripts/ra_launch.sh";
    std::string classic_os_boot_cfg = "/media/project_eris/etc/project_eris/CFG/classic_os_boot.cfg";
    std::string bootctl_script      = "/media/project_eris/opt/classic_os/bootctl.sh";
    std::string launch_cmd_file     = "/tmp/launchfilecommand";
    std::string power_disable_file  = "/data/power/disable";
    std::string return_target       = "launch_bootmenu"; // "launch_bootmenu" or "launch_stockui"
    std::string video_driver;                            // "" = auto-detect (wayland -> kmsdrm -> SDL default)
    std::string ready_file          = "/tmp/classic_os_ready"; // touched after first presented frame

    bool launch_ra_from_stock_ui    = false;
    bool config_file_loaded         = false;
    bool primary_dashboard_enabled  = true;
    std::size_t max_vram_bytes      = 32u * 1024u * 1024u; // Strict 32 MB LRU cap (<100MB ES cap)

    std::unordered_map<std::string, std::string> raw_vars;
};

class ConfigLoader {
public:
    static RuntimeConfig Load(int argc, char* argv[]);
    static bool ParseShellConfigFile(const std::string& path, RuntimeConfig& cfg);
};

} // namespace classic_os

#endif // CLASSIC_OS_CONFIG_HPP
