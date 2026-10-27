#include "gfx.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "../third_party/stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace nl {

namespace {

constexpr float kPi = 3.14159265358979f;

struct V {
    float x, y, nx, ny;
};

// Outline of a rounded rectangle, clockwise from the top edge's left end, with the
// outward unit normal at each point.
std::vector<V> perimeter(const Rect &r, float radius, int arc_steps = 10) {
    radius = std::max(0.0f, std::min(radius, std::min(r.w, r.h) * 0.5f));
    std::vector<V> pts;
    const float cx[4] = {r.x + r.w - radius, r.x + r.w - radius, r.x + radius, r.x + radius};
    const float cy[4] = {r.y + radius, r.y + r.h - radius, r.y + r.h - radius, r.y + radius};
    const float a0[4] = {-0.5f * kPi, 0.0f, 0.5f * kPi, kPi};
    const int steps = radius < 0.5f ? 1 : arc_steps;
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i <= steps; ++i) {
            const float a = a0[c] + (0.5f * kPi) * (static_cast<float>(i) / steps);
            const float nx = std::cos(a), ny = std::sin(a);
            pts.push_back({cx[c] + nx * radius, cy[c] + ny * radius, nx, ny});
        }
    }
    return pts;
}

SDL_Vertex vert(float x, float y, Color c) {
    SDL_Vertex v;
    v.position.x = x;
    v.position.y = y;
    v.color.r = c.r;
    v.color.g = c.g;
    v.color.b = c.b;
    v.color.a = c.a;
    v.tex_coord.x = 0;
    v.tex_coord.y = 0;
    return v;
}

}  // namespace

struct Gfx::Impl {
    struct Glyph {
        SDL_Texture *tex = nullptr;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        float advance = 0;
    };
    struct SizedFont {
        float scale = 0;
        int ascent = 0, descent = 0;
        std::unordered_map<uint32_t, Glyph> glyphs;
    };
    struct Face {
        std::vector<unsigned char> data;
        stbtt_fontinfo info;
        bool ok = false;
        std::unordered_map<int, SizedFont> sizes;   // keyed by real pixel height
    };
    Face faces[2];
    Gfx *owner = nullptr;
    SDL_Texture *grain = nullptr;

    void clear_glyphs() {
        for (Face &f : faces)
            for (auto &kv : f.sizes)
                for (auto &g : kv.second.glyphs)
                    if (g.second.tex) SDL_DestroyTexture(g.second.tex);
        for (Face &f : faces) f.sizes.clear();
    }

    bool load_face(Face &f, const std::string &path) {
        SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
        if (!rw) return false;
        const Sint64 n = SDL_RWsize(rw);
        if (n <= 0) { SDL_RWclose(rw); return false; }
        f.data.resize(static_cast<size_t>(n));
        const size_t got = SDL_RWread(rw, f.data.data(), 1, f.data.size());
        SDL_RWclose(rw);
        if (got != f.data.size()) return false;
        f.ok = stbtt_InitFont(&f.info, f.data.data(), stbtt_GetFontOffsetForIndex(f.data.data(), 0)) != 0;
        return f.ok;
    }

    SizedFont &sized(FontFace face, int design_px, float ui_scale) {
        Face &f = faces[static_cast<int>(face)];
        static const float kFaceSize[2] = {1.0f, 1.14f};
        const int real = std::max(6, static_cast<int>(std::lround(design_px * ui_scale * kFaceSize[static_cast<int>(face)])));
        auto it = f.sizes.find(real);
        if (it != f.sizes.end()) return it->second;
        SizedFont sf;
        sf.scale = stbtt_ScaleForPixelHeight(&f.info, static_cast<float>(real));
        int asc = 0, desc = 0, gap = 0;
        stbtt_GetFontVMetrics(&f.info, &asc, &desc, &gap);
        sf.ascent = static_cast<int>(std::lround(asc * sf.scale));
        sf.descent = static_cast<int>(std::lround(desc * sf.scale));
        return f.sizes.emplace(real, std::move(sf)).first->second;
    }

