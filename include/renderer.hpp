#ifndef CLASSIC_OS_RENDERER_HPP
#define CLASSIC_OS_RENDERER_HPP

#include "sdl_sqlite_abi.hpp"
#include "config.hpp"
#include <string>
#include <vector>

namespace classic_os {

struct DisplayDiagnostics {
    std::string video_driver;
    std::string renderer_name;
    bool hardware_accelerated = false;
    bool vsync_enabled        = false;
    bool target_texture_ok    = false;
    int window_width          = 0;
    int window_height         = 0;
    int output_width          = 0;
    int output_height         = 0;
    int display_width         = 0;
    int display_height        = 0;
    int display_hz            = 0;
    int max_texture_width     = 0;
    int max_texture_height    = 0;
    std::string cpu_arch_info;
    std::string kernel_info;
    int mem_total_mb          = 0;
    int mem_available_mb      = 0;
};

class Renderer {
public:
    static constexpr int kLogicalWidth  = 1280;
    static constexpr int kLogicalHeight = 720;

    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Init(const RuntimeConfig& cfg);
    void Shutdown();

    void BeginFrame(SDL_Color clear_color);
    void EndFrame();

    void DrawFilledRect(const SDL_Rect& rect, SDL_Color color);
    void DrawOutlineRect(const SDL_Rect& rect, SDL_Color color, int thickness = 1);
    void DrawBevelPanel(const SDL_Rect& rect, SDL_Color fill, SDL_Color border, SDL_Color highlight);
    void DrawVerticalGradient(const SDL_Rect& rect, SDL_Color top, SDL_Color bottom);
    void DrawLine(int x1, int y1, int x2, int y2, SDL_Color color);
    void DrawTexture(SDL_Texture* tex, const SDL_Rect* src, const SDL_Rect& dst, Uint8 alpha = 255);
    void SetClipRect(const SDL_Rect* rect);

    SDL_Renderer* raw() const { return renderer_; }
    SDL_Window* window() const { return window_; }
    const DisplayDiagnostics& diagnostics() const { return diag_; }

private:
    void CollectSystemDiagnostics(const RuntimeConfig& cfg);
    bool TryCreateWindowAndRenderer(const RuntimeConfig& cfg, const std::string& cand);
    void WriteDiagnosticsLog(const RuntimeConfig& cfg, const std::vector<std::string>& tried) const;

    SDL_Window* window_     = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool ttf_initialized_   = false;
    DisplayDiagnostics diag_{};
};

} // namespace classic_os

#endif // CLASSIC_OS_RENDERER_HPP
