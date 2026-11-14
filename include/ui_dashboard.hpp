#ifndef CLASSIC_OS_UI_DASHBOARD_HPP
#define CLASSIC_OS_UI_DASHBOARD_HPP

#include "config.hpp"
#include "renderer.hpp"
#include "texture_cache.hpp"
#include "input.hpp"
#include "library.hpp"
#include "launcher.hpp"
#include <string>
#include <vector>

namespace classic_os {

enum class SidebarSection {
    Home = 0,
    PlayStation = 1,
    Platforms = 2,
    AllGames = 3,
    Diagnostics = 4,
    ExitMenu = 5
};

enum class FocusZone {
    Sidebar = 0,
    FeaturedHero = 1,
    PlatformStrip = 2,
    GameGrid = 3,
    ModalDialog = 4
};

class UiDashboard {
public:
    UiDashboard(
        const RuntimeConfig& cfg,
        Renderer& renderer,
        TextureCache& cache,
        InputManager& input,
        const GameLibrary& library,
        Launcher& launcher);

    // Returns false when the user confirms exiting Classic OS
    bool HandleAction(NavAction action, ExitTarget& out_exit_target);
    void UpdateAnimation(float dt_seconds);
    void Render();

private:
    void RebuildFilteredGames();
    void RenderBackground();
    void RenderTopBar();
    void RenderSidebar();
    void RenderHomeContent();
    void RenderFeaturedBanner(const SDL_Rect& bounds);
    void RenderPlatformTiles(const SDL_Rect& bounds);
    void RenderGameCoverRow(const SDL_Rect& bounds, const std::string& section_title);
    void RenderFullGameGrid(const SDL_Rect& bounds, const std::string& header_title);
    void RenderDiagnosticsView(const SDL_Rect& bounds);
    void RenderExitView(const SDL_Rect& bounds);
    void RenderGameCard(const GameEntry& game, const SDL_Rect& card_rect, bool focused);
    void RenderBottomControllerPrompts();
    void RenderStatusToast();

    const RuntimeConfig& cfg_;
    Renderer& renderer_;
    TextureCache& cache_;
    InputManager& input_;
    const GameLibrary& library_;
    Launcher& launcher_;

    SidebarSection active_section_ = SidebarSection::Home;
    FocusZone focus_zone_          = FocusZone::FeaturedHero;

    int sidebar_index_             = 0;
    int platform_index_            = 0;
    int game_index_                = 0;
    int exit_option_index_         = 0;

    float scroll_offset_y_         = 0.0f;
    float target_scroll_y_         = 0.0f;
    float focus_pulse_phase_       = 0.0f;
    float transition_alpha_        = 1.0f;

    std::string selected_platform_filter_;
    std::vector<const GameEntry*> active_games_;
    std::string status_toast_;
    float status_toast_timer_      = 0.0f;
};

} // namespace classic_os

#endif // CLASSIC_OS_UI_DASHBOARD_HPP