    Glyph &glyph(SDL_Renderer *r, FontFace face, SizedFont &sf, uint32_t cp) {
        auto it = sf.glyphs.find(cp);
        if (it != sf.glyphs.end()) return it->second;
        Face &f = faces[static_cast<int>(face)];
        Glyph g;
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f.info, static_cast<int>(cp), &adv, &lsb);
        g.advance = adv * sf.scale;
        int w = 0, h = 0, xo = 0, yo = 0;
        unsigned char *bm = stbtt_GetCodepointBitmap(&f.info, sf.scale, sf.scale, static_cast<int>(cp), &w, &h, &xo, &yo);
        if (bm && w > 0 && h > 0) {
            std::vector<Uint32> px(static_cast<size_t>(w) * h);
            for (int i = 0; i < w * h; ++i) px[i] = (static_cast<Uint32>(bm[i]) << 24) | 0x00FFFFFFu;
            g.tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
            if (g.tex) {
                SDL_UpdateTexture(g.tex, nullptr, px.data(), w * 4);
                SDL_SetTextureBlendMode(g.tex, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(g.tex, SDL_ScaleModeLinear);
            }
            g.w = w; g.h = h; g.xoff = xo; g.yoff = yo;
        }
        if (bm) stbtt_FreeBitmap(bm, nullptr);
        return sf.glyphs.emplace(cp, g).first->second;
    }
};

namespace {

// Decodes one UTF-8 code point and advances `p`.
uint32_t next_cp(const char *&p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x80) { ++p; return c; }
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = (extra == 3) ? (c & 0x07) : (extra == 2) ? (c & 0x0F) : (extra == 1) ? (c & 0x1F) : c;
    ++p;
    for (int i = 0; i < extra && (*p & 0xC0) == 0x80; ++i, ++p) cp = (cp << 6) | (*p & 0x3F);
    return cp;
}

}  // namespace

bool Gfx::init(SDL_Renderer *r, const std::string &fonts_dir) {
    r_ = r;
    impl_ = new Impl();
    impl_->owner = this;
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    // Body: Special Elite, a worn typewriter face. Headings: IM Fell Double Pica, an old-style print face.
    const bool a = impl_->load_face(impl_->faces[0], fonts_dir + "/SpecialElite-Regular.ttf");
    const bool b = impl_->load_face(impl_->faces[1], fonts_dir + "/IMFELLDoublePica-Regular.ttf");
    return a && b;
}

void Gfx::shutdown() {
    if (impl_) {
        if (impl_->grain) SDL_DestroyTexture(impl_->grain);
        impl_->clear_glyphs();
        delete impl_;
        impl_ = nullptr;
    }
}

void Gfx::set_output_size(int w, int h) {
    // SDL letterboxes the 16:9 design space into the output; text is rasterised at the
    // real pixel size so it stays sharp when the output is larger than 1080p.
    const float s = std::min(static_cast<float>(w) / kDesignW, static_cast<float>(h) / kDesignH);
    if (std::fabs(s - scale_) > 1e-4f) {
        scale_ = s;
        impl_->clear_glyphs();
    }
    SDL_RenderSetLogicalSize(r_, kDesignW, kDesignH);
}

void Gfx::quad_gradient_v(const Rect &r, Color top, Color bottom) {
    SDL_Vertex v[4] = {vert(r.x, r.y, top), vert(r.right(), r.y, top), vert(r.right(), r.bottom(), bottom),
                       vert(r.x, r.bottom(), bottom)};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r_, nullptr, v, 4, idx, 6);
}

void Gfx::fill_black() {
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
}

void Gfx::clear_background() {
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);
    const Rect full{0, 0, kDesignW, kDesignH};
    quad_gradient_v(full, Color(14, 10, 8), Color(26, 16, 12));
    // Soft vignette at the left and right edges.
    const Color dark(0, 0, 0, 150), clear(0, 0, 0, 0);
    const float m = 360;
    SDL_Vertex vl[4] = {vert(0, 0, dark), vert(m, 0, clear), vert(m, kDesignH, clear), vert(0, kDesignH, dark)};
    SDL_Vertex vr[4] = {vert(kDesignW - m, 0, clear), vert(kDesignW, 0, dark), vert(kDesignW, kDesignH, dark),
                        vert(kDesignW - m, kDesignH, clear)};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r_, nullptr, vl, 4, idx, 6);
    SDL_RenderGeometry(r_, nullptr, vr, 4, idx, 6);
}

namespace {

void strip(SDL_Renderer *r, const std::vector<V> &a, const std::vector<V> &b, Color ca, Color cb) {
    const size_t n = a.size();
    std::vector<SDL_Vertex> vs;
    std::vector<int> idx;
    vs.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        vs.push_back(vert(a[i].x, a[i].y, ca));
        vs.push_back(vert(b[i].x, b[i].y, cb));
    }
    for (size_t i = 0; i < n; ++i) {
        const int i0 = static_cast<int>(i * 2), i1 = static_cast<int>(((i + 1) % n) * 2);
        idx.insert(idx.end(), {i0, i0 + 1, i1, i1, i0 + 1, i1 + 1});
    }
    SDL_RenderGeometry(r, nullptr, vs.data(), static_cast<int>(vs.size()), idx.data(), static_cast<int>(idx.size()));
}

