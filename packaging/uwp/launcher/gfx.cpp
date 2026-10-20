#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

namespace nl {

struct Gfx::FontData {
    std::vector<uint8_t> bytes;
    stbtt_fontinfo info;
};

namespace {

bool read_file(const std::string &path, std::vector<uint8_t> &out) {
    SDL_RWops *f = SDL_RWFromFile(path.c_str(), "rb");
    if (!f) return false;
    Sint64 size = SDL_RWsize(f);
    if (size <= 0) {
        SDL_RWclose(f);
        return false;
    }
    out.resize((size_t)size);
    size_t got = SDL_RWread(f, out.data(), 1, (size_t)size);
    SDL_RWclose(f);
    return got == (size_t)size;
}

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float len2 = vx * vx + vy * vy;
    float t = len2 > 0 ? clamp01((wx * vx + wy * vy) / len2) : 0;
    float dx = px - (ax + t * vx), dy = py - (ay + t * vy);
    return std::sqrt(dx * dx + dy * dy);
}

bool in_circle(float x, float y, float cx, float cy, float r) {
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

bool in_rrect(float x, float y, float x0, float y0, float x1, float y1, float r) {
    if (x < x0 || x > x1 || y < y0 || y > y1) return false;
    float cx = x < x0 + r ? x0 + r : (x > x1 - r ? x1 - r : x);
    float cy = y < y0 + r ? y0 + r : (y > y1 - r ? y1 - r : y);
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

bool in_tri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) {
    auto sign = [](float x1, float y1, float x2, float y2, float x3, float y3) {
        return (x1 - x3) * (y2 - y3) - (x2 - x3) * (y1 - y3);
    };
    float d1 = sign(px, py, ax, ay, bx, by), d2 = sign(px, py, bx, by, cx, cy), d3 = sign(px, py, cx, cy, ax, ay);
    bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0), pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

bool icon_inside(Icon icon, float x, float y) {
    switch (icon) {
    case Icon::Check:
        return seg_dist(x, y, .18f, .54f, .40f, .76f) <= .075f || seg_dist(x, y, .40f, .76f, .82f, .28f) <= .075f;
    case Icon::Cross:
        return seg_dist(x, y, .26f, .26f, .74f, .74f) <= .075f || seg_dist(x, y, .74f, .26f, .26f, .74f) <= .075f;
    case Icon::Play:
        return in_tri(x, y, .30f, .16f, .30f, .84f, .84f, .50f);
    case Icon::Folder:
        return in_rrect(x, y, .08f, .18f, .46f, .42f, .05f) || in_rrect(x, y, .08f, .30f, .92f, .82f, .07f);
    case Icon::Sliders: {
        bool lines = false;
        for (float ly : {.24f, .50f, .76f}) lines = lines || (x >= .10f && x <= .90f && std::fabs(y - ly) <= .035f);
        bool knobs = in_circle(x, y, .34f, .24f, .12f) || in_circle(x, y, .66f, .50f, .12f) || in_circle(x, y, .44f, .76f, .12f);
        return lines || knobs;
    }
    case Icon::Info: {
        float d = std::sqrt((x - .5f) * (x - .5f) + (y - .5f) * (y - .5f));
        bool ring = d <= .44f && d >= .34f;
        return ring || in_circle(x, y, .5f, .31f, .055f) || in_rrect(x, y, .465f, .42f, .535f, .72f, .03f);
    }
    case Icon::ChevLeft:
        return seg_dist(x, y, .64f, .18f, .34f, .50f) <= .07f || seg_dist(x, y, .34f, .50f, .64f, .82f) <= .07f;
    case Icon::ChevRight:
        return seg_dist(x, y, .36f, .18f, .66f, .50f) <= .07f || seg_dist(x, y, .66f, .50f, .36f, .82f) <= .07f;
    case Icon::Drive:
        return in_rrect(x, y, .08f, .34f, .92f, .74f, .08f) && !in_circle(x, y, .76f, .54f, .055f) && !in_rrect(x, y, .18f, .50f, .52f, .58f, .02f);
    case Icon::Refresh: {
        float dx = x - .5f, dy = y - .52f, d = std::sqrt(dx * dx + dy * dy);
        float a = std::atan2(dy, dx) * 57.29578f;
        bool arc = d >= .27f && d <= .39f && !(a > -80.f && a < -25.f);
        bool head = in_tri(x, y, .56f, .06f, .56f, .40f, .86f, .23f);
        return arc || head;
    }
    case Icon::Warning: {
        bool tri = in_tri(x, y, .5f, .10f, .94f, .88f, .06f, .88f);
        bool mark = in_rrect(x, y, .465f, .36f, .535f, .64f, .03f) || in_circle(x, y, .5f, .74f, .045f);
        return tri && !mark;
    }
    case Icon::Gamepad: {
        bool body = in_rrect(x, y, .06f, .28f, .94f, .76f, .20f);
        bool dpad = in_rrect(x, y, .19f, .48f, .37f, .56f, .02f) || in_rrect(x, y, .24f, .43f, .32f, .62f, .02f);
        bool btn = in_circle(x, y, .70f, .46f, .05f) || in_circle(x, y, .79f, .56f, .05f);
        return body && !dpad && !btn;
    }
    }
    return false;
}

}  // namespace

bool Gfx::init(SDL_Renderer *renderer, const std::string &font_dir) {
    r_ = renderer;
    static const char *names[] = {"Poppins-Regular.ttf", "Poppins-Medium.ttf", "Poppins-Bold.ttf"};
    for (const char *n : names) {
        FontData *f = new FontData();
        if (!read_file(font_dir + n, f->bytes) || !stbtt_InitFont(&f->info, f->bytes.data(), 0)) {
            delete f;
            SDL_Log("launcher: cannot load font %s%s", font_dir.c_str(), n);
            shutdown();
            return false;
        }
        fonts_.push_back(f);
    }
    return true;
}

void Gfx::free_caches() {
    for (auto &kv : atlases_) {
        if (kv.second->tex) SDL_DestroyTexture(kv.second->tex);
        delete kv.second;
    }
    atlases_.clear();
    for (auto &kv : corners_) SDL_DestroyTexture(kv.second);
    corners_.clear();
    // Icon, glow and crescent textures are resolution independent: kept.
}

void Gfx::shutdown() {
    free_caches();
    for (auto &kv : icons_) SDL_DestroyTexture(kv.second);
    icons_.clear();
    if (soft_) SDL_DestroyTexture(soft_);
    if (crescent_) SDL_DestroyTexture(crescent_);
    soft_ = crescent_ = nullptr;
    for (FontData *f : fonts_) delete f;
    fonts_.clear();
}

void Gfx::set_scale(float s) {
    if (s < 0.5f) s = 0.5f;
    if (std::fabs(s - scale_) < 0.01f) return;
    scale_ = s;
    free_caches();
}

Gfx::Atlas *Gfx::atlas(Weight w, int px_logical) {
    int pxp = std::max(6, (int)std::lround(px_logical * scale_));
    int key = (int)w * 100000 + pxp;
    auto it = atlases_.find(key);
    if (it != atlases_.end()) return it->second;

    FontData *f = fonts_[(size_t)w];
    Atlas *a = new Atlas();
    int dim = pxp <= 40 ? 512 : (pxp <= 90 ? 1024 : 2048);
    std::vector<unsigned char> bmp;
    for (;;) {
        bmp.assign((size_t)dim * dim, 0);
        a->baked.assign(sizeof(stbtt_bakedchar) * 96, 0);
        int res = stbtt_BakeFontBitmap(f->bytes.data(), 0, (float)pxp, bmp.data(), dim, dim, 32, 96,
                                       (stbtt_bakedchar *)a->baked.data());
        if (res > 0) break;
        dim *= 2;
        if (dim > 4096) break;
    }
    a->w = a->h = dim;
    std::vector<uint32_t> rgba((size_t)dim * dim);
    for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = 0x00FFFFFFu | ((uint32_t)bmp[i] << 24);
    a->tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, dim, dim);
    if (a->tex) {
        SDL_UpdateTexture(a->tex, nullptr, rgba.data(), dim * 4);
        SDL_SetTextureBlendMode(a->tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(a->tex, SDL_ScaleModeLinear);
    }
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
    float sc = stbtt_ScaleForPixelHeight(&f->info, (float)pxp);
    a->ascent = asc * sc;
    a->descent = desc * sc;
    a->line_gap = gap * sc;
    atlases_[key] = a;
    return a;
}

int Gfx::line_height(int px, Weight w) {
    Atlas *a = atlas(w, px);
    return (int)std::lround((a->ascent - a->descent + a->line_gap) / scale_);
}

int Gfx::text_width(const std::string &s, int px, Weight w, int spacing) {
    Atlas *a = atlas(w, px);
    const stbtt_bakedchar *cd = (const stbtt_bakedchar *)a->baked.data();
    float total = 0;
    for (unsigned char c : s) {
        if (c < 32 || c > 126) c = '?';
        total += cd[c - 32].xadvance / scale_ + spacing;
    }
    if (!s.empty()) total -= spacing;
    return (int)std::lround(total);
}

void Gfx::text(const std::string &s, int x, int y, int px, Weight w, Color c, int spacing) {
    Atlas *a = atlas(w, px);
    if (!a->tex) return;
    SDL_SetTextureColorMod(a->tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(a->tex, c.a);
    const stbtt_bakedchar *cd = (const stbtt_bakedchar *)a->baked.data();
    float xp = std::round(x * scale_);
    float yp = std::round(y * scale_ + a->ascent);
    for (unsigned char ch : s) {
        if (ch < 32 || ch > 126) ch = '?';
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(cd, a->w, a->h, ch - 32, &xp, &yp, &q, 1);
        SDL_Rect src = {(int)std::lround(q.s0 * a->w), (int)std::lround(q.t0 * a->h),
                        (int)std::lround((q.s1 - q.s0) * a->w), (int)std::lround((q.t1 - q.t0) * a->h)};
        SDL_FRect dst = {q.x0 / scale_, q.y0 / scale_, (q.x1 - q.x0) / scale_, (q.y1 - q.y0) / scale_};
        if (src.w > 0 && src.h > 0) SDL_RenderCopyF(r_, a->tex, &src, &dst);
        xp += spacing * scale_;
    }
}

void Gfx::text_right(const std::string &s, int right, int y, int px, Weight w, Color c, int spacing) {
    text(s, right - text_width(s, px, w, spacing), y, px, w, c, spacing);
}

void Gfx::text_center(const std::string &s, int cx, int y, int px, Weight w, Color c, int spacing) {
    text(s, cx - text_width(s, px, w, spacing) / 2, y, px, w, c, spacing);
}

std::vector<std::string> Gfx::wrap(const std::string &s, int px, Weight w, int max_w) {
    std::vector<std::string> lines;
    std::string para;
    auto flush_para = [&]() {
        std::string line;
        size_t i = 0;
        while (i <= para.size()) {
            size_t j = para.find(' ', i);
            if (j == std::string::npos) j = para.size();
            std::string word = para.substr(i, j - i);
            std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && text_width(trial, px, w) > max_w) {
                lines.push_back(line);
                line = word;
            } else {
                line = trial;
            }
            i = j + 1;
        }
        lines.push_back(line);
        para.clear();
    };
    for (char ch : s) {
        if (ch == '\n') flush_para();
        else para += ch;
    }
    flush_para();
    return lines;
}

void Gfx::rect(int x, int y, int w, int h, Color c) {
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(r_, &rc);
}

SDL_Texture *Gfx::corner_texture(int rp) {
    auto it = corners_.find(rp);
    if (it != corners_.end()) return it->second;
    int n = rp * 2;
    std::vector<uint32_t> px((size_t)n * n);
    for (int yy = 0; yy < n; ++yy)
        for (int xx = 0; xx < n; ++xx) {
            float dx = xx + .5f - rp, dy = yy + .5f - rp;
            float cov = clamp01(rp - std::sqrt(dx * dx + dy * dy) + .5f);
            px[(size_t)yy * n + xx] = 0x00FFFFFFu | ((uint32_t)std::lround(cov * 255) << 24);
        }
    SDL_Texture *t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, n, n);
    SDL_UpdateTexture(t, nullptr, px.data(), n * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    corners_[rp] = t;
    return t;
}

void Gfx::round_rect(int x, int y, int w, int h, int r, Color c) {
    r = std::min(r, std::min(w, h) / 2);
    if (r <= 0) {
        rect(x, y, w, h, c);
        return;
    }
    int rp = std::max(1, (int)std::lround(r * scale_));
    SDL_Texture *t = corner_texture(rp);
    rect(x + r, y, w - 2 * r, h, c);
    rect(x, y + r, r, h - 2 * r, c);
    rect(x + w - r, y + r, r, h - 2 * r, c);
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    SDL_Rect s[4] = {{0, 0, rp, rp}, {rp, 0, rp, rp}, {0, rp, rp, rp}, {rp, rp, rp, rp}};
    SDL_Rect d[4] = {{x, y, r, r}, {x + w - r, y, r, r}, {x, y + h - r, r, r}, {x + w - r, y + h - r, r, r}};
    for (int i = 0; i < 4; ++i) SDL_RenderCopy(r_, t, &s[i], &d[i]);
}

void Gfx::circle(int cx, int cy, int r, Color c) { round_rect(cx - r, cy - r, r * 2, r * 2, r, c); }

void Gfx::gradient_v(int x, int y, int w, int h, Color top, Color bottom) {
    SDL_Vertex v[4];
    auto mk = [](float px, float py, Color c) {
        SDL_Vertex vx;
        vx.position = {px, py};
        vx.color = {c.r, c.g, c.b, c.a};
        vx.tex_coord = {0, 0};
        return vx;
    };
    v[0] = mk((float)x, (float)y, top);
    v[1] = mk((float)(x + w), (float)y, top);
    v[2] = mk((float)x, (float)(y + h), bottom);
    v[3] = mk((float)(x + w), (float)(y + h), bottom);
    int idx[6] = {0, 1, 2, 1, 3, 2};
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r_, nullptr, v, 4, idx, 6);
}

