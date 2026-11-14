#include "config.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace classic_os {

namespace {

std::string Trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string StripQuotes(const std::string& s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

std::string ExpandVars(const std::string& input, const std::unordered_map<std::string, std::string>& vars) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '$' && i + 1 < input.size()) {
            if (input[i + 1] == '{') {
                const auto close = input.find('}', i + 2);
                if (close != std::string::npos) {
                    const std::string key = input.substr(i + 2, close - (i + 2));
                    auto it = vars.find(key);
                    if (it != vars.end()) {
                        out += it->second;
                    } else if (const char* env_v = std::getenv(key.c_str())) {
                        out += env_v;
                    }
                    i = close;
                    continue;
                }
            }
        }
        out.push_back(input[i]);
    }
    return out;
}

} // namespace

bool ConfigLoader::ParseShellConfigFile(const std::string& path, RuntimeConfig& cfg) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        if (trimmed.compare(0, 7, "export ") == 0) {
            trimmed = Trim(trimmed.substr(7));
        }
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = Trim(trimmed.substr(0, eq));
        std::string val = StripQuotes(Trim(trimmed.substr(eq + 1)));
        val = ExpandVars(val, cfg.raw_vars);
        cfg.raw_vars[key] = val;
    }

    auto apply_if = [&](const char* key, std::string& target) {
        auto it = cfg.raw_vars.find(key);
        if (it != cfg.raw_vars.end() && !it->second.empty()) {
            target = it->second;
        }
    };

    apply_if("PROJECT_ERIS_PATH", cfg.project_eris_path);
    apply_if("MOUNTPOINT", cfg.mountpoint);
    apply_if("RETROARCH_PATH", cfg.retroarch_path);
    apply_if("RUNTIME_LOG_PATH", cfg.runtime_log_path);
    apply_if("THEMES_PATH", cfg.themes_path);
    apply_if("SELECTED_THEME", cfg.selected_theme);
    apply_if("VOL_GAADATA", cfg.vol_gaadata);
    apply_if("CLASSIC_OS_DEFAULT_EXIT_TARGET", cfg.return_target);

    auto ra_it = cfg.raw_vars.find("LAUNCH_RA_FROM_STOCK_UI");
    if (ra_it != cfg.raw_vars.end()) {
        cfg.launch_ra_from_stock_ui = (ra_it->second == "1" || ra_it->second == "true");
    }

    auto prim_it = cfg.raw_vars.find("CLASSIC_OS_PRIMARY_DASHBOARD");
    if (prim_it != cfg.raw_vars.end()) {
        cfg.primary_dashboard_enabled = (prim_it->second == "1" || prim_it->second == "true");
    }

    cfg.databases_path      = cfg.project_eris_path + "/etc/project_eris/SYS/databases";
    cfg.roms_path           = cfg.mountpoint + "/roms";
    cfg.games_path          = cfg.mountpoint + "/games";
    cfg.es_systems_cfg      = cfg.project_eris_path + "/opt/emulationstation/.emulationstation/es_systems.cfg";
    cfg.sdl_remap_lib       = cfg.project_eris_path + "/lib/sdl_remap_arm.so";
    cfg.ra_launch_script    = cfg.project_eris_path + "/etc/project_eris/SUP/scripts/ra_launch.sh";
    cfg.classic_os_boot_cfg = cfg.project_eris_path + "/etc/project_eris/CFG/classic_os_boot.cfg";
    cfg.bootctl_script      = cfg.project_eris_path + "/opt/classic_os/bootctl.sh";
    cfg.config_file_loaded  = true;
    return true;
}

RuntimeConfig ConfigLoader::Load(int argc, char* argv[]) {
    RuntimeConfig cfg;
    cfg.raw_vars["PROJECT_ERIS_PATH"] = cfg.project_eris_path;
    cfg.raw_vars["MOUNTPOINT"]        = cfg.mountpoint;
    cfg.raw_vars["RETROARCH_PATH"]    = cfg.retroarch_path;
    cfg.raw_vars["RUNTIME_LOG_PATH"]  = cfg.runtime_log_path;
    cfg.raw_vars["THEMES_PATH"]       = cfg.themes_path;
    cfg.raw_vars["SELECTED_THEME"]    = cfg.selected_theme;
    cfg.raw_vars["VOL_GAADATA"]       = cfg.vol_gaadata;

    // 1. Load Project Eris runtime config from /var/volatile/project_eris.cfg if present
    ParseShellConfigFile("/var/volatile/project_eris.cfg", cfg);

    // 2. Load Classic OS boot config from /media/project_eris/etc/project_eris/CFG/classic_os_boot.cfg if present
    ParseShellConfigFile(cfg.classic_os_boot_cfg, cfg);

    // 3. Override from CLI flags (--project-eris-path, --database-folder, --theme-folder, --return-target)
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project-eris-path" && i + 1 < argc) {
            cfg.project_eris_path   = argv[++i];
            cfg.databases_path      = cfg.project_eris_path + "/etc/project_eris/SYS/databases";
            cfg.themes_path         = cfg.project_eris_path + "/etc/project_eris/THEME";
            cfg.es_systems_cfg      = cfg.project_eris_path + "/opt/emulationstation/.emulationstation/es_systems.cfg";
            cfg.sdl_remap_lib       = cfg.project_eris_path + "/lib/sdl_remap_arm.so";
            cfg.ra_launch_script    = cfg.project_eris_path + "/etc/project_eris/SUP/scripts/ra_launch.sh";
            cfg.classic_os_boot_cfg = cfg.project_eris_path + "/etc/project_eris/CFG/classic_os_boot.cfg";
            cfg.bootctl_script      = cfg.project_eris_path + "/opt/classic_os/bootctl.sh";
            ParseShellConfigFile(cfg.classic_os_boot_cfg, cfg);
        } else if (arg == "--database-folder" && i + 1 < argc) {
            cfg.databases_path = argv[++i];
        } else if (arg == "--games-folder" && i + 1 < argc) {
            cfg.games_path = argv[++i];
        } else if (arg == "--roms-folder" && i + 1 < argc) {
            cfg.roms_path = argv[++i];
        } else if (arg == "--log-folder" && i + 1 < argc) {
            cfg.runtime_log_path = argv[++i];
        } else if (arg == "--return-target" && i + 1 < argc) {
            cfg.return_target = argv[++i];
        } else if (arg == "--use-retroarch") {
            cfg.launch_ra_from_stock_ui = true;
        }
    }

    return cfg;
}

} // namespace classic_os
