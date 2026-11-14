#include "ui_dashboard.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace classic_os {

namespace {

// Dark Premium Gaming Palette with Purple Accent Styling
constexpr SDL_Color kBgTop             = {11, 10, 18, 255};    // #0B0A12 Deep Obsidian
constexpr SDL_Color kBgBottom          = {19, 16, 34, 255};    // #131022 Midnight Indigo
constexpr SDL_Color kSidebarBg         = {15, 13, 26, 245};    // #0F0D1A Sidebar Surface
constexpr SDL_Color kCardBg            = {24, 21, 43, 240};    // #18152B Elevated Card
constexpr SDL_Color kCardHoverBg       = {36, 30, 66, 250};    // #241E42 Focused Card Surface
constexpr SDL_Color kCardBorder        = {48, 42, 82, 255};    // #302A52 Subtle Border
constexpr SDL_Color kCardHighlight     = {76, 66, 128, 120};   // Top Specular Edge
constexpr SDL_Color kPurplePrimary     = {139, 92, 246, 255};  // #8B5CF6 Electric Royal Purple
constexpr SDL_Color kPurpleBright      = {167, 139, 250, 255}; // #A78BFA Neon Lavender Focus
constexpr SDL_Color kPurpleDeep        = {76, 29, 149, 230};   // #4C1D95 Deep Amethyst Pill
constexpr SDL_Color kPurpleGlow        = {139, 92, 246, 65};   // Ambient Focus Halo
constexpr SDL_Color kTextPrimary       = {244, 243, 255, 255}; // #F4F3FF Crisp White
constexpr SDL_Color kTextSecondary     = {165, 161, 194, 255}; // #A5A1C2 Muted Lavender-Grey
constexpr SDL_Color kTextDim           = {108, 103, 140, 255}; // #6C678C Dim Metadata
constexpr SDL_Color kStatusGreen       = {52, 211, 153, 255};  // #34D399 Verified Badge
constexpr SDL_Color kStatusAmber       = {251, 191, 36, 255};  // #FBBF24 Warning/Unmounted Badge

std::string TruncateLabel(const std::string& s, std::size_t max_chars) {
    if (s.size() <= max_chars) return s;
    if (max_chars <= 3) return s.substr(0, max_chars);
    return s.substr(0, max_chars - 2) + "..";
}

void DrawTextLeft(
    Renderer& renderer,
    TextureCache& cache,
    const std::string& text,
    int x,
    int y,
    int tier,
    SDL_Color color,
    Uint8 alpha = 255) {
    const CachedTexture* ct = cache.GetText(text, tier, color, 0);
    if (!ct || !ct->texture) return;
    SDL_Rect dst{x, y, ct->width, ct->height};
    renderer.DrawTexture(ct->texture, nullptr, dst, alpha);
}

} // namespace

UiDashboard::UiDashboard(
    const RuntimeConfig& cfg,
    Renderer& renderer,
    TextureCache& cache,
    InputManager& input,
    const GameLibrary& library,
    Launcher& launcher)
    : cfg_(cfg),
      renderer_(renderer),
      cache_(cache),
      input_(input),
      library_(library),
      launcher_(launcher) {
    RebuildFilteredGames();
}

void UiDashboard::RebuildFilteredGames() {
    if (active_section_ == SidebarSection::PlayStation) {
        active_games_ = library_.FilterByPlatform("psx");
    } else if (!selected_platform_filter_.empty()) {
        active_games_ = library_.FilterByPlatform(selected_platform_filter_);
    } else {
        active_games_ = library_.FilterByPlatform("");
    }
    if (game_index_ >= static_cast<int>(active_games_.size())) {
        game_index_ = std::max(0, static_cast<int>(active_games_.size()) - 1);
    }
}

void UiDashboard::UpdateAnimation(float dt_seconds) {
    focus_pulse_phase_ += dt_seconds * 4.2f;
    if (focus_pulse_phase_ > 6.283185f) {
        focus_pulse_phase_ -= 6.283185f;
    }
    if (transition_alpha_ < 1.0f) {
        transition_alpha_ = std::min(1.0f, transition_alpha_ + dt_seconds * 6.0f);
    }
    const float diff = target_scroll_y_ - scroll_offset_y_;
    scroll_offset_y_ += diff * std::min(1.0f, dt_seconds * 12.0f);

    if (status_toast_timer_ > 0.0f) {
        status_toast_timer_ = std::max(0.0f, status_toast_timer_ - dt_seconds);
    }
}

