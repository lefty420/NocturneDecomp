// Small 2D drawing layer for the launcher: text (stb_truetype), rounded boxes,
// gradients and procedurally drawn icons, all on top of SDL_Renderer so it runs
// unchanged on desktop and on UWP (where SDL_Renderer is Direct3D 11).
#pragma once
#include <SDL.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nl {

struct Color {
    uint8_t r, g, b, a;
};
constexpr Color rgb(int r, int g, int b, int a = 255) {
    return Color{(uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a};
}
inline Color with_alpha(Color c, int a) { return Color{c.r, c.g, c.b, (uint8_t)a}; }

enum class Weight { Regular = 0, Medium = 1, Bold = 2 };

enum class Icon { Check, Cross, Play, Folder, Sliders, Info, ChevLeft, ChevRight, Drive, Refresh, Warning, Gamepad };

class Gfx {
public:
    ~Gfx() { shutdown(); }

    // Loads Poppins-{Regular,Medium,Bold}.ttf from `font_dir`. False if one is missing.
    bool init(SDL_Renderer *renderer, const std::string &font_dir);
    void shutdown();

    // Physical pixels per logical pixel (output height / logical height). Text and
    // icons are rasterised at physical size so they stay sharp at 4K.
    void set_scale(float physical_per_logical);
    float scale() const { return scale_; }

    // Text. `y` is the top of the line box; sizes are logical pixels.
    int text_width(const std::string &s, int px, Weight w, int spacing = 0);
    int line_height(int px, Weight w);
    void text(const std::string &s, int x, int y, int px, Weight w, Color c, int spacing = 0);
    void text_right(const std::string &s, int right, int y, int px, Weight w, Color c, int spacing = 0);
    void text_center(const std::string &s, int cx, int y, int px, Weight w, Color c, int spacing = 0);
    std::vector<std::string> wrap(const std::string &s, int px, Weight w, int max_w);

    // Shapes (logical pixels).
    void rect(int x, int y, int w, int h, Color c);
    void round_rect(int x, int y, int w, int h, int r, Color c);
    void circle(int cx, int cy, int r, Color c);
    void gradient_v(int x, int y, int w, int h, Color top, Color bottom);
    void glow(int cx, int cy, int radius, Color c);
    void crescent(int cx, int cy, int r, Color c);
    void icon(Icon i, int x, int y, int size, Color c);

    SDL_Renderer *renderer() const { return r_; }

private:
    struct Atlas {
        SDL_Texture *tex = nullptr;
        int w = 0, h = 0;
        float ascent = 0, descent = 0, line_gap = 0;
        std::vector<unsigned char> baked;   // stbtt_bakedchar[96]
    };
    struct FontData;
    Atlas *atlas(Weight w, int px_physical);
    SDL_Texture *corner_texture(int r_physical);
    SDL_Texture *soft_circle_texture();
    SDL_Texture *crescent_texture();
    SDL_Texture *icon_texture(Icon i);
    void free_caches();

    SDL_Renderer *r_ = nullptr;
    float scale_ = 1.0f;
    std::vector<FontData *> fonts_;
    std::map<int, Atlas *> atlases_;   // key: weight * 100000 + px
    std::map<int, SDL_Texture *> corners_;
    std::map<int, SDL_Texture *> icons_;
    SDL_Texture *soft_ = nullptr;
    SDL_Texture *crescent_ = nullptr;
};

}  // namespace nl
