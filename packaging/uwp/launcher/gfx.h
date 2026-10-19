#pragma once
// Small 2D toolkit on top of SDL_Renderer: anti-aliased rounded boxes, soft shadows,
// gradients and TrueType text (stb_truetype, embedded Poppins). No other dependencies,
// so it behaves the same on Linux (for testing) and on UWP/Xbox (SDL's D3D11 renderer).
#include <SDL.h>
#include <string>
#include <unordered_map>

namespace launcher {

struct Color {
    Uint8 r, g, b, a;
    constexpr Color(Uint8 r_ = 0, Uint8 g_ = 0, Uint8 b_ = 0, Uint8 a_ = 255) : r(r_), g(g_), b(b_), a(a_) {}
    Color alpha(Uint8 na) const { return Color(r, g, b, na); }
    Color scaled(float k) const;  // multiply alpha
};
Color mix(Color a, Color b, float t);

enum class Align { Left, Center, Right };
enum class Weight { Regular, Medium };

struct FontFace;  // opaque

class Gfx {
public:
    Gfx() = default;
    ~Gfx();
    bool init(SDL_Renderer *renderer);

    // Filled / outlined rounded boxes (anti-aliased via pre-rendered nine-slice textures).
    void box(float x, float y, float w, float h, Color c, float radius = 14);
    void ring(float x, float y, float w, float h, Color c, float radius = 14);
    void shadow(float x, float y, float w, float h, Color c, float radius = 14, float spread = 28);
    void rect(float x, float y, float w, float h, Color c);
    void gradientV(float x, float y, float w, float h, Color top, Color bottom);
    void vignette(float w, float h, Uint8 strength);

    // Text. y is the top of the line box. Returns the drawn width.
    float text(const std::string &s, float x, float y, int px, Color c, Align a = Align::Left,
               Weight wgt = Weight::Regular);
    float measure(const std::string &s, int px, Weight wgt = Weight::Regular);
    int lineHeight(int px) const { return px * 3 / 2; }
    // Word-wraps into max_w; returns the number of lines drawn. maxLines<=0 means unlimited.
    int paragraph(const std::string &s, float x, float y, float max_w, int px, Color c,
                  Weight wgt = Weight::Regular, int maxLines = 0);

    SDL_Renderer *renderer() { return r_; }

private:
    struct Glyph { SDL_Texture *tex = nullptr; int w = 0, h = 0, xoff = 0, yoff = 0; float adv = 0; };
    const Glyph &glyph(Weight w, int px, unsigned cp);
    SDL_Texture *makeNineSlice(int kind);
    void slice(SDL_Texture *t, int srcSize, int srcCorner, float x, float y, float w, float h, float corner, Color c);

    SDL_Renderer *r_ = nullptr;
    SDL_Texture *fill_ = nullptr, *ring_ = nullptr, *shadow_ = nullptr, *vign_ = nullptr;
    FontFace *faces_[2] = {nullptr, nullptr};
    std::unordered_map<unsigned long long, Glyph> glyphs_;
};

}  // namespace launcher