bool UiDashboard::HandleAction(NavAction action, ExitTarget& out_exit_target) {
    if (action == NavAction::None) return true;

    if (action == NavAction::QuitApp) {
        out_exit_target = (cfg_.return_target == "launch_stockui") ? ExitTarget::StockUI : ExitTarget::BootMenu;
        launcher_.WriteLaunchFileCommand(out_exit_target);
        return false;
    }

    // Global shoulder paging: L1 / R1 cycles platform filter or jumps grid pages
    if (action == NavAction::PageLeft || action == NavAction::PageRight) {
        const auto& plats = library_.platforms();
        if (!plats.empty()) {
            const int total_tabs = static_cast<int>(plats.size()) + 1; // 0 = All
            int cur_tab = 0;
            for (std::size_t i = 0; i < plats.size(); ++i) {
                if (plats[i].id == selected_platform_filter_) {
                    cur_tab = static_cast<int>(i) + 1;
                    break;
                }
            }
            cur_tab = (action == NavAction::PageRight)
                          ? ((cur_tab + 1) % total_tabs)
                          : ((cur_tab - 1 + total_tabs) % total_tabs);
            selected_platform_filter_ = (cur_tab == 0) ? "" : plats[cur_tab - 1].id;
            platform_index_ = std::max(0, cur_tab - 1);
            RebuildFilteredGames();
            status_toast_ = selected_platform_filter_.empty()
                                ? "Filter: All Platforms"
                                : ("Filter: " + plats[cur_tab - 1].full_name);
            status_toast_timer_ = 2.2f;
        }
        return true;
    }

    // Global L2 / R2 switches sidebar sections directly
    if (action == NavAction::TabPrev || action == NavAction::TabNext) {
        const int delta = (action == NavAction::TabNext) ? 1 : -1;
        sidebar_index_ = (sidebar_index_ + delta + 6) % 6;
        active_section_ = static_cast<SidebarSection>(sidebar_index_);
        transition_alpha_ = 0.35f;
        RebuildFilteredGames();
        return true;
    }

    // Select button jumps directly to Runtime Diagnostics view
    if (action == NavAction::SelectBtn) {
        sidebar_index_  = static_cast<int>(SidebarSection::Diagnostics);
        active_section_ = SidebarSection::Diagnostics;
        focus_zone_     = FocusZone::Sidebar;
        transition_alpha_ = 0.35f;
        return true;
    }

    // Start button opens the Exit / Boot Menu Return view
    if (action == NavAction::StartBtn || action == NavAction::ConsoleReset) {
        sidebar_index_  = static_cast<int>(SidebarSection::ExitMenu);
        active_section_ = SidebarSection::ExitMenu;
        focus_zone_     = FocusZone::FeaturedHero;
        transition_alpha_ = 0.35f;
        return true;
    }

    // Navigation when Sidebar has focus
    if (focus_zone_ == FocusZone::Sidebar) {
        if (action == NavAction::Up) {
            sidebar_index_ = (sidebar_index_ + 5) % 6;
            active_section_ = static_cast<SidebarSection>(sidebar_index_);
            transition_alpha_ = 0.35f;
            RebuildFilteredGames();
        } else if (action == NavAction::Down) {
            sidebar_index_ = (sidebar_index_ + 1) % 6;
            active_section_ = static_cast<SidebarSection>(sidebar_index_);
            transition_alpha_ = 0.35f;
            RebuildFilteredGames();
        } else if (action == NavAction::Right || action == NavAction::Confirm) {
            if (active_section_ == SidebarSection::Home) {
                focus_zone_ = FocusZone::FeaturedHero;
            } else if (active_section_ == SidebarSection::PlayStation || active_section_ == SidebarSection::AllGames) {
                focus_zone_ = FocusZone::GameGrid;
            } else if (active_section_ == SidebarSection::Platforms) {
                focus_zone_ = FocusZone::PlatformStrip;
            } else if (active_section_ == SidebarSection::ExitMenu) {
                focus_zone_ = FocusZone::FeaturedHero;
            }
        }
        return true;
    }

    // Exit Menu View interaction (4 options: BootMenu, StockUI, RetroArchStandalone, Reversible Auto-Boot Toggle)
    if (active_section_ == SidebarSection::ExitMenu) {
        if (action == NavAction::Left || action == NavAction::Cancel) {
            focus_zone_ = FocusZone::Sidebar;
        } else if (action == NavAction::Up) {
            exit_option_index_ = (exit_option_index_ + 3) % 4;
        } else if (action == NavAction::Down) {
            exit_option_index_ = (exit_option_index_ + 1) % 4;
        } else if (action == NavAction::Confirm) {
            if (exit_option_index_ == 0) {
                out_exit_target = ExitTarget::BootMenu;
                launcher_.WriteLaunchFileCommand(out_exit_target);
                return false;
            } else if (exit_option_index_ == 1) {
                out_exit_target = ExitTarget::StockUI;
                launcher_.WriteLaunchFileCommand(out_exit_target);
                return false;
            } else if (exit_option_index_ == 2) {
                out_exit_target = ExitTarget::RetroArchStandalone;
                launcher_.WriteLaunchFileCommand(out_exit_target);
                return false;
            } else if (exit_option_index_ == 3) {
                const bool next_state = !launcher_.IsPrimaryBootDashboardEnabled();
                launcher_.SetPrimaryBootDashboard(next_state);
                status_toast_ = next_state
                    ? "Primary Auto-Boot ENABLED (Classic OS boots on power-on)"
                    : "Primary Auto-Boot DISABLED (Restored original Project Eris Boot Menu)";
                status_toast_timer_ = 3.2f;
                return true;
            }
        }
        return true;
    }

    // Home View Zone Navigation (FeaturedHero -> PlatformStrip -> GameGrid)
    if (active_section_ == SidebarSection::Home) {
        if (focus_zone_ == FocusZone::FeaturedHero) {
            if (action == NavAction::Left || action == NavAction::Cancel) {
                focus_zone_ = FocusZone::Sidebar;
            } else if (action == NavAction::Down) {
                focus_zone_ = FocusZone::PlatformStrip;
            } else if (action == NavAction::Confirm && !active_games_.empty()) {
                const GameEntry* g = active_games_[game_index_];
                if (!launcher_.LaunchGameAndRestore(*g, renderer_, cache_, input_)) {
                    status_toast_ = "Preview Mode: Disc image not mounted at " + g->launch_path;
                    status_toast_timer_ = 3.2f;
                }
            } else if (action == NavAction::Options && !active_games_.empty()) {
                GameEntry ra_copy = *active_games_[game_index_];
                ra_copy.backend = LaunchBackend::RetroArchCore;
                if (!launcher_.LaunchGameAndRestore(ra_copy, renderer_, cache_, input_)) {
                    status_toast_ = "RetroArch Launch: Core/ROM not mounted on console";
                    status_toast_timer_ = 3.2f;
                }
            }
            return true;
        }

        if (focus_zone_ == FocusZone::PlatformStrip) {
            const int pcount = static_cast<int>(library_.platforms().size());
            if (action == NavAction::Up) {
                focus_zone_ = FocusZone::FeaturedHero;
            } else if (action == NavAction::Down) {
                focus_zone_ = FocusZone::GameGrid;
            } else if (action == NavAction::Left) {
                if (platform_index_ > 0) {
                    --platform_index_;
                } else {
                    focus_zone_ = FocusZone::Sidebar;
                }
            } else if (action == NavAction::Right && platform_index_ + 1 < pcount) {
                ++platform_index_;
            } else if (action == NavAction::Cancel) {
                selected_platform_filter_.clear();
                RebuildFilteredGames();
                focus_zone_ = FocusZone::Sidebar;
            } else if (action == NavAction::Confirm && pcount > 0) {
                const auto& plat = library_.platforms()[platform_index_];
                if (selected_platform_filter_ == plat.id) {
                    selected_platform_filter_.clear();
                    status_toast_ = "Showing All Platforms";
                } else {
                    selected_platform_filter_ = plat.id;
                    status_toast_ = "Filtered by " + plat.full_name;
                }
                status_toast_timer_ = 2.0f;
                RebuildFilteredGames();
            }
            return true;
        }
    }

    // Platforms View Navigation
    if (active_section_ == SidebarSection::Platforms) {
        const int pcount = static_cast<int>(library_.platforms().size());
        if (action == NavAction::Left) {
            if (platform_index_ % 3 == 0) {
                focus_zone_ = FocusZone::Sidebar;
            } else {
                --platform_index_;
            }
        } else if (action == NavAction::Right && platform_index_ + 1 < pcount) {
            ++platform_index_;
        } else if (action == NavAction::Up && platform_index_ >= 3) {
            platform_index_ -= 3;
        } else if (action == NavAction::Down && platform_index_ + 3 < pcount) {
            platform_index_ += 3;
        } else if (action == NavAction::Cancel) {
            focus_zone_ = FocusZone::Sidebar;
        } else if (action == NavAction::Confirm && pcount > 0) {
            selected_platform_filter_ = library_.platforms()[platform_index_].id;
            RebuildFilteredGames();
            active_section_ = SidebarSection::AllGames;
            sidebar_index_  = static_cast<int>(SidebarSection::AllGames);
            focus_zone_     = FocusZone::GameGrid;
        }
        return true;
    }

    // Game Grid / Cover Row Navigation
    if (focus_zone_ == FocusZone::GameGrid) {
        const int cols = 5;
        const int total = static_cast<int>(active_games_.size());
        if (action == NavAction::Cancel) {
            focus_zone_ = FocusZone::Sidebar;
        } else if (action == NavAction::Left) {
            if (active_section_ == SidebarSection::Home) {
                if (game_index_ > 0) --game_index_;
                else focus_zone_ = FocusZone::Sidebar;
            } else {
                if (game_index_ % cols == 0) focus_zone_ = FocusZone::Sidebar;
                else if (game_index_ > 0) --game_index_;
            }
        } else if (action == NavAction::Right && game_index_ + 1 < total) {
            ++game_index_;
        } else if (action == NavAction::Up) {
            if (active_section_ == SidebarSection::Home) {
                focus_zone_ = FocusZone::PlatformStrip;
            } else if (game_index_ >= cols) {
                game_index_ -= cols;
            }
        } else if (action == NavAction::Down && active_section_ != SidebarSection::Home) {
            if (game_index_ + cols < total) {
                game_index_ += cols;
            }
        } else if (action == NavAction::Confirm && total > 0) {
            const GameEntry* g = active_games_[game_index_];
            if (!launcher_.LaunchGameAndRestore(*g, renderer_, cache_, input_)) {
                status_toast_ = "Preview Mode: Disc image not mounted (" + g->disc_basename + ")";
                status_toast_timer_ = 3.2f;
            }
        } else if (action == NavAction::Options && total > 0) {
            GameEntry ra_copy = *active_games_[game_index_];
            ra_copy.backend = LaunchBackend::RetroArchCore;
            if (!launcher_.LaunchGameAndRestore(ra_copy, renderer_, cache_, input_)) {
                status_toast_ = "RetroArch Launch: Core/ROM not mounted (" + ra_copy.core_so + ")";
                status_toast_timer_ = 3.2f;
            }
        }
    }

    return true;
}

