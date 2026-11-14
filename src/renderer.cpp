#include "renderer.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/utsname.h>

namespace classic_os {

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Init(const RuntimeConfig& cfg) {
    CollectSystemDiagnostics(cfg);

    // Verified hint from Project Eris game_manager @ 0x48e80
    SDL_SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");

    // Verified SDL_Init flags from Project Eris game_manager @ 0x6d80 (0x2220)
    const Uint32 init_flags = SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER;
    if (SDL_Init(init_flags) < 0) {
        // Fallback: if Wayland is explicitly forced in env but unavailable on test host, retry default driver
        const char* err1 = SDL_GetError();
        std::cerr << "[CLASSIC OS](RENDERER)(WARN) Initial SDL_Init(0x2220) failed: "
                  << (err1 ? err1 : "unknown") << "\n";
        if (std::getenv("SDL_VIDEODRIVER") != nullptr) {
            unsetenv("SDL_VIDEODRIVER");
            if (SDL_Init(init_flags) < 0) {
                std::cerr << "[CLASSIC OS](RENDERER)(ERROR) Fallback SDL_Init failed: "
                          << SDL_GetError() << "\n";
                WriteDiagnosticsLog(cfg);
                return false;
            }
        } else {
            WriteDiagnosticsLog(cfg);
            return false;
        }
    }

    // Verified SDL_CreateWindow parameters from Project Eris game_manager @ 0x6d94..0x6db4:
    // x = 0x1FFF0000 (SDL_WINDOWPOS_UNDEFINED), y = 0x1FFF0000, w = 1280, h = 720, flags = SDL_WINDOW_FULLSCREEN (0x1)
    Uint32 win_flags = SDL_WINDOW_FULLSCREEN;
    if (std::getenv("CLASSIC_OS_WINDOWED") != nullptr) {
        win_flags = SDL_WINDOW_SHOWN;
    }

    window_ = SDL_CreateWindow(
        "Classic OS",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        kLogicalWidth,
        kLogicalHeight,
        win_flags);

    if (!window_) {
        std::cerr << "[CLASSIC OS](RENDERER)(ERROR) SDL_CreateWindow(1280x720) failed: "
                  << SDL_GetError() << "\n";
        WriteDiagnosticsLog(cfg);
        return false;
    }

    // Verified SDL_CreateRenderer parameters from Project Eris game_manager @ 0x6db8..0x6dc4:
    // index = -1, flags = 0 (lets SDL2 pick accelerated driver first, then fallback)
    renderer_ = SDL_CreateRenderer(window_, -1, 0);
    if (!renderer_) {
        std::cerr << "[CLASSIC OS](RENDERER)(ERROR) SDL_CreateRenderer(win, -1, 0) failed: "
                  << SDL_GetError() << "\n";
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        WriteDiagnosticsLog(cfg);
        return false;
    }

    SDL_RenderSetLogicalSize(renderer_, kLogicalWidth, kLogicalHeight);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Collect runtime SDL2 driver & renderer diagnostics
    const char* vdrv = SDL_GetCurrentVideoDriver();
    diag_.video_driver = vdrv ? vdrv : "unknown";

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

    WriteDiagnosticsLog(cfg);
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

void Renderer::WriteDiagnosticsLog(const RuntimeConfig& cfg) const {
    const std::string log_path = cfg.runtime_log_path + "/classic_os_diagnostics.log";
    std::ofstream out(log_path, std::ios::out | std::ios::trunc);
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
    out << "Window Geometry   : " << diag_.window_width << "x" << diag_.window_height << "\n";
    out << "Output Buffer     : " << diag_.output_width << "x" << diag_.output_height << "\n";
    out << "Max Texture Dim   : " << diag_.max_texture_width << "x" << diag_.max_texture_height << "\n";
    out << "VRAM LRU Cap      : " << (cfg.max_vram_bytes / (1024 * 1024)) << " MB\n";
    out << "=========================================================\n";
}

} // namespace classic_os
