#include "renderer.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <vector>

namespace classic_os {

Renderer::~Renderer() {
    Shutdown();
}

namespace {

bool ProbeFile(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

// A Wayland compositor is only usable if its socket exists. SDL's wayland driver
// needs XDG_RUNTIME_DIR (+ optionally WAYLAND_DISPLAY) to find it.
bool WaylandSocketAvailable() {
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    if (!rt || !*rt) return false;
    const char* disp = std::getenv("WAYLAND_DISPLAY");
    return ProbeFile(std::string(rt) + "/" + ((disp && *disp) ? disp : "wayland-0"));
}

bool SdlHasVideoDriver(const std::string& name) {
    const int n = SDL_GetNumVideoDrivers();
    for (int i = 0; i < n; ++i) {
        const char* d = SDL_GetVideoDriver(i);
        if (d && name == d) return true;
    }
    return false;
}

void AppendDiag(const RuntimeConfig& cfg, const std::string& line) {
    std::ofstream out(cfg.runtime_log_path + "/classic_os_diagnostics.log", std::ios::out | std::ios::app);
    if (out.is_open()) out << line << "\n";
}

} // namespace

bool Renderer::Init(const RuntimeConfig& cfg) {
    CollectSystemDiagnostics(cfg);

    // Verified hint from Project Eris game_manager @ 0x48e80
    SDL_SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");

    // Video backend candidates, in order. The console's real display stack is not assumed:
    //   1. an explicit override (CLASSIC_OS_VIDEO_DRIVER in classic_os_boot.cfg, --video-driver,
    //      or the CLASSIC_OS_VIDEODRIVER / SDL_VIDEODRIVER environment variables)
    //   2. wayland  (only when a compositor socket is actually present)
    //   3. kmsdrm
    //   4. SDL's own default order
    // Each candidate must get all the way through SDL_Init + window + renderer to be accepted;
    // "dummy"/"offscreen" are never auto-selected because they would "succeed" with no picture.
    std::vector<std::string> candidates;
    const char* env_drv = std::getenv("SDL_VIDEODRIVER");
    if (!cfg.video_driver.empty()) {
        candidates.push_back(cfg.video_driver);
    } else if (env_drv && *env_drv) {
        candidates.push_back(env_drv);
    }
    candidates.push_back("wayland");
    candidates.push_back("kmsdrm");
    candidates.push_back("");

    std::vector<std::string> tried;
    for (const std::string& cand : candidates) {
        bool dup = false;
        for (const auto& t : tried) dup = dup || (t == cand);
        if (dup) continue;
        tried.push_back(cand);

        if (cand == "wayland" && cfg.video_driver != "wayland" && !WaylandSocketAvailable()) {
            AppendDiag(cfg, "[video] skip wayland: no compositor socket (XDG_RUNTIME_DIR/WAYLAND_DISPLAY)");
            continue;
        }

        if (cand.empty()) unsetenv("SDL_VIDEODRIVER");
        else              setenv("SDL_VIDEODRIVER", cand.c_str(), 1);

        SDL_ClearError();
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            const char* e = SDL_GetError();
            AppendDiag(cfg, "[video] " + (cand.empty() ? std::string("default") : cand) + ": SDL_Init(VIDEO) failed: " + (e ? e : "?"));
            SDL_Quit();
            continue;
        }
        if (!cand.empty() && !SdlHasVideoDriver(cand)) {
            AppendDiag(cfg, "[video] " + cand + ": not compiled into this SDL2");
            SDL_Quit();
            continue;
        }
        if (TryCreateWindowAndRenderer(cfg, cand)) {
            const bool headless = (diag_.video_driver == "dummy" || diag_.video_driver == "offscreen");
            if (headless && cfg.video_driver != diag_.video_driver) {
                AppendDiag(cfg, "[video] rejected headless backend '" + diag_.video_driver + "' (not explicitly requested)");
                if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
                if (window_)   { SDL_DestroyWindow(window_);     window_   = nullptr; }
                SDL_Quit();
                continue;
            }
            // Joystick / controller subsystems are optional: a failure must not stop the UI
            // (the dashboard is still usable via GPIO keys), but it is logged.
            if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
                const char* e = SDL_GetError();
                std::cerr << "[CLASSIC OS](RENDERER)(WARN) joystick/gamecontroller init failed: " << (e ? e : "?") << "\n";
                AppendDiag(cfg, std::string("[input] SDL joystick/gamecontroller init failed: ") + (e ? e : "?"));
            }
            WriteDiagnosticsLog(cfg, tried);
            return true;
        }
        SDL_Quit();
    }

    std::cerr << "[CLASSIC OS](RENDERER)(ERROR) No usable SDL2 video backend (tried:";
    for (const auto& t : tried) std::cerr << " " << (t.empty() ? "default" : t);
    std::cerr << "). See " << cfg.runtime_log_path << "/classic_os_diagnostics.log\n";
    WriteDiagnosticsLog(cfg, tried);
    return false;
}