void UiDashboard::Render() {
    RenderBackground();
    RenderSidebar();
    RenderTopBar();

    const SDL_Rect main_bounds{248, 64, 1008, 604};
    switch (active_section_) {
        case SidebarSection::Home:
            RenderHomeContent();
            break;
        case SidebarSection::PlayStation:
            RenderFullGameGrid(main_bounds, "SONY PLAYSTATION LIBRARY");
            break;
        case SidebarSection::Platforms:
            RenderPlatformTiles(main_bounds);
            break;
        case SidebarSection::AllGames:
            RenderFullGameGrid(main_bounds, "ALL DISCOVERED TITLES");
            break;
        case SidebarSection::Diagnostics:
            RenderDiagnosticsView(main_bounds);
            break;
        case SidebarSection::ExitMenu:
            RenderExitView(main_bounds);
            break;
    }

    RenderBottomControllerPrompts();
    RenderStatusToast();
}

void UiDashboard::RenderBackground() {
    const SDL_Rect full{0, 0, Renderer::kLogicalWidth, Renderer::kLogicalHeight};
    renderer_.DrawVerticalGradient(full, kBgTop, kBgBottom);

    // Subtle top ambient purple glow strip
    const SDL_Rect top_glow{228, 0, 1052, 3};
    renderer_.DrawFilledRect(top_glow, kPurplePrimary);
}

