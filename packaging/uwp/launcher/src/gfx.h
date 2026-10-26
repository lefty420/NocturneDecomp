#pragma once
// Small drawing helpers on top of SDL_Renderer: smooth rounded rectangles, circles,
// gradients and text. Everything is laid out in a fixed 1920x1080 design space; the
// renderer scale maps that to the real output, and text is rasterised at the real
// pixel size so it stays sharp on a 4K display.

#include <SDL.h>

#include <string>
#include <unordered_map>

namespace nl {

constexpr int kDesignW = 1920;
constexpr int kDesignH = 1080;

struct Color {
    Uint8 r, g, b, a;
    constexpr Color(Uint8 r_, Uint8 g_, Uint8 b_, Uint8 a_ = 255) : r(r_), g(g_), b(b_), a(a_) {}
    Color alpha(float f) const { return Color(r, g, b, static_cast<Uint8>(a * f)); }
    Color mix(const Color &o, float t) const {
        auto m = [t](Uint8 x, Uint8 y) { return static_cast<Uint8>(x + (y - x) * t); };
        return Color(m(r, o.r), m(g, o.g), m(b, o.b), m(a, o.a));
    }
};

struct Rect {
    float x, y, w, h;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};

enum class FontFace { Medium, Bold };

class Gfx {
public:
    // `fonts_dir` holds SpecialElite-Regular.ttf and IMFELLDoublePica-Regular.ttf.
    bool init(SDL_Renderer *r, const std::string &fonts_dir);
    void shutdown();

    // Call when the output size changes (and once at start).
    void set_output_size(int w, int h);
    float scale() const { return scale_; }

    void clear_background();   // the dark gradient with a soft vignette
    void rrect(const Rect &r, float radius, Color c);          // corners are squared off by the theme
    void rrect_exact(const Rect &r, float radius, Color c);    // the radius as given (circles, pills)
    void rrect_outline_exact(const Rect &r, float radius, float thickness, Color c);
    void rrect_outline(const Rect &r, float radius, float thickness, Color c);
    void circle(float cx, float cy, float radius, Color c);
    void ring(float cx, float cy, float radius, float thickness, Color c);
    void line(float x1, float y1, float x2, float y2, float thickness, Color c);
    void quad_gradient_v(const Rect &r, Color top, Color bottom);

    // Text. `px` is the design-space pixel height; `spacing` extra letter spacing.
    float text_width(const std::string &s, int px, FontFace f = FontFace::Medium, float spacing = 0);
    void text(float x, float y, const std::string &s, int px, Color c, FontFace f = FontFace::Medium,
              float spacing = 0);                              // x,y = top-left
    void text_center(float cx, float y, const std::string &s, int px, Color c,
                     FontFace f = FontFace::Medium, float spacing = 0);
    void text_right(float rx, float y, const std::string &s, int px, Color c,
                    FontFace f = FontFace::Medium, float spacing = 0);
    // Shortens `s` with an ellipsis so it fits in `max_w`.
    std::string fit(const std::string &s, float max_w, int px, FontFace f = FontFace::Medium);

    // A 24/32-bit .bmp from disk as a texture (SDL reads these itself, no image library needed).
    // Null when the file is missing or unreadable. The caller destroys the texture.
    SDL_Texture *load_bmp(const std::string &path, int *w, int *h);
    // Draws `tex` scaled into `dst` (design-space rectangle).
    void image(SDL_Texture *tex, const Rect &dst);
    void image_tinted(SDL_Texture *tex, const Rect &dst, Color tint);   // colour and alpha multiply the picture
    void draw_grain(float strength);                                    // film grain over everything drawn so far

    SDL_Renderer *renderer() const { return r_; }

private:
    struct Impl;
    SDL_Renderer *r_ = nullptr;
    float scale_ = 1.0f;
    Impl *impl_ = nullptr;
};

}  // namespace nl
