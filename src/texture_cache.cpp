#include "texture_cache.hpp"
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <vector>

// Exact libpng16 simplified API struct & functions verified in game_manager (.dynsym):
// png_image_begin_read_from_file, png_image_finish_read, png_image_free
extern "C" {
struct png_control;
struct png_image {
    png_control* opaque;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t flags;
    uint32_t colormap_entries;
    uint32_t warning_or_error;
    char message[64];
};
constexpr uint32_t kPngImageVersion = 1;
constexpr uint32_t kPngFormatRgba   = 0x03u; // PNG_FORMAT_RGBA

int png_image_begin_read_from_file(png_image* image, const char* file_name);
int png_image_finish_read(png_image* image, const void* background, void* buffer, int32_t row_stride, void* colormap);
void png_image_free(png_image* image);
}

namespace classic_os {

namespace {

// Dynamic function pointers for libSDL2_ttf-2.0.so.0 and libSDL2_image-2.0.so.0
// (Loads directly from /usr/sony/lib on PlayStation Classic or system path, with clean software fallback)
struct DynamicSdlLibs {
    void* ttf_handle = nullptr;
    void* img_handle = nullptr;

    int (*p_TTF_Init)(void) = nullptr;
    void (*p_TTF_Quit)(void) = nullptr;
    TTF_Font* (*p_TTF_OpenFont)(const char*, int) = nullptr;
    void (*p_TTF_CloseFont)(TTF_Font*) = nullptr;
    SDL_Surface* (*p_TTF_RenderUTF8_Blended_Wrapped)(TTF_Font*, const char*, SDL_Color, Uint32) = nullptr;
    SDL_Surface* (*p_IMG_Load)(const char*) = nullptr;