std::vector<V> offset(const std::vector<V> &p, float d) {
    std::vector<V> o = p;
    for (V &v : o) { v.x += v.nx * d; v.y += v.ny * d; }
    return o;
}

}  // namespace

// The theme is square-cornered paper and card, not soft pills: rounded rectangles keep a fraction of the
// radius they are asked for. circle() and ring() need the full radius and use the exact versions.
static constexpr float kCornerScale = 0.22f;

void Gfx::rrect(const Rect &r, float radius, Color c) { rrect_exact(r, std::max(2.0f, radius * kCornerScale), c); }

void Gfx::rrect_outline(const Rect &r, float radius, float t, Color c) {
    rrect_outline_exact(r, std::max(2.0f, radius * kCornerScale), t, c);
}

void Gfx::rrect_exact(const Rect &r, float radius, Color c) {
    if (c.a == 0 || r.w <= 0 || r.h <= 0) return;
    const std::vector<V> p = perimeter(r, radius);
    std::vector<SDL_Vertex> vs;
    vs.push_back(vert(r.cx(), r.cy(), c));
    for (const V &v : p) vs.push_back(vert(v.x, v.y, c));
    std::vector<int> idx;
    const int n = static_cast<int>(p.size());
    for (int i = 0; i < n; ++i) idx.insert(idx.end(), {0, 1 + i, 1 + (i + 1) % n});
    SDL_RenderGeometry(r_, nullptr, vs.data(), static_cast<int>(vs.size()), idx.data(), static_cast<int>(idx.size()));
    // One real pixel of fade for smooth edges.
    const float f = 1.0f / scale_;
    strip(r_, p, offset(p, f), c, Color(c.r, c.g, c.b, 0));
}

void Gfx::rrect_outline_exact(const Rect &r, float radius, float t, Color c) {
    if (c.a == 0) return;
    const float h = t * 0.5f;
    const std::vector<V> mid = perimeter(r, radius);
    const std::vector<V> outer = offset(mid, h), inner = offset(mid, -h);
    strip(r_, inner, outer, c, c);
    const float f = 1.0f / scale_;
    const Color z(c.r, c.g, c.b, 0);
    strip(r_, outer, offset(outer, f), c, z);
    strip(r_, offset(inner, -f), inner, z, c);
}

void Gfx::circle(float cx, float cy, float rad, Color c) {
    rrect_exact({cx - rad, cy - rad, rad * 2, rad * 2}, rad, c);
}

void Gfx::ring(float cx, float cy, float rad, float t, Color c) {
    rrect_outline_exact({cx - rad, cy - rad, rad * 2, rad * 2}, rad, t, c);
}

void Gfx::line(float x1, float y1, float x2, float y2, float t, Color c) {
    const float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    const float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    SDL_Vertex v[4] = {vert(x1 + nx, y1 + ny, c), vert(x2 + nx, y2 + ny, c), vert(x2 - nx, y2 - ny, c),
                       vert(x1 - nx, y1 - ny, c)};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r_, nullptr, v, 4, idx, 6);
}

float Gfx::text_width(const std::string &s, int px, FontFace f, float spacing) {
    if (!impl_ || !impl_->faces[static_cast<int>(f)].ok) return 0;
    Impl::SizedFont &sf = impl_->sized(f, px, scale_);
    float w = 0;
    for (const char *p = s.c_str(); *p;) {
        const uint32_t cp = next_cp(p);
        Impl::Glyph &g = impl_->glyph(r_, f, sf, cp);
        w += g.advance / scale_ + spacing;
    }
    return w;
}

void Gfx::text(float x, float y, const std::string &s, int px, Color c, FontFace f, float spacing) {
    if (!impl_ || !impl_->faces[static_cast<int>(f)].ok || c.a == 0) return;
    Impl::SizedFont &sf = impl_->sized(f, px, scale_);
    float pen = x;
    const float base = y + sf.ascent / scale_;
    for (const char *p = s.c_str(); *p;) {
        const uint32_t cp = next_cp(p);
        Impl::Glyph &g = impl_->glyph(r_, f, sf, cp);
        if (g.tex) {
            SDL_SetTextureColorMod(g.tex, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(g.tex, c.a);
            SDL_FRect d{pen + g.xoff / scale_, base + g.yoff / scale_, g.w / scale_, g.h / scale_};
            SDL_RenderCopyF(r_, g.tex, nullptr, &d);
        }
        pen += g.advance / scale_ + spacing;
    }
}

void Gfx::text_center(float cx, float y, const std::string &s, int px, Color c, FontFace f, float sp) {
    text(cx - text_width(s, px, f, sp) * 0.5f, y, s, px, c, f, sp);
}

void Gfx::text_right(float rx, float y, const std::string &s, int px, Color c, FontFace f, float sp) {
    text(rx - text_width(s, px, f, sp), y, s, px, c, f, sp);
}

std::string Gfx::fit(const std::string &s, float max_w, int px, FontFace f) {
    if (text_width(s, px, f) <= max_w) return s;
    std::string out = s;
    const std::string dots = "\xE2\x80\xA6";
    while (!out.empty()) {
        // Drop one UTF-8 character from the end.
        size_t i = out.size() - 1;
        while (i > 0 && (static_cast<unsigned char>(out[i]) & 0xC0) == 0x80) --i;
        out.erase(i);
        if (text_width(out + dots, px, f) <= max_w) return out + dots;
    }
    return dots;
}

SDL_Texture *Gfx::load_bmp(const std::string &path, int *w, int *h) {
    SDL_Surface *surf = SDL_LoadBMP(path.c_str());
    if (!surf) return nullptr;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r_, surf);
    if (w) *w = surf->w;
    if (h) *h = surf->h;
    SDL_FreeSurface(surf);
    if (tex) SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    return tex;
}

