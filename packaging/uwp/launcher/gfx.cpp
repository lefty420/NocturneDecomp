#include "gfx.h"
#include "font_data.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "third_party/stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace launcher {

struct FontFace {
    stbtt_fontinfo info;
};

Color Color::scaled(float k) const { return Color(r, g, b, (Uint8)std::clamp(a * k, 0.f, 255.f)); }

Color mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto l = [t](Uint8 x, Uint8 y) { return (Uint8)(x + (y - x) * t + 0.5f); };
    return Color(l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a));
}

static constexpr int kTex = 96;      // nine-slice texture edge
static constexpr int kCorner = 32;   // corner radius baked into it

// Signed distance to a rounded square of half-size h, corner radius r, centred at 0.
static float sdRoundBox(float px, float py, float hx, float hy, float r) {
    float qx = std::fabs(px) - (hx - r), qy = std::fabs(py) - (hy - r);
    float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}

bool Gfx::init(SDL_Renderer *renderer) {
    r_ = renderer;
    for (int i = 0; i < 2; i++) {
        faces_[i] = new FontFace();
        const unsigned char *d = i == 0 ? kFontRegular : kFontMedium;
        if (!stbtt_InitFont(&faces_[i]->info, d, stbtt_GetFontOffsetForIndex(d, 0))) return false;
    }
    fill_ = makeNineSlice(0);
    ring_ = makeNineSlice(1);
    shadow_ = makeNineSlice(2);
    // Vignette: a 128x128 radial alpha ramp stretched over the screen.
    {
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 128, 128, 32, SDL_PIXELFORMAT_RGBA32);
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++) {
                float dx = (x + .5f) / 128 * 2 - 1, dy = (y + .5f) / 128 * 2 - 1;
                float d = std::sqrt(dx * dx + dy * dy) / 1.414f;
                float a = std::clamp((d - 0.35f) / 0.65f, 0.f, 1.f);
                a = a * a;
                ((Uint32 *)((Uint8 *)s->pixels + y * s->pitch))[x] = SDL_MapRGBA(s->format, 0, 0, 0, (Uint8)(a * 255));
            }
        vign_ = SDL_CreateTextureFromSurface(r_, s);
        SDL_SetTextureBlendMode(vign_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(vign_, SDL_ScaleModeLinear);
        SDL_FreeSurface(s);
    }
    return fill_ && ring_ && shadow_ && vign_;
}

Gfx::~Gfx() {
    for (auto &kv : glyphs_) if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    for (SDL_Texture *t : {fill_, ring_, shadow_, vign_}) if (t) SDL_DestroyTexture(t);
    for (FontFace *f : faces_) delete f;
}

SDL_Texture *Gfx::makeNineSlice(int kind) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, kTex, kTex, 32, SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    const float half = kTex / 2.f;
    for (int y = 0; y < kTex; y++)
        for (int x = 0; x < kTex; x++) {
            // 4x4 supersampling for clean edges
            float acc = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float px = x + (sx + .5f) / 4 - half, py = y + (sy + .5f) / 4 - half;
                    float sd = sdRoundBox(px, py, half, half, kCorner);
                    if (kind == 0) acc += std::clamp(0.5f - sd, 0.f, 1.f);
                    else if (kind == 1) acc += std::clamp(0.5f - (std::fabs(sd + 1.5f) - 1.5f), 0.f, 1.f);
                    else {  // soft shadow: smooth falloff outside the shape
                        float t = std::clamp(1.f - (sd + 6.f) / 30.f, 0.f, 1.f);
                        acc += t * t * (3 - 2 * t);
                    }
                }
            acc /= 16.f;
            ((Uint32 *)((Uint8 *)s->pixels + y * s->pitch))[x] = SDL_MapRGBA(s->format, 255, 255, 255, (Uint8)(acc * 255 + .5f));
        }
    SDL_Texture *t = SDL_CreateTextureFromSurface(r_, s);
    SDL_FreeSurface(s);
    if (t) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    }
    return t;
}

void Gfx::slice(SDL_Texture *t, int srcSize, int srcCorner, float x, float y, float w, float h, float corner, Color c) {
    if (w <= 0 || h <= 0) return;
    corner = std::min(corner, std::min(w, h) / 2);
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    const int sc = srcCorner, sm = srcSize - 2 * srcCorner;
    const float xs[4] = {x, x + corner, x + w - corner, x + w};
    const float ys[4] = {y, y + corner, y + h - corner, y + h};
    const int sxs[4] = {0, sc, sc + sm, srcSize};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            SDL_Rect src{sxs[i], sxs[j], sxs[i + 1] - sxs[i], sxs[j + 1] - sxs[j]};
            SDL_FRect dst{xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j]};
            if (dst.w <= 0 || dst.h <= 0) continue;
            SDL_RenderCopyF(r_, t, &src, &dst);
        }
}

void Gfx::box(float x, float y, float w, float h, Color c, float radius) { slice(fill_, kTex, kCorner, x, y, w, h, radius, c); }
void Gfx::ring(float x, float y, float w, float h, Color c, float radius) { slice(ring_, kTex, kCorner, x, y, w, h, radius, c); }
void Gfx::shadow(float x, float y, float w, float h, Color c, float radius, float spread) {
    slice(shadow_, kTex, kCorner, x - spread, y - spread, w + 2 * spread, h + 2 * spread, radius + spread, c);
}

