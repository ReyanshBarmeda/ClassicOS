#include "library.hpp"
#include "sdl_sqlite_abi.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>

namespace classic_os {

namespace {

bool PathExists(const std::string& path) {
    struct stat st{};
    return (stat(path.c_str(), &st) == 0);
}

bool IsRegularFile(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISREG(st.st_mode);
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

std::string ExtractXmlTagValue(const std::string& block, const std::string& tag) {
    const std::string open_tag  = "<" + tag + ">";
    const std::string close_tag = "</" + tag + ">";
    const auto p1 = block.find(open_tag);
    if (p1 == std::string::npos) return "";
    const auto start = p1 + open_tag.size();
    const auto p2 = block.find(close_tag, start);
    if (p2 == std::string::npos) return "";
    std::string val = block.substr(start, p2 - start);
    const auto first = val.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = val.find_last_not_of(" \t\r\n");
    return val.substr(first, last - first + 1);
}

} // namespace

bool GameLibrary::ScanAll(const RuntimeConfig& cfg) {
    games_.clear();
    platforms_.clear();
    psx_count_ = 0;
    rom_count_ = 0;

    // 1. Seed default PlayStation category first
    PlatformCategory psx_cat{};
    psx_cat.id           = "psx";
    psx_cat.full_name    = "Sony PlayStation";
    psx_cat.rom_dir      = cfg.games_path;
    psx_cat.default_core = "pcsx_rearmed_libretro.so";
    psx_cat.extensions   = {".cue", ".pbp", ".chd", ".m3u", ".bin", ".img", ".iso"};
    platforms_.push_back(psx_cat);

    // 2. Query Project Eris regional.db (MENU_ENTRIES & DISC tables verified from psc_game_add)
    regional_db_found_ = ScanRegionalDb(cfg);

    // 3. Query BleemSync.db if present to enrich developer & cover metadata
    bleemsync_db_found_ = ScanBleemSyncDb(cfg);

    // 4. If regional.db had no rows (e.g. fresh USB before first boot sync), load the 20 stock internal titles
    //    from Project Eris's verified psc_row.lpl playlist so the dashboard always displays the console's library
    if (games_.empty()) {
        ScanStockInternalPlaylist(cfg);
    }

    // 5. Parse es_systems.cfg and scan /media/roms/<system> for multi-system RetroArch games
    ScanEmulationStationSystems(cfg);

    RebuildPlatformCounts();
    return !games_.empty();
}

bool GameLibrary::ScanRegionalDb(const RuntimeConfig& cfg) {
    const std::string db_path = cfg.databases_path + "/regional.db";
    if (!IsRegularFile(db_path)) {
        return false;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK || !db) {
        if (db) sqlite3_close(db);
        return false;
    }

    // Verified schema from SUP/scripts/migrate_pre10_db.sql and psc_game_add:
    // MENU_ENTRIES(GAME_ID, GAME_TITLE_STRING, PUBLISHER_NAME, RELEASE_YEAR, PLAYERS, ..., POSITION)
    // DISC(DISC_ID, GAME_ID, DISC_NUMBER, BASENAME)
    const char* sql_with_pos =
        "SELECT m.GAME_ID, m.GAME_TITLE_STRING, m.PUBLISHER_NAME, m.RELEASE_YEAR, m.PLAYERS, d.BASENAME "
        "FROM MENU_ENTRIES m "
        "LEFT JOIN DISC d ON m.GAME_ID = d.GAME_ID AND d.DISC_NUMBER = 1 "
        "ORDER BY COALESCE(m.POSITION, 0), m.GAME_TITLE_STRING;";

    const char* sql_legacy =
        "SELECT g.GAME_ID, g.GAME_TITLE_STRING, g.PUBLISHER_NAME, g.RELEASE_YEAR, g.PLAYERS, d.BASENAME "
        "FROM GAME g "
        "LEFT JOIN DISC d ON g.GAME_ID = d.GAME_ID AND d.DISC_NUMBER = 1 "
        "ORDER BY g.GAME_TITLE_STRING;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql_with_pos, -1, &stmt, nullptr) != SQLITE_OK) {
        if (sqlite3_prepare_v2(db, sql_legacy, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_close(db);
            return false;
        }
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GameEntry entry{};
        entry.id            = sqlite3_column_int(stmt, 0);
        const unsigned char* t = sqlite3_column_text(stmt, 1);
        const unsigned char* p = sqlite3_column_text(stmt, 2);
        entry.release_year  = sqlite3_column_int(stmt, 3);
        entry.players       = sqlite3_column_int(stmt, 4);
        const unsigned char* b = sqlite3_column_text(stmt, 5);

        entry.title         = t ? reinterpret_cast<const char*>(t) : "Untitled PS1 Game";
        entry.publisher     = p ? reinterpret_cast<const char*>(p) : "Sony Interactive Entertainment";
        entry.disc_basename = b ? reinterpret_cast<const char*>(b) : "";
        entry.platform_id   = "psx";
        entry.platform_name = "Sony PlayStation";
        entry.core_so       = "pcsx_rearmed_libretro.so";
        entry.backend       = cfg.launch_ra_from_stock_ui ? LaunchBackend::RetroArchCore : LaunchBackend::StockPcsx;

        ResolvePsxGamePaths(cfg, entry);
        games_.push_back(entry);
        ++psx_count_;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return !games_.empty();
}

bool GameLibrary::ScanBleemSyncDb(const RuntimeConfig& cfg) {
    const std::string db_path = cfg.databases_path + "/BleemSync.db";
    if (!IsRegularFile(db_path)) {
        return false;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK || !db) {
        if (db) sqlite3_close(db);
        return false;
    }

    // Verified schema from psc_game_add lines 1152-1153:
    // GameManagerNodes(Id, Name, SortName, Description, ReleaseDate, Players, Developer, Publisher, Type, SystemId, ParentId, Position)
    // GameManagerFiles(Id, Name, Path, NodeId)
    std::unordered_map<std::string, std::string> title_to_dev;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT Name, Developer FROM GameManagerNodes WHERE Name IS NOT NULL;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* n = sqlite3_column_text(stmt, 0);
            const unsigned char* d = sqlite3_column_text(stmt, 1);
            if (n && d) {
                title_to_dev[reinterpret_cast<const char*>(n)] = reinterpret_cast<const char*>(d);
            }
        }
        sqlite3_finalize(stmt);
    }

