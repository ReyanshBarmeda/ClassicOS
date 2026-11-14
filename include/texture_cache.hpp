#ifndef CLASSIC_OS_TEXTURE_CACHE_HPP
#define CLASSIC_OS_TEXTURE_CACHE_HPP

#include "sdl_sqlite_abi.hpp"
#include "config.hpp"
#include <list>
#include <string>
#include <unordered_map>

namespace classic_os {

struct CachedTexture {
    SDL_Texture* texture = nullptr;
    int width            = 0;
    int height           = 0;
    std::size_t bytes    = 0;
};

class TextureCache {
public:
    TextureCache(SDL_Renderer* renderer, std::size_t max_bytes);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    bool InitFonts(const RuntimeConfig& cfg);
    void ClearAll();

    // Image texture LRU (loads via libpng16 / SDL2_image and evicts oldest when over budget)
    const CachedTexture* GetOrLoadImage(const std::string& path);

    // Text texture LRU (renders UTF-8 via SDL2_ttf)
    const CachedTexture* GetText(const std::string& text, int font_tier, SDL_Color color, Uint32 wrap_width = 0);

    std::size_t used_bytes() const { return used_bytes_; }
    std::size_t max_bytes() const { return max_bytes_; }
    std::size_t cached_count() const { return map_.size(); }
    const std::string& active_font_path() const { return active_font_path_; }

private:
    using LruList = std::list<std::string>;
    struct CacheEntry {
        CachedTexture data;
        LruList::iterator lru_it;
    };

    const CachedTexture* InsertTexture(const std::string& key, SDL_Texture* tex, int w, int h);
    void EvictToFit(std::size_t incoming_bytes);
    SDL_Surface* LoadPngViaLibpng16(const std::string& path);

    SDL_Renderer* renderer_ = nullptr;
    std::size_t max_bytes_  = 32u * 1024u * 1024u;
    std::size_t used_bytes_ = 0;

    LruList lru_;
    std::unordered_map<std::string, CacheEntry> map_;

    // Font tiers: 0=Small(14pt), 1=Body(18pt), 2=Subhead(22pt), 3=Title(30pt), 4=Hero(38pt)
    TTF_Font* fonts_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    std::string active_font_path_;
};

} // namespace classic_os

#endif // CLASSIC_OS_TEXTURE_CACHE_HPP