void UiDashboard::RenderTopBar() {
    // System status pills in top-right (x=248..1256, y=16..52)
    const auto& diag = renderer_.diagnostics();
    DrawTextLeft(renderer_, cache_, "DASHBOARD // SCPH-1000R", 252, 22, 1, kTextSecondary);

    // Renderer acceleration badge
    const SDL_Rect hw_pill{780, 16, 210, 30};
    renderer_.DrawBevelPanel(hw_pill, kCardBg, kCardBorder, kCardHighlight);
    const std::string drv_label = diag.video_driver + " / " + diag.renderer_name;
    DrawTextLeft(renderer_, cache_, TruncateLabel(drv_label, 22), 794, 22, 0,
                 diag.hardware_accelerated ? kStatusGreen : kStatusAmber);

    // Library counter badge
    const SDL_Rect lib_pill{1002, 16, 252, 30};
    renderer_.DrawBevelPanel(lib_pill, kPurpleDeep, kPurplePrimary, kCardHighlight);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d PS1  |  %d ROMS", library_.psx_count(), library_.rom_count());
    DrawTextLeft(renderer_, cache_, buf, 1020, 22, 0, kTextPrimary);
}

void UiDashboard::RenderSidebar() {
    const SDL_Rect sb_rect{0, 0, 228, Renderer::kLogicalHeight};
    renderer_.DrawFilledRect(sb_rect, kSidebarBg);
    renderer_.DrawLine(228, 0, 228, Renderer::kLogicalHeight, kCardBorder);

    // Brand Logo Block
    const SDL_Rect logo_badge{20, 20, 38, 38};
    renderer_.DrawBevelPanel(logo_badge, kPurplePrimary, kPurpleBright, kTextPrimary);
    DrawTextLeft(renderer_, cache_, "C", 32, 26, 2, kTextPrimary);
    DrawTextLeft(renderer_, cache_, "CLASSIC OS", 68, 20, 2, kTextPrimary);
    DrawTextLeft(renderer_, cache_, "PROJECT ERIS NATIVE", 68, 44, 0, kPurpleBright);

    const char* labels[6] = {
        "Home Hub",
        "PlayStation",
        "Platforms",
        "All Games",
        "Diagnostics",
        "Exit / Power"
    };

    for (int i = 0; i < 6; ++i) {
        const int item_y = 96 + i * 54;
        const SDL_Rect item_rect{14, item_y, 200, 44};
        const bool is_active  = (static_cast<int>(active_section_) == i);
        const bool is_focused = (focus_zone_ == FocusZone::Sidebar && sidebar_index_ == i);

        if (is_focused) {
            renderer_.DrawBevelPanel(item_rect, kCardHoverBg, kPurpleBright, kCardHighlight);
            const SDL_Rect bar{14, item_y + 6, 4, 32};
            renderer_.DrawFilledRect(bar, kPurpleBright);
        } else if (is_active) {
            renderer_.DrawBevelPanel(item_rect, kCardBg, kPurplePrimary, kCardHighlight);
            const SDL_Rect bar{14, item_y + 8, 4, 28};
            renderer_.DrawFilledRect(bar, kPurplePrimary);
        }

        // Vector geometric icon per navigation item
        const SDL_Rect icon_box{28, item_y + 13, 18, 18};
        renderer_.DrawOutlineRect(icon_box, is_active ? kPurpleBright : kTextDim, 2);
        if (is_active) {
            const SDL_Rect inner_dot{33, item_y + 18, 8, 8};
            renderer_.DrawFilledRect(inner_dot, kPurplePrimary);
        }

        DrawTextLeft(
            renderer_, cache_, labels[i],
            58, item_y + 12, 1,
            (is_active || is_focused) ? kTextPrimary : kTextSecondary);
    }

    // Controller & Memory telemetry card at bottom of sidebar
    const SDL_Rect pad_card{14, 548, 200, 112};
    renderer_.DrawBevelPanel(pad_card, kCardBg, kCardBorder, kCardHighlight);
    DrawTextLeft(renderer_, cache_, "CONTROLLER PORT 1", 24, 558, 0, kPurpleBright);
    DrawTextLeft(
        renderer_, cache_,
        input_.connected_pad_count() > 0 ? "PSC Pad Ready" : "USB / Key Ready",
        24, 580, 0, kStatusGreen);

    char vram_buf[64];
    std::snprintf(
        vram_buf, sizeof(vram_buf), "VRAM: %zu / %zu MB",
        cache_.used_bytes() / (1024 * 1024),
        cache_.max_bytes() / (1024 * 1024));
    DrawTextLeft(renderer_, cache_, vram_buf, 24, 606, 0, kTextSecondary);
    DrawTextLeft(
        renderer_, cache_,
        library_.regional_db_found() ? "DB: regional.db (RO)" : "DB: Stock LPL Ready",
        24, 630, 0, kTextDim);
}