bool Renderer::TryCreateWindowAndRenderer(const RuntimeConfig& cfg, const std::string& cand) {
    const std::string label = cand.empty() ? std::string("default") : cand;

    // Verified SDL_CreateWindow parameters from Project Eris game_manager @ 0x6d94..0x6db4:
    // x = y = SDL_WINDOWPOS_UNDEFINED, w = 1280, h = 720, flags = SDL_WINDOW_FULLSCREEN.
    // If the display has no exact 1280x720 mode (HDMI sink / kmsdrm), fall back to the desktop
    // mode; SDL_RenderSetLogicalSize() below then scales the fixed 1280x720 UI to the real output.
    std::vector<Uint32> window_modes;
    if (std::getenv("CLASSIC_OS_WINDOWED") != nullptr) {
        window_modes.push_back(SDL_WINDOW_SHOWN);
    } else {
        window_modes.push_back(SDL_WINDOW_FULLSCREEN);
        window_modes.push_back(SDL_WINDOW_FULLSCREEN_DESKTOP);
        window_modes.push_back(SDL_WINDOW_SHOWN);
    }

    for (Uint32 wf : window_modes) {
        window_ = SDL_CreateWindow("Classic OS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                   kLogicalWidth, kLogicalHeight, wf);
        if (!window_) {
            const char* e = SDL_GetError();
            AppendDiag(cfg, "[video] " + label + ": SDL_CreateWindow(flags=" + std::to_string(wf) + ") failed: " + (e ? e : "?"));
            continue;
        }

        // Verified SDL_CreateRenderer parameters from game_manager @ 0x6db8..0x6dc4: (-1, 0) = SDL picks
        // the accelerated driver first. Fall back to the software renderer so a missing GPU/GLES stack
        // still produces a picture instead of a black screen.
        const Uint32 renderer_flags[2] = {0u, SDL_RENDERER_SOFTWARE};
        for (Uint32 rf : renderer_flags) {
            renderer_ = SDL_CreateRenderer(window_, -1, rf);
            if (renderer_) break;
            const char* e = SDL_GetError();
            AppendDiag(cfg, "[video] " + label + ": SDL_CreateRenderer(flags=" + std::to_string(rf) + ") failed: " + (e ? e : "?"));
        }
        if (renderer_) break;

        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (!window_ || !renderer_) {
        return false;
    }

    SDL_RenderSetLogicalSize(renderer_, kLogicalWidth, kLogicalHeight);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Collect runtime SDL2 driver & renderer diagnostics
    const char* vdrv = SDL_GetCurrentVideoDriver();
    diag_.video_driver = vdrv ? vdrv : "unknown";

    SDL_DisplayMode dm{};
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        diag_.display_width  = dm.w;
        diag_.display_height = dm.h;
        diag_.display_hz     = dm.refresh_rate;
    }

    SDL_GetWindowSize(window_, &diag_.window_width, &diag_.window_height);
    if (SDL_GetRendererOutputSize(renderer_, &diag_.output_width, &diag_.output_height) != 0) {
        diag_.output_width  = diag_.window_width;
        diag_.output_height = diag_.window_height;
    }

    SDL_RendererInfo rinfo{};
    if (SDL_GetRendererInfo(renderer_, &rinfo) == 0) {
        diag_.renderer_name        = rinfo.name ? rinfo.name : "unknown";
        diag_.hardware_accelerated = (rinfo.flags & SDL_RENDERER_ACCELERATED) != 0;
        diag_.vsync_enabled        = (rinfo.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
        diag_.target_texture_ok    = (rinfo.flags & SDL_RENDERER_TARGETTEXTURE) != 0;
        diag_.max_texture_width    = rinfo.max_texture_width;
        diag_.max_texture_height   = rinfo.max_texture_height;
    }
    return true;
}

void Renderer::Shutdown() {
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void Renderer::BeginFrame(SDL_Color clear_color) {
    if (!renderer_) return;
    SDL_SetRenderDrawColor(renderer_, clear_color.r, clear_color.g, clear_color.b, clear_color.a);
    SDL_RenderClear(renderer_);
}

void Renderer::EndFrame() {
    if (!renderer_) return;
    SDL_RenderPresent(renderer_);
}

void Renderer::DrawFilledRect(const SDL_Rect& rect, SDL_Color color) {
    if (!renderer_) return;
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer_, &rect);
}

void Renderer::DrawOutlineRect(const SDL_Rect& rect, SDL_Color color, int thickness) {
    if (!renderer_) return;
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    for (int i = 0; i < thickness; ++i) {
        SDL_Rect r{rect.x + i, rect.y + i, rect.w - i * 2, rect.h - i * 2};
        if (r.w > 0 && r.h > 0) {
            SDL_RenderDrawRect(renderer_, &r);
        }
    }
}

void Renderer::DrawBevelPanel(const SDL_Rect& rect, SDL_Color fill, SDL_Color border, SDL_Color highlight) {
    DrawFilledRect(rect, fill);
    DrawOutlineRect(rect, border, 1);
    if (rect.w > 4 && rect.h > 4) {
        SDL_Rect top_hl{rect.x + 2, rect.y + 1, rect.w - 4, 1};
        DrawFilledRect(top_hl, highlight);
    }
}

void Renderer::DrawVerticalGradient(const SDL_Rect& rect, SDL_Color top, SDL_Color bottom) {
    if (!renderer_ || rect.h <= 0 || rect.w <= 0) return;
    const int bands = 24;
    const int band_h = (rect.h + bands - 1) / bands;
    for (int i = 0; i < bands; ++i) {
        const int y0 = rect.y + i * band_h;
        if (y0 >= rect.y + rect.h) break;
        const int h = (y0 + band_h > rect.y + rect.h) ? (rect.y + rect.h - y0) : band_h;
        const float t = static_cast<float>(i) / static_cast<float>(bands - 1);
        SDL_Color c{
            static_cast<Uint8>(top.r + (bottom.r - top.r) * t),
            static_cast<Uint8>(top.g + (bottom.g - top.g) * t),
            static_cast<Uint8>(top.b + (bottom.b - top.b) * t),
            static_cast<Uint8>(top.a + (bottom.a - top.a) * t)
        };
        SDL_Rect band{rect.x, y0, rect.w, h};
        DrawFilledRect(band, c);
    }
}

void Renderer::DrawLine(int x1, int y1, int x2, int y2, SDL_Color color) {
    if (!renderer_) return;
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer_, x1, y1, x2, y2);
}

void Renderer::DrawTexture(SDL_Texture* tex, const SDL_Rect* src, const SDL_Rect& dst, Uint8 alpha) {
    if (!renderer_ || !tex) return;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex, alpha);
    SDL_RenderCopyEx(renderer_, tex, src, &dst, 0.0, nullptr, SDL_FLIP_NONE);
}

