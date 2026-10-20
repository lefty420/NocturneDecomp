#include "launcher.h"

#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

// The fonts are linked in as byte arrays (see CMakeLists: embed_fonts).
extern "C" {
extern const unsigned char launcher_font_regular[];
extern const unsigned char launcher_font_medium[];
extern const unsigned char launcher_font_bold[];
}

namespace launcher {

// ---------------------------------------------------------------- theme

namespace theme {
const Color bgTop = rgba(0x0A0C11), bgBottom = rgba(0x1A0A0E);
const Color panel = rgba(0x161922, 235), panelHi = rgba(0x1F2430, 245);
const Color line = rgba(0x2A2F3C);
const Color text = rgba(0xEDE7DB), dim = rgba(0x8F8D97), faint = rgba(0x5E5D66);
const Color accent = rgba(0xC4283A), accentSoft = rgba(0xC4283A, 70);
const Color good = rgba(0x62C27F), warn = rgba(0xE3B64B), bad = rgba(0xE5605F);
}  // namespace theme

static const float W = 1920, H = 1080, MARGIN = 96;

struct Fonts {
    Font title, h1, h2, body, small, tiny;
};

struct App::Impl {
    SDL_Renderer *r;
    std::string root, folderLabel;
    std::unique_ptr<Importer> importer;
    Fonts f;
    Scan scan;

    // navigation
    int homeSel = 0, filesSel = 0, settingsSel = 0;
    float focusY = 0, focusTarget = 0;       // animated focus bar
    float fade = 1;                          // screen transition 0..1
    float time = 0;

    // toast
    std::string toast;
    float toastT = 0;