void UiDashboard::RenderHomeContent() {
    const SDL_Rect hero_bounds{252, 62, 1002, 206};
    RenderFeaturedBanner(hero_bounds);

    const SDL_Rect plat_bounds{252, 282, 1002, 116};
    RenderPlatformTiles(plat_bounds);

    const SDL_Rect row_bounds{252, 412, 1002, 254};
    const std::string title = selected_platform_filter_.empty()
                                  ? "INSTALLED LIBRARY  //  QUICK LAUNCH"
                                  : ("FILTERED LIBRARY  //  " + selected_platform_filter_);
    RenderGameCoverRow(row_bounds, title);
}

void UiDashboard::RenderFeaturedBanner(const SDL_Rect& bounds) {
    const bool focused = (focus_zone_ == FocusZone::FeaturedHero);
    renderer_.DrawBevelPanel(
        bounds,
        focused ? kCardHoverBg : kCardBg,
        focused ? kPurpleBright : kPurplePrimary,
        kCardHighlight);

    if (focused) {
        const SDL_Rect accent_strip{bounds.x, bounds.y, 6, bounds.h};
        renderer_.DrawFilledRect(accent_strip, kPurpleBright);
    }

    if (active_games_.empty()) {
        DrawTextLeft(renderer_, cache_, "No games found in current filter.", bounds.x + 28, bounds.y + 80, 2, kTextSecondary);
        return;
    }

    const GameEntry* feat = active_games_[game_index_];

    // Left cover art frame inside Featured Hero (164x164)
    const SDL_Rect art_rect{bounds.x + 22, bounds.y + 21, 164, 164};
    RenderGameCard(*feat, art_rect, focused);

    // Featured Badge & Metadata
    const SDL_Rect pill{bounds.x + 208, bounds.y + 22, 178, 26};
    renderer_.DrawBevelPanel(pill, kPurpleDeep, kPurpleBright, kCardHighlight);
    DrawTextLeft(
        renderer_, cache_,
        feat->is_internal_stock ? "FEATURED // NAND" : "FEATURED // USB",
        bounds.x + 220, bounds.y + 26, 0, kTextPrimary);

    // Disc / Verification status pill
    const SDL_Rect status_pill{bounds.x + 398, bounds.y + 22, 195, 26};
    renderer_.DrawBevelPanel(status_pill, kCardBg, kCardBorder, kCardHighlight);
    DrawTextLeft(
        renderer_, cache_,
        feat->file_verified ? "DISC VERIFIED" : "METADATA READY",
        bounds.x + 412, bounds.y + 26, 0,
        feat->file_verified ? kStatusGreen : kStatusAmber);

    DrawTextLeft(renderer_, cache_, TruncateLabel(feat->title, 38), bounds.x + 208, bounds.y + 58, 3, kTextPrimary);

    char meta_line[256];
    std::snprintf(
        meta_line, sizeof(meta_line), "%s  |  %d  |  %d Player%s  |  ID #%d (%s)",
        feat->publisher.c_str(),
        feat->release_year,
        feat->players,
        feat->players > 1 ? "s" : "",
        feat->id,
        feat->disc_basename.c_str());
    DrawTextLeft(renderer_, cache_, TruncateLabel(meta_line, 68), bounds.x + 208, bounds.y + 98, 1, kTextSecondary);

    DrawTextLeft(renderer_, cache_, TruncateLabel("Path: " + feat->launch_path, 72), bounds.x + 208, bounds.y + 126, 0, kTextDim);

    // Interactive Launch CTA Buttons
    const SDL_Rect btn_pcsx{bounds.x + 208, bounds.y + 152, 236, 36};
    renderer_.DrawBevelPanel(btn_pcsx, focused ? kPurplePrimary : kPurpleDeep, kPurpleBright, kCardHighlight);
    DrawTextLeft(renderer_, cache_, "[ X ] LAUNCH GAME", bounds.x + 230, bounds.y + 161, 1, kTextPrimary);

    const SDL_Rect btn_ra{bounds.x + 458, bounds.y + 152, 250, 36};
    renderer_.DrawBevelPanel(btn_ra, kCardBg, kCardBorder, kCardHighlight);
    DrawTextLeft(renderer_, cache_, "[ TRI ] RETROARCH CORE", bounds.x + 476, bounds.y + 161, 1, kTextSecondary);
}

