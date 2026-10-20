// =============================================================================
// GFX — the few drawing primitives the launcher needs, on SDL_Renderer
// =============================================================================
//
// SDL_Renderer is used (not the game's OpenGL path) so the launcher runs before
// the game's renderer exists, and on UWP it rides SDL's Direct3D 11 backend.
// Text is rasterised by stb_truetype into an alpha atlas per font size.
#pragma once
#include <SDL.h>
#include <string>
#include <vector>

namespace launcher {

struct Color { Uint8 r, g, b, a; };
inline Color rgba(unsigned hex, Uint8 a = 255) { return {(Uint8)(hex >> 16), (Uint8)(hex >> 8), (Uint8)hex, a}; }
inline Color with_alpha(Color c, Uint8 a) { c.a = a; return c; }
Color mix(Color a, Color b, float t);

class Font {
public:
    Font() {}
    ~Font();
    // `ttf` must outlive the Font. `px` is the pixel height.
    bool init(SDL_Renderer *r, const unsigned char *ttf, float px);
    // Width of `s` in pixels, with extra spacing between letters.
    float width(const std::string &s, float tracking = 0) const;
    float lineHeight() const { return lineH_; }
    float ascent() const { return ascent_; }
    // y is the top of the line.
    void draw(SDL_Renderer *r, const std::string &s, float x, float y, Color c, float tracking = 0) const;
    // Greedy word wrap into lines no wider than maxW.
    std::vector<std::string> wrap(const std::string &s, float maxW) const;

private:
    struct Glyph { float x0, y0, x1, y1; float u0, v0, u1, v1; float adv; bool ok; };
    SDL_Texture *tex_ = nullptr;
    Glyph glyphs_[96 + 8];
    float lineH_ = 0, ascent_ = 0;
    int glyphIndex(unsigned cp) const;
};

void fill_rect(SDL_Renderer *r, float x, float y, float w, float h, Color c);
void fill_round_rect(SDL_Renderer *r, float x, float y, float w, float h, float radius, Color c);
void stroke_round_rect(SDL_Renderer *r, float x, float y, float w, float h, float radius, float thickness, Color c);
// Vertical gradient from `top` to `bottom`.
void fill_gradient(SDL_Renderer *r, float x, float y, float w, float h, Color top, Color bottom);
void fill_circle(SDL_Renderer *r, float cx, float cy, float radius, Color c);
// A soft dark edge around the whole screen.
void draw_vignette(SDL_Renderer *r, int w, int h, Uint8 strength);

// Small glyphs drawn as shapes so they do not depend on the font having them.
void draw_check(SDL_Renderer *r, float cx, float cy, float size, Color c);
void draw_cross(SDL_Renderer *r, float cx, float cy, float size, Color c);
void draw_dash(SDL_Renderer *r, float cx, float cy, float size, Color c);
void draw_chevron(SDL_Renderer *r, float cx, float cy, float size, bool right, Color c);

}  // namespace launcher