    void Load() {
        const char* ttf_paths[] = {
            "libSDL2_ttf-2.0.so.0",
            "/usr/sony/lib/libSDL2_ttf-2.0.so.0",
            "/media/project_eris/lib/libSDL2_ttf-2.0.so.0",
            nullptr
        };
        for (int i = 0; ttf_paths[i]; ++i) {
            ttf_handle = dlopen(ttf_paths[i], RTLD_LAZY | RTLD_LOCAL);
            if (ttf_handle) break;
        }
        if (ttf_handle) {
            p_TTF_Init = reinterpret_cast<int(*)(void)>(dlsym(ttf_handle, "TTF_Init"));
            p_TTF_Quit = reinterpret_cast<void(*)(void)>(dlsym(ttf_handle, "TTF_Quit"));
            p_TTF_OpenFont = reinterpret_cast<TTF_Font*(*)(const char*, int)>(dlsym(ttf_handle, "TTF_OpenFont"));
            p_TTF_CloseFont = reinterpret_cast<void(*)(TTF_Font*)>(dlsym(ttf_handle, "TTF_CloseFont"));
            p_TTF_RenderUTF8_Blended_Wrapped = reinterpret_cast<SDL_Surface*(*)(TTF_Font*, const char*, SDL_Color, Uint32)>(
                dlsym(ttf_handle, "TTF_RenderUTF8_Blended_Wrapped"));
        }

        const char* img_paths[] = {
            "libSDL2_image-2.0.so.0",
            "/usr/sony/lib/libSDL2_image-2.0.so.0",
            "/media/project_eris/lib/libSDL2_image-2.0.so.0",
            nullptr
        };
        for (int i = 0; img_paths[i]; ++i) {
            img_handle = dlopen(img_paths[i], RTLD_LAZY | RTLD_LOCAL);
            if (img_handle) break;
        }
        if (img_handle) {
            p_IMG_Load = reinterpret_cast<SDL_Surface*(*)(const char*)>(dlsym(img_handle, "IMG_Load"));
        }
    }
};

DynamicSdlLibs& GetDynLibs() {
    static DynamicSdlLibs libs;
    static bool initialized = false;
    if (!initialized) {
        libs.Load();
        initialized = true;
    }
    return libs;
}

bool FileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

// Built-in 5x7 ASCII glyph bitmap table so text rendering works even if TTF fonts or libSDL2_ttf are missing
const uint8_t kGlyph5x7[96][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, // '!'
    {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00}, // '"'
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, // '#'
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // '$'
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // '%'
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // '&'
    {0x04,0x04,0x08,0x00,0x00,0x00,0x00}, // '\''
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // '('
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // ')'
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // '*'
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x04,0x04,0x08}, // ','
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // '.'
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, // '/'
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // '0'
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // '1'
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // '2'
    {0x1F,0x02,0x04,0x06,0x01,0x11,0x0E}, // '3'
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // '4'
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // '5'
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // '6'
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // '7'
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // '8'
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // '9'
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, // ':'
    {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08}, // ';'
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // '<'
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // '='
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // '>'
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // '?'
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // '@'
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // 'A'
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // 'B'
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // 'C'
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // 'D'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // 'E'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // 'F'
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // 'G'
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // 'H'
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // 'I'
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // 'J'
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // 'K'
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // 'L'
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // 'M'
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, // 'N'
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'O'
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // 'P'
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // 'Q'
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // 'R'
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // 'S'
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // 'T'
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // 'U'
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // 'V'
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // 'W'
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // 'X'
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // 'Y'
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // 'Z'
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // '['
    {0x10,0x08,0x08,0x04,0x02,0x02,0x01}, // '\\'
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // ']'
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // '_'
    {0x08,0x04,0x02,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // 'a'
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, // 'b'
    {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}, // 'c'
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, // 'd'
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // 'e'
    {0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, // 'f'
    {0x00,0x0F,0x11,0x11,0x0F,0x01,0x0E}, // 'g'
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, // 'h'
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // 'i'
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // 'j'
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // 'k'
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // 'l'
    {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, // 'm'
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, // 'n'
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // 'o'
    {0x00,0x1E,0x11,0x11,0x1E,0x10,0x10}, // 'p'
    {0x00,0x0F,0x11,0x11,0x0F,0x01,0x01}, // 'q'
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // 'r'
    {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}, // 's'
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, // 't'
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, // 'u'
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // 'v'
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, // 'w'
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // 'x'
    {0x00,0x11,0x11,0x11,0x0F,0x01,0x0E}, // 'y'
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // 'z'
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, // '{'
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, // '|'
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, // '}'
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00}, // '~'
    {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F}  // DEL
};

SDL_Surface* RenderSoftwareBitmapText(const std::string& text, int font_tier, SDL_Color color) {
    const int scale = (font_tier <= 0) ? 2 : (font_tier == 1 ? 2 : (font_tier == 2 ? 3 : 4));
    const int char_w = 6 * scale;
    const int char_h = 8 * scale;
    const int len = static_cast<int>(text.empty() ? 1 : text.size());
    const int surf_w = len * char_w;
    const int surf_h = char_h;

    SDL_Surface* surf = SDL_CreateRGBSurface(
        0, surf_w, surf_h, 32,
        0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
    if (!surf) return nullptr;

    std::memset(surf->pixels, 0, static_cast<std::size_t>(surf->pitch) * surf_h);
    const uint32_t rgba_pixel =
        (static_cast<uint32_t>(color.r)) |
        (static_cast<uint32_t>(color.g) << 8) |
        (static_cast<uint32_t>(color.b) << 16) |
        (static_cast<uint32_t>(color.a) << 24);

    for (int ci = 0; ci < static_cast<int>(text.size()); ++ci) {
        unsigned char ch = static_cast<unsigned char>(text[ci]);
        if (ch < 32 || ch > 127) ch = '?';
        const uint8_t* glyph = kGlyph5x7[ch - 32];
        for (int row = 0; row < 7; ++row) {
            const uint8_t bits = glyph[row];
            for (int col = 0; col < 5; ++col) {
                if ((bits >> (4 - col)) & 1u) {
                    for (int sy = 0; sy < scale; ++sy) {
                        uint32_t* dst_row = reinterpret_cast<uint32_t*>(
                            static_cast<uint8_t*>(surf->pixels) + (row * scale + sy) * surf->pitch);
                        for (int sx = 0; sx < scale; ++sx) {
                            dst_row[ci * char_w + col * scale + sx] = rgba_pixel;
                        }
                    }
                }
            }
        }
    }
    return surf;
}

} // namespace

TextureCache::TextureCache(SDL_Renderer* renderer, std::size_t max_bytes)
    : renderer_(renderer), max_bytes_(max_bytes) {}

TextureCache::~TextureCache() {
    ClearAll();
    auto& libs = GetDynLibs();
    for (int i = 0; i < 5; ++i) {
        if (fonts_[i] && libs.p_TTF_CloseFont) {
            libs.p_TTF_CloseFont(fonts_[i]);
            fonts_[i] = nullptr;
        }
    }
    if (libs.p_TTF_Quit) {
        libs.p_TTF_Quit();
    }
}

bool TextureCache::InitFonts(const RuntimeConfig& cfg) {
    auto& libs = GetDynLibs();
    if (!libs.p_TTF_Init || !libs.p_TTF_OpenFont) {
        active_font_path_ = "Built-in Vector/Bitmap Fallback (libSDL2_ttf not loaded)";
        return true;
    }

    if (libs.p_TTF_Init() < 0) {
        active_font_path_ = "Built-in Vector/Bitmap Fallback (TTF_Init failed)";
        return true;
    }

    // Search verified font locations on PlayStation Classic & Project Eris
    const std::vector<std::string> candidate_fonts = {
        "assets/fonts/Cabin-SemiBold.ttf",
        "assets/fonts/Cabin-Regular.ttf",
        cfg.project_eris_path + "/opt/classic_os/assets/fonts/Cabin-SemiBold.ttf",
        cfg.themes_path + "/" + cfg.selected_theme + "/menu_files/Font/Cabin-SemiBold.ttf",
        cfg.themes_path + "/modmyclassic/menu_files/Font/Cabin-SemiBold.ttf",
        cfg.project_eris_path + "/opt/emulationstation/.emulationstation/themes/simple/art/OPENSANS.TTF",
        "/usr/sony/share/data/font/SSTJapanese-Regular.ttf",
        "/usr/share/fonts/ttf/LiberationMono-Regular.ttf"
    };

    for (const auto& path : candidate_fonts) {
        if (FileExists(path)) {
            active_font_path_ = path;
            break;
        }
    }

    if (active_font_path_.empty()) {
        active_font_path_ = "Built-in Vector/Bitmap Fallback (no .ttf file found)";
        return true;
    }

    const int sizes[5] = {14, 18, 22, 28, 36};
    for (int i = 0; i < 5; ++i) {
        fonts_[i] = libs.p_TTF_OpenFont(active_font_path_.c_str(), sizes[i]);
    }
    return true;
}

void TextureCache::ClearAll() {
    for (auto& kv : map_) {
        if (kv.second.data.texture) {
            SDL_DestroyTexture(kv.second.data.texture);
        }
    }
    map_.clear();
    lru_.clear();
    used_bytes_ = 0;
}

SDL_Surface* TextureCache::LoadPngViaLibpng16(const std::string& path) {
    png_image image{};
    image.version = kPngImageVersion;
    if (png_image_begin_read_from_file(&image, path.c_str()) == 0) {
        return nullptr;
    }
    image.format = kPngFormatRgba;
    const int w = static_cast<int>(image.width);
    const int h = static_cast<int>(image.height);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        png_image_free(&image);
        return nullptr;
    }

    SDL_Surface* surf = SDL_CreateRGBSurface(
        0, w, h, 32,
        0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
    if (!surf) {
        png_image_free(&image);
        return nullptr;
    }

    if (png_image_finish_read(&image, nullptr, surf->pixels, surf->pitch, nullptr) == 0) {
        SDL_FreeSurface(surf);
        png_image_free(&image);
        return nullptr;
    }
    png_image_free(&image);
    return surf;
}

const CachedTexture* TextureCache::GetOrLoadImage(const std::string& path) {
    if (path.empty() || !renderer_) return nullptr;
    const std::string key = "IMG:" + path;
    auto it = map_.find(key);
    if (it != map_.end()) {
        lru_.erase(it->second.lru_it);
        lru_.push_front(key);
        it->second.lru_it = lru_.begin();
        return &it->second.data;
    }

    SDL_Surface* surf = LoadPngViaLibpng16(path);
    if (!surf) {
        auto& libs = GetDynLibs();
        if (libs.p_IMG_Load) {
            surf = libs.p_IMG_Load(path.c_str());
        }
    }
    if (!surf) {
        return nullptr;
    }

    const int w = surf->w;
    const int h = surf->h;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_FreeSurface(surf);
    if (!tex) {
        return nullptr;
    }
    return InsertTexture(key, tex, w, h);
}

const CachedTexture* TextureCache::GetText(const std::string& text, int font_tier, SDL_Color color, Uint32 wrap_width) {
    if (text.empty() || !renderer_) return nullptr;
    if (font_tier < 0) font_tier = 0;
    if (font_tier > 4) font_tier = 4;

    char key_buf[128];
    std::snprintf(
        key_buf, sizeof(key_buf), "TXT:%d:%u:%02x%02x%02x%02x:",
        font_tier, wrap_width, color.r, color.g, color.b, color.a);
    const std::string key = std::string(key_buf) + text;

    auto it = map_.find(key);
    if (it != map_.end()) {
        lru_.erase(it->second.lru_it);
        lru_.push_front(key);
        it->second.lru_it = lru_.begin();
        return &it->second.data;
    }

    SDL_Surface* surf = nullptr;
    auto& libs = GetDynLibs();
    if (fonts_[font_tier] && libs.p_TTF_RenderUTF8_Blended_Wrapped) {
        const Uint32 wrap = (wrap_width > 0) ? wrap_width : 1200u;
        surf = libs.p_TTF_RenderUTF8_Blended_Wrapped(fonts_[font_tier], text.c_str(), color, wrap);
    }
    if (!surf) {
        surf = RenderSoftwareBitmapText(text, font_tier, color);
    }
    if (!surf) {
        return nullptr;
    }

    const int w = surf->w;
    const int h = surf->h;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    SDL_FreeSurface(surf);
    if (!tex) {
        return nullptr;
    }
    return InsertTexture(key, tex, w, h);
}

const CachedTexture* TextureCache::InsertTexture(const std::string& key, SDL_Texture* tex, int w, int h) {
    const std::size_t tex_bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
    EvictToFit(tex_bytes);

    lru_.push_front(key);
    CacheEntry entry{};
    entry.data.texture = tex;
    entry.data.width   = w;
    entry.data.height  = h;
    entry.data.bytes   = tex_bytes;
    entry.lru_it       = lru_.begin();

    auto res = map_.emplace(key, entry);
    used_bytes_ += tex_bytes;
    return &res.first->second.data;
}

void TextureCache::EvictToFit(std::size_t incoming_bytes) {
    while (!lru_.empty() && (used_bytes_ + incoming_bytes > max_bytes_)) {
        const std::string& oldest_key = lru_.back();
        auto it = map_.find(oldest_key);
        if (it != map_.end()) {
            if (it->second.data.texture) {
                SDL_DestroyTexture(it->second.data.texture);
            }
            if (used_bytes_ >= it->second.data.bytes) {
                used_bytes_ -= it->second.data.bytes;
            } else {
                used_bytes_ = 0;
            }
            map_.erase(it);
        }
        lru_.pop_back();
    }
}

} // namespace classic_os
