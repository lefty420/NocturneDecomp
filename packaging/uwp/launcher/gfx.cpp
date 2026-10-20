#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace launcher {

Color mix(Color a, Color b, float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return {(Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t),
            (Uint8)(a.b + (b.b - a.b) * t), (Uint8)(a.a + (b.a - a.a) * t)};
}

// ---------------------------------------------------------------- font

static const unsigned kExtra[8] = {0x2022, 0x2026, 0x2014, 0x00B7, 0x2013, 0x00A9, 0x2192, 0x00D7};

Font::~Font() {
    if (tex_) SDL_DestroyTexture(tex_);
}

int Font::glyphIndex(unsigned cp) const {
    if (cp >= 32 && cp < 127) return (int)cp - 32;
    for (int i = 0; i < 8; ++i)
        if (kExtra[i] == cp) return 96 + i;
    return (int)'?' - 32;
}

bool Font::init(SDL_Renderer *r, const unsigned char *ttf, float px) {
    const int W = 1024, H = 1024;
    std::vector<unsigned char> alpha((size_t)W * H, 0);
    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, alpha.data(), W, H, 0, 1, nullptr)) return false;
    int over = px > 40 ? 1 : 2;   // big sizes need the atlas space more than the smoothing
    stbtt_PackSetOversampling(&pc, over, over);
    stbtt_packedchar base[96];
    stbtt_packedchar extra[8];
    stbtt_pack_range ranges[9];
    std::memset(ranges, 0, sizeof ranges);
    ranges[0].font_size = px;
    ranges[0].first_unicode_codepoint_in_range = 32;
    ranges[0].num_chars = 96;
    ranges[0].chardata_for_range = base;
    for (int i = 0; i < 8; ++i) {
        ranges[1 + i].font_size = px;
        ranges[1 + i].first_unicode_codepoint_in_range = (int)kExtra[i];
        ranges[1 + i].num_chars = 1;
        ranges[1 + i].chardata_for_range = &extra[i];
    }
    if (!stbtt_PackFontRanges(&pc, ttf, 0, ranges, 9)) {
        stbtt_PackEnd(&pc);
        return false;
    }
    stbtt_PackEnd(&pc);

    stbtt_fontinfo info;
    stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0));
    float scale = stbtt_ScaleForPixelHeight(&info, px);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    ascent_ = asc * scale;
    lineH_ = (asc - desc + gap) * scale;

    for (int i = 0; i < 96 + 8; ++i) {
        const stbtt_packedchar &p = i < 96 ? base[i] : extra[i - 96];
        Glyph &g = glyphs_[i];
        g.x0 = p.xoff; g.y0 = p.yoff; g.x1 = p.xoff2; g.y1 = p.yoff2;
        g.u0 = p.x0; g.v0 = p.y0; g.u1 = p.x1; g.v1 = p.y1;
        g.adv = p.xadvance;
        g.ok = true;
    }

    // Expand the alpha atlas to white-with-alpha so colour modulation works.
    std::vector<Uint32> px32((size_t)W * H);
    for (size_t i = 0; i < px32.size(); ++i) px32[i] = ((Uint32)alpha[i] << 24) | 0x00FFFFFFu;
    tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, W, H);
    if (!tex_) return false;
    SDL_UpdateTexture(tex_, nullptr, px32.data(), W * 4);
    SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex_, SDL_ScaleModeLinear);
    return true;
}

// Decode one UTF-8 code point; advances `i`.
static unsigned next_cp(const std::string &s, size_t &i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = (c >> 5) == 6 ? 1 : ((c >> 4) == 14 ? 2 : ((c >> 3) == 30 ? 3 : 0));
    unsigned cp = extra == 1 ? (c & 0x1F) : (extra == 2 ? (c & 0x0F) : (c & 0x07));
    while (extra-- > 0 && i < s.size()) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}

float Font::width(const std::string &s, float tracking) const {
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        const Glyph &g = glyphs_[glyphIndex(next_cp(s, i))];
        w += g.adv + tracking;
    }
    return w > 0 ? w - tracking : 0;
}