SDL_Texture *Gfx::soft_circle_texture() {
    if (soft_) return soft_;
    const int n = 256;
    std::vector<uint32_t> px((size_t)n * n);
    for (int yy = 0; yy < n; ++yy)
        for (int xx = 0; xx < n; ++xx) {
            float dx = (xx + .5f - n / 2) / (n / 2), dy = (yy + .5f - n / 2) / (n / 2);
            float d = std::sqrt(dx * dx + dy * dy);
            float a = clamp01(1.0f - d);
            a = a * a * (3 - 2 * a);
            px[(size_t)yy * n + xx] = 0x00FFFFFFu | ((uint32_t)std::lround(a * 255) << 24);
        }
    soft_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, n, n);
    SDL_UpdateTexture(soft_, nullptr, px.data(), n * 4);
    SDL_SetTextureBlendMode(soft_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(soft_, SDL_ScaleModeLinear);
    return soft_;
}

void Gfx::glow(int cx, int cy, int radius, Color c) {
    SDL_Texture *t = soft_circle_texture();
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    SDL_Rect d = {cx - radius, cy - radius, radius * 2, radius * 2};
    SDL_RenderCopy(r_, t, nullptr, &d);
}

SDL_Texture *Gfx::crescent_texture() {
    if (crescent_) return crescent_;
    const int n = 512;
    std::vector<uint32_t> px((size_t)n * n);
    for (int yy = 0; yy < n; ++yy)
        for (int xx = 0; xx < n; ++xx) {
            float fx = xx + .5f, fy = yy + .5f;
            float da = std::sqrt((fx - n * .5f) * (fx - n * .5f) + (fy - n * .5f) * (fy - n * .5f));
            float db = std::sqrt((fx - n * .66f) * (fx - n * .66f) + (fy - n * .42f) * (fy - n * .42f));
            float cov = clamp01(n * .45f - da + .5f) * clamp01(db - n * .38f + .5f);
            px[(size_t)yy * n + xx] = 0x00FFFFFFu | ((uint32_t)std::lround(cov * 255) << 24);
        }
    crescent_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, n, n);
    SDL_UpdateTexture(crescent_, nullptr, px.data(), n * 4);
    SDL_SetTextureBlendMode(crescent_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(crescent_, SDL_ScaleModeLinear);
    return crescent_;
}