void Gfx::rect(float x, float y, float w, float h, Color c) {
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_FRect f{x, y, w, h};
    SDL_RenderFillRectF(r_, &f);
}

void Gfx::gradientV(float x, float y, float w, float h, Color t, Color b) {
    SDL_Vertex v[4];
    auto mk = [](float px, float py, Color c) {
        SDL_Vertex vv; vv.position = {px, py}; vv.color = {c.r, c.g, c.b, c.a}; vv.tex_coord = {0, 0}; return vv;
    };
    v[0] = mk(x, y, t); v[1] = mk(x + w, y, t); v[2] = mk(x + w, y + h, b); v[3] = mk(x, y + h, b);
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r_, nullptr, v, 4, idx, 6);
}

void Gfx::vignette(float w, float h, Uint8 strength) {
    SDL_SetTextureAlphaMod(vign_, strength);
    SDL_FRect d{0, 0, w, h};
    SDL_RenderCopyF(r_, vign_, nullptr, &d);
}

// ---- text -----------------------------------------------------------------------------------

static unsigned next_cp(const std::string &s, size_t &i) {
    unsigned char c = s[i++];
    if (c < 0x80) return c;
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    unsigned cp = c & (0x3F >> n);
    while (n-- && i < s.size()) cp = (cp << 6) | (s[i++] & 0x3F);
    return cp;
}

const Gfx::Glyph &Gfx::glyph(Weight w, int px, unsigned cp) {
    unsigned long long key = ((unsigned long long)(int)w << 40) | ((unsigned long long)px << 32) | cp;
    auto it = glyphs_.find(key);
    if (it != glyphs_.end()) return it->second;
    Glyph g;
    stbtt_fontinfo &fi = faces_[(int)w]->info;
    float scale = stbtt_ScaleForPixelHeight(&fi, (float)px);
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&fi, (int)cp, &adv, &lsb);
    g.adv = adv * scale;
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&fi, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
    g.w = x1 - x0; g.h = y1 - y0; g.xoff = x0; g.yoff = y0;
    if (g.w > 0 && g.h > 0) {
        std::vector<unsigned char> bm((size_t)g.w * g.h);
        stbtt_MakeCodepointBitmap(&fi, bm.data(), g.w, g.h, g.w, scale, scale, (int)cp);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, g.w, g.h, 32, SDL_PIXELFORMAT_RGBA32);
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++)
                ((Uint32 *)((Uint8 *)s->pixels + y * s->pitch))[x] = SDL_MapRGBA(s->format, 255, 255, 255, bm[(size_t)y * g.w + x]);
        g.tex = SDL_CreateTextureFromSurface(r_, s);
        SDL_FreeSurface(s);
        if (g.tex) { SDL_SetTextureBlendMode(g.tex, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(g.tex, SDL_ScaleModeLinear); }
    }
    return glyphs_.emplace(key, g).first->second;
}

float Gfx::measure(const std::string &s, int px, Weight wgt) {
    float x = 0;
    stbtt_fontinfo &fi = faces_[(int)wgt]->info;
    float scale = stbtt_ScaleForPixelHeight(&fi, (float)px);
    unsigned prev = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned cp = next_cp(s, i);
        if (prev) x += stbtt_GetCodepointKernAdvance(&fi, (int)prev, (int)cp) * scale;
        x += glyph(wgt, px, cp).adv;
        prev = cp;
    }
    return x;
}

float Gfx::text(const std::string &s, float x, float y, int px, Color c, Align a, Weight wgt) {
    float w = measure(s, px, wgt);
    if (a == Align::Center) x -= w / 2;
    else if (a == Align::Right) x -= w;
    stbtt_fontinfo &fi = faces_[(int)wgt]->info;
    float scale = stbtt_ScaleForPixelHeight(&fi, (float)px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&fi, &asc, &desc, &gap);
    // Centre the glyph box (ascent..descent) in a line of lineHeight(px).
    float base = y + (lineHeight(px) - (asc - desc) * scale) / 2 + asc * scale;
    float cx = x;
    unsigned prev = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned cp = next_cp(s, i);
        if (prev) cx += stbtt_GetCodepointKernAdvance(&fi, (int)prev, (int)cp) * scale;
        const Glyph &g = glyph(wgt, px, cp);
        if (g.tex) {
            SDL_SetTextureColorMod(g.tex, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(g.tex, c.a);
            SDL_FRect d{std::floor(cx + g.xoff), std::floor(base + g.yoff), (float)g.w, (float)g.h};
            SDL_RenderCopyF(r_, g.tex, nullptr, &d);
        }
        cx += g.adv;
        prev = cp;
    }
    return w;
}

int Gfx::paragraph(const std::string &s, float x, float y, float max_w, int px, Color c, Weight wgt, int maxLines) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(trial, px, wgt) > max_w) { lines.push_back(line); line = word; }
        else line = trial;
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') flushWord();
        else if (ch == '\n') { flushWord(); lines.push_back(line); line.clear(); }
        else word += ch;
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    int n = 0;
    for (auto &l : lines) {
        if (maxLines > 0 && n >= maxLines) break;
        text(l, x, y + n * lineHeight(px), px, c, Align::Left, wgt);
        n++;
    }
    return n;
}

}  // namespace launcher