    // import sheet
    bool sheetOpen = false;
    int sheetSel = 0;
    std::vector<ImportMethod> methods;
    bool importing = false;
    ImportStatus last;
};

// ---------------------------------------------------------------- helpers

// Shortens `s` with a leading "..." until it fits (paths are most useful at the end).
static std::string elide(const Font &f, std::string s, float maxW) {
    if (f.width(s) <= maxW) return s;
    while (s.size() > 4 && f.width("..." + s) > maxW) s.erase(0, 1);
    return "..." + s;
}

static bool g_suppressHints = false;

static void centered(SDL_Renderer *r, const Font &f, const std::string &s, float cx, float y, Color c) {
    f.draw(r, s, cx - f.width(s) / 2, y, c);
}

static void right_text(SDL_Renderer *r, const Font &f, const std::string &s, float rx, float y, Color c) {
    f.draw(r, s, rx - f.width(s), y, c);
}

// Xbox-style button badge.
static void badge(SDL_Renderer *r, Fonts &f, char letter, float x, float y, float size) {
    Color c = letter == 'A' ? rgba(0x4CAF50) : letter == 'B' ? rgba(0xE0463C) : letter == 'X' ? rgba(0x3F7FE0) : rgba(0xE2B02F);
    fill_circle(r, x + size / 2, y + size / 2, size / 2, c);
    std::string s(1, letter);
    f.tiny.draw(r, s, x + size / 2 - f.tiny.width(s) / 2, y + size / 2 - f.tiny.lineHeight() / 2 + 1, rgba(0x101114));
}

static void hint(SDL_Renderer *r, Fonts &f, float &x, float y, char b, const std::string &label) {
    badge(r, f, b, x, y, 34);
    f.small.draw(r, label, x + 46, y + 2, theme::dim);
    x += 46 + f.small.width(label) + 44;
}

static void hints(SDL_Renderer *r, Fonts &f, std::initializer_list<std::pair<char, const char *>> items) {
    if (g_suppressHints) return;
    float x = MARGIN, y = H - 78;
    for (auto &it : items) hint(r, f, x, y, it.first, it.second);
}

static void panel(SDL_Renderer *r, float x, float y, float w, float h, bool hi = false) {
    fill_round_rect(r, x, y, w, h, 20, hi ? theme::panelHi : theme::panel);
    stroke_round_rect(r, x, y, w, h, 20, 1.5f, theme::line);
}

static void pill(SDL_Renderer *r, Fonts &f, const std::string &s, float rx, float y, Color bg, Color fg) {
    float w = f.small.width(s) + 36;
    fill_round_rect(r, rx - w, y, w, 44, 22, bg);
    f.small.draw(r, s, rx - w + 18, y + 7, fg);
}

// ---------------------------------------------------------------- App

App::App(SDL_Renderer *renderer, std::string dataRoot, std::unique_ptr<Importer> importer, std::string label)
    : d_(new Impl) {
    d_->r = renderer;
    d_->root = std::move(dataRoot);
    d_->folderLabel = std::move(label);
    d_->importer = std::move(importer);
    ok_ = d_->f.title.init(renderer, launcher_font_bold, 96) &&
          d_->f.h1.init(renderer, launcher_font_bold, 52) &&
          d_->f.h2.init(renderer, launcher_font_medium, 36) &&
          d_->f.body.init(renderer, launcher_font_regular, 30) &&
          d_->f.small.init(renderer, launcher_font_regular, 26) &&
          d_->f.tiny.init(renderer, launcher_font_bold, 22);
    settings_ = load_settings(d_->root);
    rescan();
    d_->homeSel = d_->scan.ready ? 0 : 1;   // nudge towards "Game Files" when Play cannot work
}

App::~App() {}

void App::rescan() { d_->scan = scan_game_files(d_->root); }

void App::setScreen(Screen s) {
    screen_ = s;
    d_->fade = 0;
    if (s == Screen::Files) rescan();
}

void App::openImportSheet() {
    d_->sheetOpen = true;
    d_->sheetSel = 0;
    d_->last = ImportStatus();
    d_->importing = false;
    if (d_->importer) d_->methods = d_->importer->methods();
}

static void show_toast(App::Impl &d, const std::string &s) {
    d.toast = s;
    d.toastT = 3.2f;
}

// ---------------------------------------------------------------- input

static const int kHomeCount = 4;
static const int kFilesCount = 2;      // Import, Rescan (B goes back)
static const int kSettingsCount = 10;  // 9 settings + reset

void App::press(Button b) {
    Impl &d = *d_;

    // Import sheet captures everything while open.
    if (d.sheetOpen) {
        ImportStatus st = d.importer ? d.importer->status() : ImportStatus();
        bool busy = st.state == ImportStatus::Copying || st.state == ImportStatus::Picking;
        if (busy) {
            if (b == Button::B) d.importer->cancel();
            return;
        }
        if (st.state == ImportStatus::Done || st.state == ImportStatus::Failed || st.state == ImportStatus::Cancelled) {
            if (b == Button::A || b == Button::B) {
                d.sheetOpen = false;
                d.importer->cancel();
                rescan();
                d.last = ImportStatus();
            }
            return;
        }
        if (b == Button::B) { d.sheetOpen = false; return; }
        int n = (int)d.methods.size();
        if (b == Button::Up && n) d.sheetSel = (d.sheetSel + n - 1) % n;
        if (b == Button::Down && n) d.sheetSel = (d.sheetSel + 1) % n;
        if (b == Button::A && n && d.importer) d.importer->start(d.methods[d.sheetSel].id);
        return;
    }

    switch (screen_) {
        case Screen::Home:
            if (b == Button::Up) d.homeSel = (d.homeSel + kHomeCount - 1) % kHomeCount;
            else if (b == Button::Down) d.homeSel = (d.homeSel + 1) % kHomeCount;
            else if (b == Button::Start || b == Button::Y) { if (d.scan.ready) wantPlay_ = true; else show_toast(d, "Add the game files first."); }
            else if (b == Button::A) {
                if (d.homeSel == 0) {
                    if (d.scan.ready) wantPlay_ = true;
                    else show_toast(d, d.scan.missingRequired == 1 ? "One required folder is missing. Open Game Files." : "Required folders are missing. Open Game Files.");
                } else if (d.homeSel == 1) setScreen(Screen::Files);
                else if (d.homeSel == 2) setScreen(Screen::Settings);
                else setScreen(Screen::About);
            } else if (b == Button::B) { /* nothing to go back to */ }
            break;

        case Screen::Files:
            if (b == Button::B) setScreen(Screen::Home);
            else if (b == Button::Up) d.filesSel = (d.filesSel + kFilesCount - 1) % kFilesCount;
            else if (b == Button::Down) d.filesSel = (d.filesSel + 1) % kFilesCount;
            else if (b == Button::Y) { rescan(); show_toast(d, "Rescanned."); }
            else if (b == Button::X) {
                if (d.importer) openImportSheet(); else show_toast(d, "Importing is not available in this build.");
            } else if (b == Button::A) {
                if (d.filesSel == 0) {
                    if (d.importer) openImportSheet(); else show_toast(d, "Importing is not available in this build.");
                } else { rescan(); show_toast(d, "Rescanned."); }
            }
            break;

        case Screen::Settings: {
            Settings &s = settings_;
            int step = b == Button::Left ? -1 : (b == Button::Right || b == Button::A ? 1 : 0);
            if (b == Button::B) { setScreen(Screen::Home); break; }
            if (b == Button::Up) { d.settingsSel = (d.settingsSel + kSettingsCount - 1) % kSettingsCount; break; }
            if (b == Button::Down) { d.settingsSel = (d.settingsSel + 1) % kSettingsCount; break; }
            if (!step) break;
            auto wrap = [](int v, int lo, int hi, int st) { v += st; if (v < lo) v = hi; if (v > hi) v = lo; return v; };
            auto clamp = [](int v, int lo, int hi, int st) { v += st; return v < lo ? lo : (v > hi ? hi : v); };
            switch (d.settingsSel) {
                case 0: s.resolution = wrap(s.resolution, 0, resolution_count() - 1, step); break;
                case 1: s.fit = wrap(s.fit, 0, 2, step); break;
                case 2: s.introMovie = !s.introMovie; break;
                case 3: s.musicVolume = clamp(s.musicVolume, 0, 100, step * 10); break;
                case 4: s.movieVolume = clamp(s.movieVolume, 0, 100, step * 10); break;
                case 5: s.lookSpeed = clamp(s.lookSpeed, 1, 10, step); break;
                case 6: s.deadzone = clamp(s.deadzone, 0, 40, step * 5); break;
                case 7: s.invertLook = !s.invertLook; break;
                case 8: s.showFps = !s.showFps; break;
                case 9:
                    if (b == Button::A) { s = Settings(); show_toast(d, "Settings reset to defaults."); }
                    break;
            }
            if (!save_settings(d.root, s)) show_toast(d, "Could not save settings.");
            break;
        }

        case Screen::About:
            if (b == Button::B || b == Button::A) setScreen(Screen::Home);
            break;
    }
}

// ---------------------------------------------------------------- update

void App::update(float dt) {
    Impl &d = *d_;
    d.time += dt;
    d.fade = std::min(1.f, d.fade + dt * 6.f);
    if (d.toastT > 0) d.toastT -= dt;

    // Ease the focus bar towards its target.
    d.focusY += (d.focusTarget - d.focusY) * std::min(1.f, dt * 16.f);

    // Keep the importer's result in sync for the rescan when it finishes.
    if (d.sheetOpen && d.importer) {
        ImportStatus st = d.importer->status();
        if ((st.state == ImportStatus::Done) && d.last.state != ImportStatus::Done) rescan();
        d.last = st;
    }
}

// ---------------------------------------------------------------- drawing

static void draw_background(App::Impl &d) {
    fill_gradient(d.r, 0, 0, W, H, theme::bgTop, theme::bgBottom);
    // A faint red glow low on the left, like a candle out of frame.
    for (int i = 0; i < 6; ++i) {
        float rad = 900 - i * 120;
        fill_circle(d.r, 260, 1180, rad, rgba(0xC4283A, (Uint8)(5 + i)));
    }
    draw_vignette(d.r, (int)W, (int)H, 150);
}

static void draw_header(App::Impl &d, const char *sub) {
    d.f.title.draw(d.r, "NOCTURNE", MARGIN, 56, theme::text, 14);
    fill_rect(d.r, MARGIN, 176, 120, 4, theme::accent);
    d.f.small.draw(d.r, sub, MARGIN, 194, theme::dim);
}

static void draw_menu_item(App::Impl &d, float x, float y, float w, float h, const std::string &label, const std::string &sub,
                           bool focused, bool dimmed) {
    if (focused) {
        fill_round_rect(d.r, x, y, w, h, 18, theme::panelHi);
        fill_round_rect(d.r, x, y + 14, 8, h - 28, 4, theme::accent);
        stroke_round_rect(d.r, x, y, w, h, 18, 2, rgba(0xC4283A, 160));
    } else {
        fill_round_rect(d.r, x, y, w, h, 18, rgba(0x161922, 150));
    }
    Color c = dimmed ? theme::faint : (focused ? theme::text : rgba(0xBDB8AE));
    d.f.h2.draw(d.r, label, x + 40, y + (sub.empty() ? h / 2 - d.f.h2.lineHeight() / 2 : 14), c);
    if (!sub.empty()) d.f.small.draw(d.r, elide(d.f.small, sub, w - 80), x + 40, y + h - 40, dimmed ? theme::faint : theme::dim);
}

static void draw_component_row(App::Impl &d, const ComponentState &c, float x, float y, float w) {
    float h = 76;
    fill_round_rect(d.r, x, y, w, h, 16, rgba(0x12151C, 210));
    Color col = c.present ? theme::good : (c.comp->required ? theme::bad : theme::faint);
    fill_circle(d.r, x + 44, y + h / 2, 22, with_alpha(col, 40));
    if (c.present) draw_check(d.r, x + 44, y + h / 2, 34, col);
    else if (c.comp->required) draw_cross(d.r, x + 44, y + h / 2, 34, col);
    else draw_dash(d.r, x + 44, y + h / 2, 34, col);
    d.f.h2.draw(d.r, c.comp->title, x + 92, y + 4, theme::text);
    d.f.small.draw(d.r, c.comp->detail, x + 92, y + 42, theme::dim);
    std::string right = c.present ? std::to_string(c.files) + (c.files == 1 ? " file  \xC2\xB7  " : " files  \xC2\xB7  ") + human_bytes(c.bytes)
                                  : (c.comp->required ? "Missing" : "Not found");
    right_text(d.r, d.f.small, right, x + w - 32, y + h / 2 - d.f.small.lineHeight() / 2, c.present ? theme::dim : col);
}

static void draw_home(App::Impl &d, Settings &) {
    draw_header(d, "Xbox Edition  \xC2\xB7  unofficial community port");

    // Status pill, top right.
    if (d.scan.ready) pill(d.r, d.f, "Game files ready", W - MARGIN, 80, with_alpha(theme::good, 40), theme::good);
    else pill(d.r, d.f, "Game files needed", W - MARGIN, 80, with_alpha(theme::warn, 40), theme::warn);

    const char *labels[kHomeCount] = {"Play", "Game Files", "Settings", "About"};
    std::string subs[kHomeCount] = {
        d.scan.ready ? "Start the game" : "Add the game files to unlock",
        d.scan.ready ? "Manage and re-check your files" : "Add and check your Nocturne files",
        "Display, sound and controller options",
        "Version, credits and help"};
    float x = MARGIN, y0 = 300, w = 700, h = 128, gap = 18;
    for (int i = 0; i < kHomeCount; ++i) {
        float y = y0 + i * (h + gap);
        draw_menu_item(d, x, y, w, h, labels[i], subs[i], d.homeSel == i, i == 0 && !d.scan.ready);
    }

    // Right card: file summary.
    float cx = 860, cy = 300, cw = W - MARGIN - cx, ch = 4 * h + 3 * gap;
    panel(d.r, cx, cy, cw, ch);
    d.f.h2.draw(d.r, "Your game files", cx + 44, cy + 32, theme::text);
    float ry = cy + 100;
    for (auto &c : d.scan.items) {
        Color col = c.present ? theme::good : (c.comp->required ? theme::bad : theme::faint);
        fill_circle(d.r, cx + 62, ry + 20, 18, with_alpha(col, 40));
        if (c.present) draw_check(d.r, cx + 62, ry + 20, 28, col);
        else if (c.comp->required) draw_cross(d.r, cx + 62, ry + 20, 28, col);
        else draw_dash(d.r, cx + 62, ry + 20, 28, col);
        d.f.body.draw(d.r, c.comp->title, cx + 100, ry + 4, c.present ? theme::text : theme::dim);
        right_text(d.r, d.f.small, c.present ? human_bytes(c.bytes) : (c.comp->required ? "missing" : "optional"), cx + cw - 44, ry + 8,
                   c.present ? theme::dim : col);
        ry += 62;
    }
    fill_rect(d.r, cx + 44, ry + 8, cw - 88, 1.5f, theme::line);
    std::string msg = d.scan.ready ? "Everything the game needs is in place." : "Open Game Files to add what is missing.";
    d.f.small.draw(d.r, msg, cx + 44, ry + 26, d.scan.ready ? theme::good : theme::warn);
    d.f.tiny.draw(d.r, "Stored in:  " + d.folderLabel, cx + 44, cy + ch - 42, theme::faint);

    hints(d.r, d.f, {{'A', "Select"}, {'Y', "Play"}});
    d.focusTarget = y0 + d.homeSel * (h + gap);
}

static void draw_files(App::Impl &d) {
    draw_header(d, "Game files");
    pill(d.r, d.f, d.scan.ready ? "Ready to play" : std::to_string(d.scan.missingRequired) + " required missing", W - MARGIN, 80,
         with_alpha(d.scan.ready ? theme::good : theme::warn, 40), d.scan.ready ? theme::good : theme::warn);

    float lx = MARGIN, ly = 270, lw = 1060;
    float y = ly;
    for (auto &c : d.scan.items) {
        draw_component_row(d, c, lx, y, lw);
        y += 76 + 10;
    }
    // Actions
    float ay = y + 14;
    const char *labels[kFilesCount] = {"Import game files", "Rescan", };
    const char *subs[kFilesCount] = {"Copy your Nocturne folder into the app", "Check the folder again"};
    float firstAction = ay;
    for (int i = 0; i < kFilesCount; ++i) {
        draw_menu_item(d, lx, ay, lw, 92, labels[i], subs[i], d.filesSel == i, false);
        ay += 92 + 12;
    }

    // Help panel
    float px = lx + lw + 40, pw = W - MARGIN - px, py = ly, ph = 660;
    panel(d.r, px, py, pw, ph);
    d.f.h2.draw(d.r, "Adding your files", px + 36, py + 28, theme::text);
    float ty = py + 96;
    auto para = [&](const std::string &t, Color c, const Font &f) {
        for (auto &l : f.wrap(t, pw - 72)) { f.draw(d.r, l, px + 36, ty, c); ty += f.lineHeight() * 1.18f; }
        ty += 14;
    };
    para("No game data comes with this app. Copy the contents of your own Nocturne install so that art, models, data and sound sit directly in the app's folder.", theme::dim, d.f.small);
    para("Easiest: Import game files, then choose where your copy is.", theme::text, d.f.small);
    para("Always works: open the Xbox Device Portal in a web browser (https://<console address>:11443), go to File explorer, then LocalAppData, this app, LocalState, and upload the folders there. Then press Y here to rescan.", theme::dim, d.f.small);
    d.f.tiny.draw(d.r, "Total found:  " + std::to_string(d.scan.totalFiles) + " files, " + human_bytes(d.scan.totalBytes), px + 36, py + ph - 44, theme::faint);

    hints(d.r, d.f, {{'A', "Select"}, {'X', "Import"}, {'Y', "Rescan"}, {'B', "Back"}});
    d.focusTarget = firstAction;
}

static std::string percent(int v) { return std::to_string(v) + "%"; }

static void draw_settings(App::Impl &d, Settings &s) {
    draw_header(d, "Settings");
    struct Row { std::string label, value, note; bool toggle; };
    Resolution res = resolution_at(s.resolution);
    std::vector<Row> rows = {
        {"Resolution", std::to_string(res.w) + " x " + std::to_string(res.h), "", false},
        {"Screen fit", fit_name(s.fit), "", false},
        {"Intro movie", s.introMovie ? "On" : "Off", "", true},
        {"Music volume", percent(s.musicVolume), "", false},
        {"Movie volume", percent(s.movieVolume), "", false},
        {"Look speed", std::to_string(s.lookSpeed), "", false},
        {"Stick deadzone", percent(s.deadzone), "", false},
        {"Invert look", s.invertLook ? "On" : "Off", "", true},
        {"Show frame rate", s.showFps ? "On" : "Off", "", true},
        {"Reset to defaults", "", "", false},
    };
    float x = MARGIN, y0 = 236, w = 1180, h = 64, gap = 6;
    for (size_t i = 0; i < rows.size(); ++i) {
        float y = y0 + i * (h + gap);
        bool foc = d.settingsSel == (int)i;
        if (foc) {
            fill_round_rect(d.r, x, y, w, h, 16, theme::panelHi);
            fill_round_rect(d.r, x, y + 12, 7, h - 24, 3.5f, theme::accent);
            stroke_round_rect(d.r, x, y, w, h, 16, 2, rgba(0xC4283A, 160));
        } else {
            fill_round_rect(d.r, x, y, w, h, 16, rgba(0x161922, 150));
        }
        d.f.body.draw(d.r, rows[i].label, x + 36, y + h / 2 - d.f.body.lineHeight() / 2, foc ? theme::text : rgba(0xBDB8AE));
        if (!rows[i].value.empty()) {
            float vx = x + w - 36;
            Color vc = foc ? theme::text : theme::dim;
            float vw = d.f.body.width(rows[i].value);
            right_text(d.r, d.f.body, rows[i].value, vx - (foc ? 44 : 0), y + h / 2 - d.f.body.lineHeight() / 2, vc);
            if (foc) {
                draw_chevron(d.r, vx - vw - 44 - 26, y + h / 2, 30, false, theme::accent);
                draw_chevron(d.r, vx - 10, y + h / 2, 30, true, theme::accent);
            }
        } else if (foc) {
            right_text(d.r, d.f.small, "Press A", x + w - 36, y + h / 2 - d.f.small.lineHeight() / 2, theme::dim);
        }
    }

    // Side notes
    float px = x + w + 40, pw = W - MARGIN - px;
    panel(d.r, px, y0, pw, 5 * (h + gap) + 60);
    d.f.h2.draw(d.r, "Good to know", px + 32, y0 + 26, theme::text);
    float ty = y0 + 90;
    const char *notes[] = {
        "Changes save as soon as you make them and apply the next time you press Play.",
        "The game draws at its own resolution; Screen fit decides how that picture fills your TV.",
        "Look speed 5 and deadzone 20% match the game's defaults."};
    for (const char *n : notes) {
        for (auto &l : d.f.small.wrap(n, pw - 64)) { d.f.small.draw(d.r, l, px + 32, ty, theme::dim); ty += d.f.small.lineHeight() * 1.18f; }
        ty += 16;
    }

    hints(d.r, d.f, {{'A', "Change"}, {'B', "Back"}});
    d.focusTarget = y0 + d.settingsSel * (h + gap);
}

static void draw_about(App::Impl &d) {
    draw_header(d, "About");
    float x = MARGIN, y = 270, w = W - 2 * MARGIN, h = 640;
    panel(d.r, x, y, w, h);
    float ty = y + 40, tw = w - 100;
    auto para = [&](const std::string &t, const Font &f, Color c) {
        for (auto &l : f.wrap(t, tw)) { f.draw(d.r, l, x + 50, ty, c); ty += f.lineHeight() * 1.2f; }
        ty += 18;
    };
    para("Nocturne for Xbox (Dev Mode)", d.f.h2, theme::text);
    para("An unofficial, fan-made port of the community decompilation of Nocturne (Terminal Reality, 1999). It is not made, approved or supported by the original developers or publishers.", d.f.body, theme::dim);
    para("No game data is included. You need your own legitimate copy of the game; add its files from the Game Files screen.", d.f.body, theme::dim);
    para("This app is a work in progress and has not been fully tested on a console. If something fails, the launcher says what it could not do rather than failing silently.", d.f.body, theme::dim);
    para("Controls:  D-pad or left stick to move,  A to choose,  B to go back,  Y to start the game from the main screen.", d.f.body, theme::text);
    para("Interface font: Poppins, SIL Open Font License 1.1.", d.f.small, theme::faint);
    hints(d.r, d.f, {{'B', "Back"}});
}

static void draw_sheet(App::Impl &d) {
    fill_rect(d.r, 0, 0, W, H, rgba(0x000000, 190));
    float w = 1100, x = (W - w) / 2, y = 250;
    float h = 200 + (d.methods.empty() ? 80 : (float)d.methods.size() * 118) + 40;
    if (h < 440) h = 440;
    panel(d.r, x, y, w, h, true);
    ImportStatus st = d.importer ? d.importer->status() : ImportStatus();
    bool busy = st.state == ImportStatus::Copying || st.state == ImportStatus::Picking;
    bool finished = st.state == ImportStatus::Done || st.state == ImportStatus::Failed || st.state == ImportStatus::Cancelled;

    d.f.h1.draw(d.r, "Import game files", x + 48, y + 30, theme::text);
    if (!busy && !finished) {
        d.f.small.draw(d.r, "Where are your Nocturne files?", x + 48, y + 108, theme::dim);
        float ry = y + 160;
        for (size_t i = 0; i < d.methods.size(); ++i) {
            bool foc = d.sheetSel == (int)i;
            draw_menu_item(d, x + 36, ry, w - 72, 104, d.methods[i].title, d.methods[i].detail, foc, false);
            ry += 118;
        }
        if (d.methods.empty()) d.f.body.draw(d.r, "No import methods are available in this build.", x + 48, ry, theme::warn);
        hints(d.r, d.f, {{'A', "Choose"}, {'B', "Cancel"}});
    } else if (busy) {
        d.f.body.draw(d.r, st.state == ImportStatus::Picking ? "Waiting for you to choose a location..." : "Copying your files...", x + 48, y + 120, theme::text);
        float bw = w - 96, by = y + 210;
        fill_round_rect(d.r, x + 48, by, bw, 26, 13, rgba(0x0E1016));
        float frac = st.bytesTotal ? (float)((double)st.bytesDone / (double)st.bytesTotal) : 0;
        if (frac > 0) fill_round_rect(d.r, x + 48, by, std::max(26.f, bw * frac), 26, 13, theme::accent);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%d of %d files  \xC2\xB7  %s of %s", st.filesDone, st.filesTotal, human_bytes(st.bytesDone).c_str(),
                      human_bytes(st.bytesTotal).c_str());
        d.f.small.draw(d.r, st.filesTotal ? buf : st.message, x + 48, by + 46, theme::dim);
        std::string cur = st.current;
        if (d.f.small.width(cur) > bw) cur = "..." + cur.substr(cur.size() > 60 ? cur.size() - 60 : 0);
        d.f.small.draw(d.r, cur, x + 48, by + 92, theme::faint);
        hints(d.r, d.f, {{'B', "Cancel"}});
    } else {
        Color c = st.state == ImportStatus::Done ? theme::good : (st.state == ImportStatus::Failed ? theme::bad : theme::warn);
        const char *title = st.state == ImportStatus::Done ? "Import complete" : (st.state == ImportStatus::Failed ? "Import failed" : "Import cancelled");
        d.f.h2.draw(d.r, title, x + 48, y + 130, c);
        float ty = y + 200;
        for (auto &l : d.f.body.wrap(st.message, w - 96)) { d.f.body.draw(d.r, l, x + 48, ty, theme::dim); ty += d.f.body.lineHeight() * 1.2f; }
        if (st.state == ImportStatus::Failed)
            d.f.small.draw(d.r, "You can still copy the files with Device Portal (see the Game Files screen).", x + 48, ty + 20, theme::faint);
        hints(d.r, d.f, {{'A', "Continue"}});
    }
}