// PNG, JPEG, BMP or TGA, decoded here so a picture from the game's own folder can be used. Nothing is loaded
// that is smaller than min_side on its short side or larger than 8192 on its long side.
SDL_Texture *Gfx::load_image(const std::string &path, int *w, int *h, int min_side) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return nullptr;
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return load_image_memory(bytes.data(), bytes.size(), w, h, min_side);
}

SDL_Texture *Gfx::load_image_memory(const unsigned char *data, size_t size, int *w, int *h, int min_side) {
    if (!data || size == 0 || size > (48u << 20)) return nullptr;
    int iw = 0, ih = 0, comp = 0;
    if (!stbi_info_from_memory(data, static_cast<int>(size), &iw, &ih, &comp)) return nullptr;
    if (std::min(iw, ih) < min_side || std::max(iw, ih) > 8192) return nullptr;
    unsigned char *px = stbi_load_from_memory(data, static_cast<int>(size), &iw, &ih, &comp, 4);
    if (!px) return nullptr;
    // Old TGA/BMP files often carry an all-zero alpha channel; a picture is opaque here.
    for (int i = 0; i < iw * ih; ++i) px[i * 4 + 3] = 255;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormatFrom(px, iw, ih, 32, iw * 4, SDL_PIXELFORMAT_RGBA32);
    SDL_Texture *tex = surf ? SDL_CreateTextureFromSurface(r_, surf) : nullptr;
    if (surf) SDL_FreeSurface(surf);
    stbi_image_free(px);
    if (!tex) return nullptr;
    if (w) *w = iw;
    if (h) *h = ih;
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeLinear);
    return tex;
}

void Gfx::image(SDL_Texture *tex, const Rect &dst) { image_tinted(tex, dst, Color(255, 255, 255, 255)); }

void Gfx::image_tinted(SDL_Texture *tex, const Rect &dst, Color tint) {
    if (!tex) return;
    const SDL_FRect d{dst.x, dst.y, dst.w, dst.h};
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(tex, tint.a);
    SDL_RenderCopyF(r_, tex, nullptr, &d);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}

// Film grain: one small texture of random dark and light specks, made once and tiled over the screen.
void Gfx::draw_grain(float strength) {
    if (!impl_) return;
    if (!impl_->grain) {
        const int n = 256;
        std::vector<Uint32> px(static_cast<size_t>(n) * n);
        Uint32 s = 0x9E3779B9u;
        for (Uint32 &p : px) {
            s = s * 1664525u + 1013904223u;
            const Uint32 v = (s >> 24) & 0xFFu;
            const Uint32 a = v > 232 ? (v - 232) * 8 : (v < 14 ? (14 - v) * 8 : 0);   // sparse specks
            const Uint32 lum = v > 200 ? 255u : 0u;
            p = (a << 24) | (lum << 16) | (lum << 8) | lum;
        }
        impl_->grain = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, n, n);
        if (impl_->grain) {
            SDL_UpdateTexture(impl_->grain, nullptr, px.data(), n * 4);
            SDL_SetTextureBlendMode(impl_->grain, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(impl_->grain, SDL_ScaleModeNearest);
        }
    }
    if (!impl_->grain) return;
    SDL_SetTextureAlphaMod(impl_->grain, static_cast<Uint8>(std::min(255.0f, 255.0f * strength)));
    for (float y = 0; y < kDesignH; y += 256)
        for (float x = 0; x < kDesignW; x += 256) {
            const SDL_FRect d{x, y, 256, 256};
            SDL_RenderCopyF(r_, impl_->grain, nullptr, &d);
        }
}

}  // namespace nl