void UiDashboard::RenderPlatformTiles(const SDL_Rect& bounds) {
    const bool strip_focused = (focus_zone_ == FocusZone::PlatformStrip || active_section_ == SidebarSection::Platforms);
    DrawTextLeft(renderer_, cache_, "PLATFORMS & CORES  (L1 / R1 QUICK FILTER)", bounds.x, bounds.y, 1, kTextSecondary);

    const auto& plats = library_.platforms();
    if (plats.empty()) return;

    if (active_section_ == SidebarSection::Platforms) {
        // Full 3-column grid when on dedicated Platforms section
        const int cols = 3;
        const int card_w = 320;
        const int card_h = 110;
        for (std::size_t i = 0; i < plats.size(); ++i) {
            const int r = static_cast<int>(i) / cols;
            const int c = static_cast<int>(i) % cols;
            const SDL_Rect tile{bounds.x + c * (card_w + 18), bounds.y + 34 + r * (card_h + 16), card_w, card_h};
            const bool is_sel = (static_cast<int>(i) == platform_index_);
            renderer_.DrawBevelPanel(
                tile,
                is_sel ? kCardHoverBg : kCardBg,
                is_sel ? kPurpleBright : kCardBorder,
                kCardHighlight);
            DrawTextLeft(renderer_, cache_, plats[i].full_name, tile.x + 18, tile.y + 16, 2, kTextPrimary);
            DrawTextLeft(renderer_, cache_, "Core: " + plats[i].default_core, tile.x + 18, tile.y + 48, 0, kPurpleBright);
            DrawTextLeft(renderer_, cache_, std::to_string(plats[i].game_count) + " Titles Indexed", tile.x + 18, tile.y + 74, 1, kTextSecondary);
        }
        return;
    }

    // Compact horizontal platform strip on Home Hub
    const int visible_tiles = std::min(5, static_cast<int>(plats.size()));
    const int start_idx = std::max(0, std::min(platform_index_ - 2, static_cast<int>(plats.size()) - visible_tiles));
    const int tile_w = 190;
    const int tile_h = 78;

    for (int i = 0; i < visible_tiles; ++i) {
        const int idx = start_idx + i;
        const auto& p = plats[idx];
        const SDL_Rect tile{bounds.x + i * (tile_w + 12), bounds.y + 28, tile_w, tile_h};
        const bool is_focused = (strip_focused && idx == platform_index_);
        const bool is_active  = (selected_platform_filter_ == p.id);

        renderer_.DrawBevelPanel(
            tile,
            is_focused ? kCardHoverBg : (is_active ? kPurpleDeep : kCardBg),
            is_focused ? kPurpleBright : (is_active ? kPurplePrimary : kCardBorder),
            kCardHighlight);

        DrawTextLeft(renderer_, cache_, TruncateLabel(p.full_name, 18), tile.x + 14, tile.y + 14, 1, kTextPrimary);
        char count_str[48];
        std::snprintf(count_str, sizeof(count_str), "%d Title%s", p.game_count, p.game_count == 1 ? "" : "s");
        DrawTextLeft(renderer_, cache_, count_str, tile.x + 14, tile.y + 44, 0, is_focused ? kPurpleBright : kTextSecondary);
    }
}

void UiDashboard::RenderGameCoverRow(const SDL_Rect& bounds, const std::string& section_title) {
    DrawTextLeft(renderer_, cache_, section_title, bounds.x, bounds.y, 1, kTextSecondary);

    if (active_games_.empty()) {
        const SDL_Rect empty_box{bounds.x, bounds.y + 30, bounds.w, 180};
        renderer_.DrawBevelPanel(empty_box, kCardBg, kCardBorder, kCardHighlight);
        DrawTextLeft(renderer_, cache_, "No titles in selected platform filter. Press L1/R1 to switch.", bounds.x + 28, bounds.y + 104, 1, kTextSecondary);
        return;
    }

    const int visible_cards = 5;
    const int total = static_cast<int>(active_games_.size());
    const int start_idx = std::max(0, std::min(game_index_ - 2, total - visible_cards));
    const int card_w = 188;
    const int card_h = 214;

    for (int i = 0; i < visible_cards && (start_idx + i) < total; ++i) {
        const int idx = start_idx + i;
        const SDL_Rect card_rect{bounds.x + i * (card_w + 15), bounds.y + 28, card_w, card_h};
        const bool is_focused = (focus_zone_ == FocusZone::GameGrid && idx == game_index_);
        RenderGameCard(*active_games_[idx], card_rect, is_focused);
    }
}

void UiDashboard::RenderFullGameGrid(const SDL_Rect& bounds, const std::string& header_title) {
    DrawTextLeft(renderer_, cache_, header_title, bounds.x, bounds.y, 2, kTextPrimary);

    const int total = static_cast<int>(active_games_.size());
    if (total == 0) {
        DrawTextLeft(renderer_, cache_, "No games matched this filter.", bounds.x, bounds.y + 48, 1, kTextSecondary);
        return;
    }

    const int cols = 5;
    const int rows_visible = 2;
    const int cur_row = game_index_ / cols;
    const int first_row = std::max(0, cur_row - 1);
    const int card_w = 188;
    const int card_h = 236;

    for (int r = 0; r < rows_visible; ++r) {
        const int row_idx = first_row + r;
        for (int c = 0; c < cols; ++c) {
            const int idx = row_idx * cols + c;
            if (idx >= total) break;
            const SDL_Rect card_rect{
                bounds.x + c * (card_w + 15),
                bounds.y + 42 + r * (card_h + 18),
                card_w,
                card_h
            };
            const bool is_focused = (focus_zone_ == FocusZone::GameGrid && idx == game_index_);
            RenderGameCard(*active_games_[idx], card_rect, is_focused);
        }
    }
}