void App::draw() {
    Impl &d = *d_;
    SDL_Renderer *r = d.r;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    draw_background(d);

    g_suppressHints = d.sheetOpen;
    switch (screen_) {
        case Screen::Home: draw_home(d, settings_); break;
        case Screen::Files: draw_files(d); break;
        case Screen::Settings: draw_settings(d, settings_); break;
        case Screen::About: draw_about(d); break;
    }
    g_suppressHints = false;
    if (d.sheetOpen) draw_sheet(d);

    // Screen transition: fade in from dark.
    if (d.fade < 1) fill_rect(r, 0, 0, W, H, rgba(0x000000, (Uint8)((1 - d.fade) * 200)));

    if (d.toastT > 0) {
        float a = std::min(1.f, d.toastT / 0.4f);
        float w = d.f.small.width(d.toast) + 80, x = (W - w) / 2, y = H - 170;
        fill_round_rect(r, x, y, w, 64, 32, rgba(0x2A2F3C, (Uint8)(245 * a)));
        d.f.small.draw(r, d.toast, x + 40, y + 16, with_alpha(theme::text, (Uint8)(255 * a)));
    }
}

// ---------------------------------------------------------------- run loop

struct Repeater {
    Uint32 downAt = 0, lastAt = 0;
    bool held = false;
};

