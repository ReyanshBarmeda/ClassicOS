#ifndef CLASSIC_OS_LIBRARY_HPP
#define CLASSIC_OS_LIBRARY_HPP

#include "config.hpp"
#include <string>
#include <vector>

namespace classic_os {

enum class LaunchBackend {
    StockPcsx = 0,
    RetroArchCore = 1,
    CustomScript = 2
};

struct GameEntry {
    int id                     = 0;
    std::string title;
    std::string publisher;
    std::string developer;
    int release_year           = 1998;
    int players                = 1;
    std::string platform_id    = "psx";      // e.g., "psx", "snes", "genesis", "gba", "arcade"
    std::string platform_name  = "Sony PlayStation";
    std::string disc_basename;
    std::string launch_path;                 // Verified .cue / .pbp / .chd / .m3u / ROM path
    std::string cover_path;                  // Verified .png cover path
    std::string core_so        = "pcsx_rearmed_libretro.so";
    LaunchBackend backend      = LaunchBackend::StockPcsx;
    bool is_internal_stock     = false;      // True if located in /gaadata/1..20
    bool file_verified         = false;
};

struct PlatformCategory {
    std::string id;
    std::string full_name;
    std::string rom_dir;
    std::string default_core;
    std::vector<std::string> extensions;
    int game_count = 0;
};

class GameLibrary {
public:
    GameLibrary() = default;

    // Read-only scan of Project Eris SQLite databases (regional.db, BleemSync.db),
    // internal stock playlists (psc_row.lpl / psc_jpn.lpl), and es_systems.cfg ROMs
    bool ScanAll(const RuntimeConfig& cfg);

    const std::vector<GameEntry>& games() const { return games_; }
    const std::vector<PlatformCategory>& platforms() const { return platforms_; }
    std::vector<const GameEntry*> FilterByPlatform(const std::string& platform_id) const;
    std::vector<const GameEntry*> SearchByTitle(const std::string& query) const;

    int psx_count() const { return psx_count_; }
    int rom_count() const { return rom_count_; }
    bool regional_db_found() const { return regional_db_found_; }
    bool bleemsync_db_found() const { return bleemsync_db_found_; }

private:
    bool ScanRegionalDb(const RuntimeConfig& cfg);
    bool ScanBleemSyncDb(const RuntimeConfig& cfg);
    void ScanStockInternalPlaylist(const RuntimeConfig& cfg);
    void ScanEmulationStationSystems(const RuntimeConfig& cfg);
    void ResolvePsxGamePaths(const RuntimeConfig& cfg, GameEntry& entry) const;
    void RebuildPlatformCounts();

    std::vector<GameEntry> games_;
    std::vector<PlatformCategory> platforms_;
    int psx_count_          = 0;
    int rom_count_          = 0;
    bool regional_db_found_ = false;
    bool bleemsync_db_found_= false;
};

} // namespace classic_os

#endif // CLASSIC_OS_LIBRARY_HPP