void UiDashboard::RenderGameCard(const GameEntry& game, const SDL_Rect& card_rect, bool focused) {
    if (focused) {
        const SDL_Rect halo{card_rect.x - 3, card_rect.y - 3, card_rect.w + 6, card_rect.h + 6};
        renderer_.DrawFilledRect(halo, kPurpleGlow);
    }

    renderer_.DrawBevelPanel(
        card_rect,
        focused ? kCardHoverBg : kCardBg,
        focused ? kPurpleBright : kCardBorder,
        kCardHighlight);

    // Cover image square area inside card
    const int pad = 10;
    const int art_size = std::min(card_rect.w - pad * 2, card_rect.h - (card_rect.h > 180 ? 64 : 20));
    const SDL_Rect art_rect{card_rect.x + pad, card_rect.y + pad, card_rect.w - pad * 2, art_size};

    const CachedTexture* cover_tex = cache_.GetOrLoadImage(game.cover_path);
    if (cover_tex && cover_tex->texture) {
        renderer_.DrawTexture(cover_tex->texture, nullptr, art_rect, 255);
    } else {
        // Procedural PlayStation Jewel-Case Cover Artwork when PNG file is not mounted
        SDL_Color top_c{38, 28, 78, 255};
        SDL_Color bot_c{18, 14, 38, 255};
        renderer_.DrawVerticalGradient(art_rect, top_c, bot_c);
        renderer_.DrawOutlineRect(art_rect, kCardBorder, 1);

        // Left jewel-case spine strip
        const SDL_Rect spine{art_rect.x, art_rect.y, 18, art_rect.h};
        renderer_.DrawFilledRect(spine, kPurpleDeep);
        DrawTextLeft(renderer_, cache_, "PS", spine.x + 2, spine.y + 6, 0, kTextPrimary);

        // Disc serial & year badge inside cover
        DrawTextLeft(renderer_, cache_, TruncateLabel(game.disc_basename, 12), art_rect.x + 26, art_rect.y + 16, 0, kPurpleBright);
        DrawTextLeft(renderer_, cache_, std::to_string(game.release_year), art_rect.x + 26, art_rect.y + art_rect.h - 26, 0, kTextSecondary);
    }

    if (card_rect.h > 180) {
        DrawTextLeft(
            renderer_, cache_,
            TruncateLabel(game.title, 18),
            card_rect.x + 10,
            card_rect.y + art_size + 16,
            1,
            focused ? kTextPrimary : kTextSecondary);

        DrawTextLeft(
            renderer_, cache_,
            TruncateLabel(game.publisher, 20),
            card_rect.x + 10,
            card_rect.y + art_size + 38,
            0,
            kTextDim);
    }
}

void UiDashboard::RenderDiagnosticsView(const SDL_Rect& bounds) {
    DrawTextLeft(renderer_, cache_, "RUNTIME HARDWARE & SDL2 COMPATIBILITY DIAGNOSTICS", bounds.x, bounds.y, 2, kTextPrimary);

    const auto& d = renderer_.diagnostics();
    const SDL_Rect panel{bounds.x, bounds.y + 38, bounds.w, 490};
    renderer_.DrawBevelPanel(panel, kCardBg, kPurplePrimary, kCardHighlight);

    struct Row { const char* k; std::string v; bool ok; };
    const std::vector<Row> rows = {
        {"Target Console / Kernel", d.kernel_info + " (" + d.cpu_arch_info + ")", true},
        {"SDL2 Video Driver", d.video_driver + " (Expected on PSC: wayland)", true},
        {"SDL2 Renderer Engine", d.renderer_name + (d.hardware_accelerated ? " [HW ACCELERATED]" : " [SOFTWARE FALLBACK]"), d.hardware_accelerated},
        {"Display & Logical Output", std::to_string(d.window_width) + "x" + std::to_string(d.window_height) + " -> " + std::to_string(d.output_width) + "x" + std::to_string(d.output_height), true},
        {"Max GPU Texture Size", std::to_string(d.max_texture_width) + "x" + std::to_string(d.max_texture_height), true},
        {"System RAM / Texture LRU", std::to_string(d.mem_available_mb) + " MB Free / " + std::to_string(cache_.used_bytes() / 1024) + " KB VRAM Cached (Cap: 32 MB)", true},
        {"Active Font Engine", TruncateLabel(cache_.active_font_path(), 64), true},
        {"Controller Port Status", input_.primary_pad_name() + (input_.mappings_loaded() ? " (gamecontrollerdb loaded)" : " (Raw PSC Joystick Map)"), true},
        {"Primary Boot Mode", launcher_.IsPrimaryBootDashboardEnabled() ? "ENABLED (0095_classic_os_boot.sh -> Classic OS)" : "DISABLED (Original Project Eris Boot Menu)", true},
        {"Databases (Read-Only)", std::string(library_.regional_db_found() ? "regional.db (RO)" : "Stock psc_row.lpl fallback") + " | " + std::to_string(library_.games().size()) + " Titles", true}
    };

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const int ry = panel.y + 18 + static_cast<int>(i) * 45;
        const SDL_Rect rbox{panel.x + 18, ry, panel.w - 36, 38};
        renderer_.DrawFilledRect(rbox, {18, 15, 34, 255});
        DrawTextLeft(renderer_, cache_, rows[i].k, rbox.x + 14, ry + 9, 1, kPurpleBright);
        DrawTextLeft(renderer_, cache_, rows[i].v, rbox.x + 310, ry + 9, 1, rows[i].ok ? kTextPrimary : kStatusAmber);
    }
}