Result run(SDL_Window *window, SDL_Renderer *renderer, App &app) {
    (void)window;
    SDL_RenderSetLogicalSize(renderer, (int)W, (int)H);
    std::map<int, SDL_GameController *> pads;
    auto open_all = [&] {
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_IsGameController(i) && !pads.count(i)) pads[i] = SDL_GameControllerOpen(i);
    };
    open_all();

    Repeater rep[4];   // up down left right
    const Button dirs[4] = {Button::Up, Button::Down, Button::Left, Button::Right};
    auto dir_down = [&](int i, bool down) {
        Uint32 now = SDL_GetTicks();
        if (down && !rep[i].held) { rep[i] = {now, now, true}; app.press(dirs[i]); }
        if (!down) rep[i].held = false;
    };

    Uint32 prev = SDL_GetTicks();
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: return Result::Quit;
                case SDL_CONTROLLERDEVICEADDED: open_all(); break;
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP: {
                    bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
                    switch (e.cbutton.button) {
                        case SDL_CONTROLLER_BUTTON_DPAD_UP: dir_down(0, down); break;
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: dir_down(1, down); break;
                        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: dir_down(2, down); break;
                        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: dir_down(3, down); break;
                        case SDL_CONTROLLER_BUTTON_A: if (down) app.press(Button::A); break;
                        case SDL_CONTROLLER_BUTTON_B: if (down) app.press(Button::B); break;
                        case SDL_CONTROLLER_BUTTON_X: if (down) app.press(Button::X); break;
                        case SDL_CONTROLLER_BUTTON_Y: if (down) app.press(Button::Y); break;
                        case SDL_CONTROLLER_BUTTON_START: if (down) app.press(Button::Start); break;
                    }
                    break;
                }
                case SDL_CONTROLLERAXISMOTION: {
                    const int dz = 16000;
                    if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) { dir_down(0, e.caxis.value < -dz); dir_down(1, e.caxis.value > dz); }
                    if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) { dir_down(2, e.caxis.value < -dz); dir_down(3, e.caxis.value > dz); }
                    break;
                }
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    bool down = e.type == SDL_KEYDOWN;
                    switch (e.key.keysym.sym) {
                        case SDLK_UP: dir_down(0, down); break;
                        case SDLK_DOWN: dir_down(1, down); break;
                        case SDLK_LEFT: dir_down(2, down); break;
                        case SDLK_RIGHT: dir_down(3, down); break;
                        case SDLK_RETURN: if (down && !e.key.repeat) app.press(Button::A); break;
                        case SDLK_ESCAPE: case SDLK_BACKSPACE: if (down && !e.key.repeat) app.press(Button::B); break;
                        case SDLK_x: if (down && !e.key.repeat) app.press(Button::X); break;
                        case SDLK_y: if (down && !e.key.repeat) app.press(Button::Y); break;
                        case SDLK_SPACE: if (down && !e.key.repeat) app.press(Button::Start); break;
                    }
                    break;
                }
            }
        }
        // Held-direction auto-repeat: 400 ms delay, then every 110 ms (the game's own menu timing).
        Uint32 now = SDL_GetTicks();
        for (int i = 0; i < 4; ++i)
            if (rep[i].held && now - rep[i].downAt > 400 && now - rep[i].lastAt > 110) { rep[i].lastAt = now; app.press(dirs[i]); }

        app.update((now - prev) / 1000.f);
        prev = now;
        if (app.wantsPlay()) return Result::Play;
        if (app.wantsQuit()) return Result::Quit;
        app.draw();
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }
    for (auto &p : pads) if (p.second) SDL_GameControllerClose(p.second);
    return Result::Quit;
}

}  // namespace launcher