    for (auto& g : games_) {
        auto it = title_to_dev.find(g.title);
        if (it != title_to_dev.end() && !it->second.empty()) {
            g.developer = it->second;
        }
    }

    sqlite3_close(db);
    return true;
}

void GameLibrary::ResolvePsxGamePaths(const RuntimeConfig& cfg, GameEntry& entry) const {
    const std::string id_str = std::to_string(entry.id);

    // Search order matches Project Eris symlink & mount hierarchy:
    // 1. /var/volatile/gaadatatmp/<id>
    // 2. /media/games/<id>/GameData and /media/games/<id>
    // 3. /gaadata/<id> (internal NAND stock games 1..20)
    const std::vector<std::string> candidate_dirs = {
        cfg.vol_gaadata + "/" + id_str,
        cfg.games_path + "/" + id_str + "/GameData",
        cfg.games_path + "/" + id_str,
        cfg.internal_gaadata + "/" + id_str
    };

    // Verified alternative disc extension priority from SUP/scripts/intercept lines 34-43:
    const std::vector<std::string> alt_exts = {
        "m3u", "pbp", "img", "iso", "mdf", "toc", "cbn", "chd", "cue"
    };

    for (const auto& dir : candidate_dirs) {
        if (!PathExists(dir)) continue;

        if (dir.rfind(cfg.internal_gaadata, 0) == 0) {
            entry.is_internal_stock = true;
        }

        // Cover image path: <dir>/<BASENAME>.png
        if (entry.cover_path.empty() && !entry.disc_basename.empty()) {
            const std::string cov = dir + "/" + entry.disc_basename + ".png";
            if (IsRegularFile(cov)) {
                entry.cover_path = cov;
            }
        }

        // Check disc image formats
        if (!entry.disc_basename.empty()) {
            for (const auto& ext : alt_exts) {
                if (!cfg.launch_ra_from_stock_ui && ext == "m3u") {
                    continue; // Matches SUP/scripts/intercept: ignore .m3u when Stock PCSX is active
                }
                const std::string p_low = dir + "/" + entry.disc_basename + "." + ext;
                const std::string p_up  = dir + "/" + entry.disc_basename + "." + ToUpper(ext);
                if (IsRegularFile(p_low)) {
                    entry.launch_path   = p_low;
                    entry.file_verified = true;
                    if (ext == "chd") {
                        entry.backend = LaunchBackend::RetroArchCore; // intercept forces RA on .chd
                    }
                    break;
                } else if (IsRegularFile(p_up)) {
                    entry.launch_path   = p_up;
                    entry.file_verified = true;
                    if (ext == "chd") {
                        entry.backend = LaunchBackend::RetroArchCore;
                    }
                    break;
                }
            }
        }
        if (entry.file_verified) break;
    }

    // Set canonical default paths if running off-console or before USB mount
    if (entry.launch_path.empty()) {
        const std::string base = entry.disc_basename.empty() ? ("DISC_" + id_str) : entry.disc_basename;
        entry.launch_path = cfg.vol_gaadata + "/" + id_str + "/" + base + ".cue";
    }
    if (entry.cover_path.empty() && !entry.disc_basename.empty()) {
        entry.cover_path = cfg.vol_gaadata + "/" + id_str + "/" + entry.disc_basename + ".png";
    }
}

void GameLibrary::ScanStockInternalPlaylist(const RuntimeConfig& cfg) {
    // Verified playlist file from Project Eris pack: etc/project_eris/SUP/retroarch/psc_row.lpl
    const std::vector<std::string> lpl_candidates = {
        cfg.project_eris_path + "/etc/project_eris/SUP/retroarch/psc_row.lpl",
        "/tmp/psc_inspect/project eris/etc/project_eris/SUP/retroarch/psc_row.lpl"
    };

    std::string lpl_path;
    for (const auto& c : lpl_candidates) {
        if (IsRegularFile(c)) {
            lpl_path = c;
            break;
        }
    }

    // Lookup metadata in pcsx_data.db if available (verified SQLite DB in opt/psc_transfer_tools/pcsx_data.db)
    const std::vector<std::string> pcsx_db_candidates = {
        cfg.project_eris_path + "/opt/psc_transfer_tools/pcsx_data.db",
        "/tmp/psc_inspect/project eris/opt/psc_transfer_tools/pcsx_data.db"
    };
    sqlite3* meta_db = nullptr;
    for (const auto& db_c : pcsx_db_candidates) {
        if (IsRegularFile(db_c)) {
            if (sqlite3_open_v2(db_c.c_str(), &meta_db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK && meta_db) {
                break;
            }
            if (meta_db) {
                sqlite3_close(meta_db);
                meta_db = nullptr;
            }
        }
    }

    if (!lpl_path.empty()) {
        std::ifstream in(lpl_path);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }

        // RetroArch 6-line .lpl format verified in psc_row.lpl:
        // Line 0: path (/gaadata/1/SCES-00002.cue)
        // Line 1: title (Battle Arena Toshinden)
        // Line 2: core path
        // Line 3: core name
        // Line 4: crc
        // Line 5: playlist name
        int next_id = 1;
        for (std::size_t i = 0; i + 5 < lines.size(); i += 6) {
            GameEntry entry{};
            entry.id                = next_id++;
            entry.launch_path       = lines[i];
            entry.title             = lines[i + 1];
            entry.platform_id       = "psx";
            entry.platform_name     = "Sony PlayStation";
            entry.core_so           = "pcsx_rearmed_libretro.so";
            entry.backend           = cfg.launch_ra_from_stock_ui ? LaunchBackend::RetroArchCore : LaunchBackend::StockPcsx;
            entry.is_internal_stock = true;
            entry.file_verified     = IsRegularFile(entry.launch_path);

            // Extract serial from filename (e.g. SCES-00002)
            const auto slash = entry.launch_path.find_last_of('/');
            std::string fname = (slash != std::string::npos) ? entry.launch_path.substr(slash + 1) : entry.launch_path;
            const auto dot = fname.find_last_of('.');
            entry.disc_basename = (dot != std::string::npos) ? fname.substr(0, dot) : fname;
            entry.cover_path    = "/gaadata/" + std::to_string(entry.id) + "/" + entry.disc_basename + ".png";
            entry.publisher     = "Sony Interactive Entertainment";
            entry.developer     = "PlayStation Studios";
            entry.release_year  = 1998;
            entry.players       = 1;

            // Enrich exact Publisher, Developer, ReleaseYear, and Players from pcsx_data.db (verified query @ psc_game_add:1133)
            if (meta_db && !entry.disc_basename.empty()) {
                sqlite3_stmt* st = nullptr;
                const std::string q_inline =
                    "SELECT Games.Developer, Games.Publisher, Games.DateReleased, Games.Players "
                    "FROM Discs, Games WHERE Discs.SerialNumber = '" + entry.disc_basename + "' AND Discs.GameId = Games.Id LIMIT 1;";
                if (sqlite3_prepare_v2(meta_db, q_inline.c_str(), -1, &st, nullptr) == SQLITE_OK) {
                    if (sqlite3_step(st) == SQLITE_ROW) {
                        const unsigned char* dev = sqlite3_column_text(st, 0);
                        const unsigned char* pub = sqlite3_column_text(st, 1);
                        const unsigned char* dt  = sqlite3_column_text(st, 2);
                        const int pl             = sqlite3_column_int(st, 3);
                        if (dev) entry.developer = reinterpret_cast<const char*>(dev);
                        if (pub) entry.publisher = reinterpret_cast<const char*>(pub);
                        if (dt && std::strlen(reinterpret_cast<const char*>(dt)) >= 4) {
                            entry.release_year = std::atoi(reinterpret_cast<const char*>(dt));
                        }
                        if (pl > 0) entry.players = pl;
                    }
                    sqlite3_finalize(st);
                }
            }

            games_.push_back(entry);
            ++psx_count_;
        }
    }

    if (meta_db) {
        sqlite3_close(meta_db);
    }
}

void GameLibrary::ScanEmulationStationSystems(const RuntimeConfig& cfg) {
    const std::vector<std::string> cfg_candidates = {
        cfg.es_systems_cfg,
        "/tmp/psc_inspect/project eris/opt/emulationstation/.emulationstation/es_systems.cfg"
    };

    std::string sys_cfg_path;
    for (const auto& c : cfg_candidates) {
        if (IsRegularFile(c)) {
            sys_cfg_path = c;
            break;
        }
    }
    if (sys_cfg_path.empty()) return;

    std::ifstream in(sys_cfg_path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string xml = buffer.str();

    // Curated priority platforms to surface cleanly in Classic OS Platform strip
    const std::vector<std::string> priority_systems = {
        "snes", "genesis", "gba", "n64", "arcade", "psp", "dreamcast", "nes"
    };

    std::size_t pos = 0;
    while (true) {
        const auto s_open = xml.find("<system>", pos);
        if (s_open == std::string::npos) break;
        const auto s_close = xml.find("</system>", s_open);
        if (s_close == std::string::npos) break;

        const std::string block = xml.substr(s_open, s_close - s_open);
        pos = s_close + 9;

        const std::string name     = ExtractXmlTagValue(block, "name");
        const std::string fullname = ExtractXmlTagValue(block, "fullname");
        const std::string path     = ExtractXmlTagValue(block, "path");
        const std::string ext_str  = ExtractXmlTagValue(block, "extension");
        const std::string command  = ExtractXmlTagValue(block, "command");

        if (name.empty() || name == "psx") continue;
        if (std::find(priority_systems.begin(), priority_systems.end(), name) == priority_systems.end()) {
            continue;
        }

        PlatformCategory cat{};
        cat.id        = name;
        cat.full_name = fullname.empty() ? name : fullname;
        cat.rom_dir   = path;

        // Parse core .so from <command>es_launch_ra <core.so> %ROM%</command>
        std::istringstream cmd_ss(command);
        std::string tok;
        while (cmd_ss >> tok) {
            if (tok.find("_libretro.so") != std::string::npos) {
                cat.default_core = tok;
                break;
            }
        }
        if (cat.default_core.empty()) {
            cat.default_core = "pcsx_rearmed_libretro.so";
        }

        std::istringstream ext_ss(ext_str);
        std::string ext;
        while (ext_ss >> ext) {
            cat.extensions.push_back(ToLower(ext));
        }

        // Scan ROM directory if present on mounted USB
        if (PathExists(cat.rom_dir)) {
            DIR* d = opendir(cat.rom_dir.c_str());
            if (d) {
                struct dirent* de = nullptr;
                while ((de = readdir(d)) != nullptr) {
                    const std::string fname = de->d_name;
                    if (fname.empty() || fname[0] == '.') continue;
                    const auto dot = fname.find_last_of('.');
                    if (dot == std::string::npos) continue;
                    const std::string fext = ToLower(fname.substr(dot));
                    if (std::find(cat.extensions.begin(), cat.extensions.end(), fext) != cat.extensions.end()) {
                        GameEntry rom{};
                        rom.id            = 1000 + static_cast<int>(games_.size());
                        rom.title         = fname.substr(0, dot);
                        rom.publisher     = cat.full_name;
                        rom.developer     = "RetroArch Libretro";
                        rom.release_year  = 1995;
                        rom.players       = 2;
                        rom.platform_id   = cat.id;
                        rom.platform_name = cat.full_name;
                        rom.disc_basename = rom.title;
                        rom.launch_path   = cat.rom_dir + "/" + fname;
                        rom.core_so       = cat.default_core;
                        rom.backend       = LaunchBackend::RetroArchCore;
                        rom.file_verified = true;
                        games_.push_back(rom);
                        ++rom_count_;
                    }
                }
                closedir(d);
            }
        }

        platforms_.push_back(cat);
    }
}

void GameLibrary::RebuildPlatformCounts() {
    for (auto& p : platforms_) {
        p.game_count = 0;
        for (const auto& g : games_) {
            if (g.platform_id == p.id) {
                ++p.game_count;
            }
        }
    }
}

std::vector<const GameEntry*> GameLibrary::FilterByPlatform(const std::string& platform_id) const {
    std::vector<const GameEntry*> out;
    for (const auto& g : games_) {
        if (platform_id.empty() || g.platform_id == platform_id) {
            out.push_back(&g);
        }
    }
    return out;
}

std::vector<const GameEntry*> GameLibrary::SearchByTitle(const std::string& query) const {
    std::vector<const GameEntry*> out;
    const std::string q_low = ToLower(query);
    for (const auto& g : games_) {
        if (q_low.empty() || ToLower(g.title).find(q_low) != std::string::npos) {
            out.push_back(&g);
        }
    }
    return out;
}

} // namespace classic_os