void Renderer::SetClipRect(const SDL_Rect* rect) {
    if (!renderer_) return;
    SDL_RenderSetClipRect(renderer_, rect);
}

void Renderer::CollectSystemDiagnostics(const RuntimeConfig& /*cfg*/) {
    struct utsname uts{};
    if (uname(&uts) == 0) {
        diag_.kernel_info   = std::string(uts.sysname) + " " + uts.release;
        diag_.cpu_arch_info = uts.machine;
    } else {
        diag_.kernel_info   = "Linux 4.4.22 (fallback)";
        diag_.cpu_arch_info = "armv8l / armhf";
    }

    std::ifstream mem("/proc/meminfo");
    if (mem.is_open()) {
        std::string key;
        long value_kb = 0;
        std::string unit;
        while (mem >> key >> value_kb >> unit) {
            if (key == "MemTotal:") {
                diag_.mem_total_mb = static_cast<int>(value_kb / 1024);
            } else if (key == "MemAvailable:") {
                diag_.mem_available_mb = static_cast<int>(value_kb / 1024);
            }
        }
    }
}

void Renderer::WriteDiagnosticsLog(const RuntimeConfig& cfg, const std::vector<std::string>& tried) const {
    const std::string log_path = cfg.runtime_log_path + "/classic_os_diagnostics.log";
    // Append: AppendDiag() lines written during backend probing must survive
    std::ofstream out(log_path, std::ios::out | std::ios::app);
    if (!out.is_open()) {
        return;
    }
    out << "=========================================================\n";
    out << " CLASSIC OS — PLAYSTATION CLASSIC RUNTIME DIAGNOSTICS\n";
    out << "=========================================================\n";
    out << "Kernel / OS       : " << diag_.kernel_info << "\n";
    out << "CPU Architecture  : " << diag_.cpu_arch_info << " (ptr=" << (sizeof(void*) * 8) << "-bit)\n";
    out << "System Memory     : " << diag_.mem_available_mb << " MB free / " << diag_.mem_total_mb << " MB total\n";
    out << "SDL Video Driver  : " << diag_.video_driver << "\n";
    out << "SDL Renderer Name : " << diag_.renderer_name << "\n";
    out << "HW Acceleration   : " << (diag_.hardware_accelerated ? "YES (SDL_RENDERER_ACCELERATED)" : "NO (Software Fallback)") << "\n";
    out << "VSync Present     : " << (diag_.vsync_enabled ? "YES" : "NO") << "\n";
    out << "Target Texture    : " << (diag_.target_texture_ok ? "YES" : "NO") << "\n";
    out << "Backends tried    :";
    for (const auto& t : tried) out << " " << (t.empty() ? "default" : t);
    out << "\n";
    out << "Display Mode      : " << diag_.display_width << "x" << diag_.display_height << "@" << diag_.display_hz << "Hz\n";
    out << "Window Geometry   : " << diag_.window_width << "x" << diag_.window_height << "\n";
    out << "Output Buffer     : " << diag_.output_width << "x" << diag_.output_height << "\n";
    out << "Max Texture Dim   : " << diag_.max_texture_width << "x" << diag_.max_texture_height << "\n";
    out << "VRAM LRU Cap      : " << (cfg.max_vram_bytes / (1024 * 1024)) << " MB\n";
    out << "=========================================================\n";
}

} // namespace classic_os