void UiDashboard::RenderExitView(const SDL_Rect& bounds) {
    DrawTextLeft(renderer_, cache_, "SAFE EXIT, SUPERVISOR HANDOFF & BOOT SELECTION", bounds.x, bounds.y, 2, kTextPrimary);
    DrawTextLeft(renderer_, cache_, "Select supervisor exit target (/tmp/launchfilecommand) or toggle Primary Boot Dashboard mode:",
                 bounds.x, bounds.y + 32, 1, kTextSecondary);

    const bool auto_boot_on = launcher_.IsPrimaryBootDashboardEnabled();
    const std::string opt4_title = std::string("4. Primary Auto-Boot Mode: ") +
        (auto_boot_on ? "[ENABLED — Classic OS]" : "[DISABLED — Project Eris Boot Menu]");
    const std::string opt4_desc = auto_boot_on
        ? "Press [X] to restore original Project Eris Boot Menu as the default startup screen (updates classic_os_boot.cfg)."
        : "Press [X] to set Classic OS as the primary dashboard launched automatically at power-on.";

    const std::string titles[4] = {
        "1. Return to Project Eris Boot Menu  (echo \"launch_bootmenu\" > /tmp/launchfilecommand)",
        "2. Return to Stock PlayStation Carousel UI  (echo \"launch_stockui\" > /tmp/launchfilecommand)",
        "3. Launch RetroArch Standalone  (echo \"launch_retroarch_standalone\" > /tmp/launchfilecommand)",
        opt4_title
    };
    const std::string descs[4] = {
        "Exits Classic OS and hands control back to the original Project Eris Boot Menu.",
        "Exits Classic OS and launches the Stock PlayStation Classic carousel UI.",
        "Transitions directly into standalone RetroArch via the Project Eris supervisor.",
        opt4_desc
    };

    for (int i = 0; i < 4; ++i) {
        const SDL_Rect card{bounds.x, bounds.y + 72 + i * 114, 940, 98};
        const bool is_sel = (exit_option_index_ == i);
        renderer_.DrawBevelPanel(
            card,
            is_sel ? kCardHoverBg : kCardBg,
            is_sel ? kPurpleBright : kCardBorder,
            kCardHighlight);
        DrawTextLeft(renderer_, cache_, titles[i], card.x + 22, card.y + 18, 2, is_sel ? kTextPrimary : kTextSecondary);
        DrawTextLeft(renderer_, cache_, descs[i], card.x + 22, card.y + 54, 1, kTextDim);
    }
}

void UiDashboard::RenderBottomControllerPrompts() {
    const SDL_Rect footer{228, 676, 1052, 44};
    renderer_.DrawFilledRect(footer, kSidebarBg);
    renderer_.DrawLine(228, 676, 1280, 676, kCardBorder);

    auto draw_prompt = [&](const char* icon_file, const char* fallback_tag, const char* label, int x, SDL_Color col) {
        const std::string p1 = std::string("assets/icons/") + icon_file;
        const std::string p2 = cfg_.themes_path + "/" + cfg_.selected_theme + "/menu_files/Buttons/" + icon_file;
        const CachedTexture* ic = cache_.GetOrLoadImage(p1);
        if (!ic) ic = cache_.GetOrLoadImage(p2);
        if (ic && ic->texture) {
            const SDL_Rect dst{x, 684, 26, 26};
            renderer_.DrawTexture(ic->texture, nullptr, dst, 255);
            DrawTextLeft(renderer_, cache_, label, x + 32, 688, 1, col);
        } else {
            DrawTextLeft(renderer_, cache_, std::string(fallback_tag) + " " + label, x, 688, 1, col);
        }
    };

    draw_prompt("X_Btn.png", "[X]", "Launch / Select", 248, kTextPrimary);
    draw_prompt("Circle_Btn.png", "[O]", "Sidebar / Back", 458, kTextSecondary);
    draw_prompt("Tri_Btn.png", "[TRI]", "RetroArch", 660, kTextSecondary);
    draw_prompt("Sholder_L1_Btn.png", "[L1/R1]", "Filter", 830, kPurpleBright);
    draw_prompt("Sel_Btn.png", "[SEL]", "Diag", 996, kTextDim);
    draw_prompt("Start_Btn.png", "[START]", "Exit", 1126, kTextDim);
}

void UiDashboard::RenderStatusToast() {
    if (status_toast_timer_ <= 0.0f || status_toast_.empty()) return;
    const SDL_Rect toast{380, 622, 680, 42};
    renderer_.DrawBevelPanel(toast, kPurpleDeep, kPurpleBright, kCardHighlight);
    DrawTextLeft(renderer_, cache_, status_toast_, toast.x + 20, toast.y + 11, 1, kTextPrimary);
}

} // namespace classic_os