void Font::draw(SDL_Renderer *r, const std::string &s, float x, float y, Color c, float tracking) const {
    if (!tex_ || c.a == 0) return;
    SDL_SetTextureColorMod(tex_, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(tex_, c.a);
    float cx = x, base = y + ascent_;
    for (size_t i = 0; i < s.size();) {
        const Glyph &g = glyphs_[glyphIndex(next_cp(s, i))];
        SDL_FRect dst{cx + g.x0, base + g.y0, g.x1 - g.x0, g.y1 - g.y0};
        SDL_Rect src{(int)g.u0, (int)g.v0, (int)(g.u1 - g.u0), (int)(g.v1 - g.v0)};
        if (src.w > 0 && src.h > 0) SDL_RenderCopyF(r, tex_, &src, &dst);
        cx += g.adv + tracking;
    }
}

std::vector<std::string> Font::wrap(const std::string &s, float maxW) const {
    std::vector<std::string> lines;
    std::istringstream words(s);
    std::string word, line;
    while (words >> word) {
        std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && width(t) > maxW) {
            lines.push_back(line);
            line = word;
        } else {
            line = t;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// ---------------------------------------------------------------- shapes

static SDL_Vertex vert(float x, float y, Color c) {
    SDL_Vertex v;
    v.position = {x, y};
    v.color = {c.r, c.g, c.b, c.a};
    v.tex_coord = {0, 0};
    return v;
}

void fill_rect(SDL_Renderer *r, float x, float y, float w, float h, Color c) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rc{x, y, w, h};
    SDL_RenderFillRectF(r, &rc);
}

void fill_gradient(SDL_Renderer *r, float x, float y, float w, float h, Color top, Color bottom) {
    SDL_Vertex v[4] = {vert(x, y, top), vert(x + w, y, top), vert(x + w, y + h, bottom), vert(x, y + h, bottom)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
}

static void round_outline(std::vector<SDL_FPoint> &pts, float x, float y, float w, float h, float rad, int seg) {
    rad = std::min(rad, std::min(w, h) / 2);
    // Clockwise from the top-right corner (screen y points down).
    const float cx[4] = {x + w - rad, x + w - rad, x + rad, x + rad};
    const float cy[4] = {y + rad, y + h - rad, y + h - rad, y + rad};
    for (int corner = 0; corner < 4; ++corner) {
        float a0 = (float)(corner * M_PI / 2 - M_PI / 2);
        for (int i = 0; i <= seg; ++i) {
            float a = a0 + (float)(M_PI / 2) * i / seg;
            pts.push_back({cx[corner] + std::cos(a) * rad, cy[corner] + std::sin(a) * rad});
        }
    }
}

void fill_round_rect(SDL_Renderer *r, float x, float y, float w, float h, float radius, Color c) {
    std::vector<SDL_FPoint> o;
    round_outline(o, x, y, w, h, radius, 10);
    std::vector<SDL_Vertex> v;
    v.push_back(vert(x + w / 2, y + h / 2, c));
    for (auto &p : o) v.push_back(vert(p.x, p.y, c));
    std::vector<int> idx;
    int n = (int)o.size();
    for (int i = 0; i < n; ++i) {
        idx.push_back(0);
        idx.push_back(1 + i);
        idx.push_back(1 + (i + 1) % n);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void stroke_round_rect(SDL_Renderer *r, float x, float y, float w, float h, float radius, float t, Color c) {
    std::vector<SDL_FPoint> outer, inner;
    round_outline(outer, x, y, w, h, radius, 10);
    round_outline(inner, x + t, y + t, w - 2 * t, h - 2 * t, std::max(0.f, radius - t), 10);
    std::vector<SDL_Vertex> v;
    for (size_t i = 0; i < outer.size(); ++i) {
        v.push_back(vert(outer[i].x, outer[i].y, c));
        v.push_back(vert(inner[i].x, inner[i].y, c));
    }
    std::vector<int> idx;
    int n = (int)outer.size();
    for (int i = 0; i < n; ++i) {
        int a = 2 * i, b = 2 * i + 1, a2 = 2 * ((i + 1) % n), b2 = a2 + 1;
        idx.insert(idx.end(), {a, b, a2, b, b2, a2});
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void fill_circle(SDL_Renderer *r, float cx, float cy, float radius, Color c) {
    fill_round_rect(r, cx - radius, cy - radius, radius * 2, radius * 2, radius, c);
}

void draw_vignette(SDL_Renderer *r, int w, int h, Uint8 strength) {
    Color dark{0, 0, 0, strength}, clear{0, 0, 0, 0};
    const float e = (float)h * 0.28f;
    fill_gradient(r, 0, 0, (float)w, e, dark, clear);
    fill_gradient(r, 0, h - e, (float)w, e, clear, dark);
    // Sides, as sideways gradients: two triangles each with per-vertex alpha.
    const float s = (float)w * 0.12f;
    SDL_Vertex L[4] = {vert(0, 0, dark), vert(s, 0, clear), vert(s, (float)h, clear), vert(0, (float)h, dark)};
    SDL_Vertex R[4] = {vert((float)w - s, 0, clear), vert((float)w, 0, dark), vert((float)w, (float)h, dark), vert((float)w - s, (float)h, clear)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r, nullptr, L, 4, idx, 6);
    SDL_RenderGeometry(r, nullptr, R, 4, idx, 6);
}

static void thick_line(SDL_Renderer *r, float x0, float y0, float x1, float y1, float t, Color c) {
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float nx = -dy / len * t / 2, ny = dx / len * t / 2;
    SDL_Vertex v[4] = {vert(x0 + nx, y0 + ny, c), vert(x1 + nx, y1 + ny, c), vert(x1 - nx, y1 - ny, c), vert(x0 - nx, y0 - ny, c)};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, nullptr, v, 4, idx, 6);
    fill_circle(r, x0, y0, t / 2, c);
    fill_circle(r, x1, y1, t / 2, c);
}

void draw_check(SDL_Renderer *r, float cx, float cy, float s, Color c) {
    float t = std::max(2.f, s * 0.16f);
    thick_line(r, cx - s * 0.32f, cy + s * 0.02f, cx - s * 0.08f, cy + s * 0.26f, t, c);
    thick_line(r, cx - s * 0.08f, cy + s * 0.26f, cx + s * 0.34f, cy - s * 0.24f, t, c);
}

void draw_cross(SDL_Renderer *r, float cx, float cy, float s, Color c) {
    float t = std::max(2.f, s * 0.16f), d = s * 0.26f;
    thick_line(r, cx - d, cy - d, cx + d, cy + d, t, c);
    thick_line(r, cx - d, cy + d, cx + d, cy - d, t, c);
}

void draw_dash(SDL_Renderer *r, float cx, float cy, float s, Color c) {
    thick_line(r, cx - s * 0.26f, cy, cx + s * 0.26f, cy, std::max(2.f, s * 0.16f), c);
}

void draw_chevron(SDL_Renderer *r, float cx, float cy, float s, bool right, Color c) {
    float t = std::max(2.f, s * 0.16f), d = s * 0.22f, k = right ? 1.f : -1.f;
    thick_line(r, cx - k * d * 0.6f, cy - d, cx + k * d * 0.6f, cy, t, c);
    thick_line(r, cx + k * d * 0.6f, cy, cx - k * d * 0.6f, cy + d, t, c);
}

}  // namespace launcher