void Gfx::crescent(int cx, int cy, int r, Color c) {
    SDL_Texture *t = crescent_texture();
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    SDL_Rect d = {cx - r, cy - r, r * 2, r * 2};
    SDL_RenderCopy(r_, t, nullptr, &d);
}

SDL_Texture *Gfx::icon_texture(Icon icon) {
    auto it = icons_.find((int)icon);
    if (it != icons_.end()) return it->second;
    const int n = 128, ss = 4;
    std::vector<uint32_t> px((size_t)n * n);
    for (int yy = 0; yy < n; ++yy)
        for (int xx = 0; xx < n; ++xx) {
            int hit = 0;
            for (int sy = 0; sy < ss; ++sy)
                for (int sx = 0; sx < ss; ++sx)
                    if (icon_inside(icon, (xx + (sx + .5f) / ss) / n, (yy + (sy + .5f) / ss) / n)) ++hit;
            px[(size_t)yy * n + xx] = 0x00FFFFFFu | ((uint32_t)(hit * 255 / (ss * ss)) << 24);
        }
    SDL_Texture *t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, n, n);
    SDL_UpdateTexture(t, nullptr, px.data(), n * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    icons_[(int)icon] = t;
    return t;
}

void Gfx::icon(Icon i, int x, int y, int size, Color c) {
    SDL_Texture *t = icon_texture(i);
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, c.a);
    SDL_Rect d = {x, y, size, size};
    SDL_RenderCopy(r_, t, nullptr, &d);
}

}  // namespace nl
