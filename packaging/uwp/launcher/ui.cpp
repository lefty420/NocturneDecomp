#include "ui.h"

#include "fs_util.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace nl {

namespace {

constexpr int W = 1920, H = 1080, MARGIN = 120;

const Color kBgTop = rgb(12, 14, 28), kBgBot = rgb(5, 6, 12);
const Color kCard = rgb(21, 25, 44, 235), kCardHi = rgb(38, 44, 76);
const Color kAccent = rgb(200, 56, 76);
const Color kText = rgb(236, 232, 226), kMuted = rgb(146, 152, 180), kDim = rgb(94, 100, 130);
const Color kOk = rgb(88, 200, 136), kWarn = rgb(238, 180, 68), kBad = rgb(228, 88, 100);
const Color kBtnA = rgb(110, 184, 90), kBtnB = rgb(222, 84, 84), kBtnX = rgb(82, 140, 230), kBtnY = rgb(238, 196, 64);

std::string fmt(const char *f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

}  // namespace

Launcher::Launcher(Gfx &gfx, ImportService &import, LauncherConfig cfg)
    : g_(gfx), import_(import), cfg_(std::move(cfg)), settings_(cfg_.data_root) {
    rescan();
    if (settings_.autostart() && inv_.ready) autostart_ms_ = 4000;
}

void Launcher::rescan() {
    inv_ = scan_game_files(cfg_.data_root);
    settings_.reload();
    methods_ = import_.methods();
}

int Launcher::files_action_count() const { return (int)methods_.size() + 2; }

void Launcher::change_screen(Screen s) {
    screen_ = s;
    fade_ = 1.0f;
    focus_y_ = -1;
}

void Launcher::debug_goto(Screen s) {
    modal_ = Modal::None;
    change_screen(s);
    fade_ = 0;
}

void Launcher::debug_open_import_help() { modal_ = Modal::Help; }

// ---- input ------------------------------------------------------------------

void Launcher::handle(Action a) {
    if (a == Action::None || result_ != Result::Running) return;
    if (autostart_ms_ >= 0) {   // any input cancels the countdown
        autostart_ms_ = -1;
        return;
    }
    switch (modal_) {
    case Modal::Import:
        if (prog_.state == ImportState::Working || prog_.state == ImportState::Choosing) {
            if (a == Action::Back) import_.cancel();
        } else if (a == Action::Accept || a == Action::Back) {
            import_.reset();
            prog_ = ImportProgress();
            modal_ = Modal::None;
            rescan();
        }
        return;
    case Modal::Help:
        if (a == Action::Accept || a == Action::Back) modal_ = Modal::None;
        return;
    case Modal::NotReady:
        if (a == Action::Accept) {
            modal_ = Modal::None;
            change_screen(Screen::Files);
        } else if (a == Action::Back) {
            modal_ = Modal::None;
        }
        return;
    case Modal::None:
        break;
    }
    switch (a) {
    case Action::Up: move(-1); break;
    case Action::Down: move(1); break;
    case Action::Left: adjust(-1); break;
    case Action::Right: adjust(1); break;
    case Action::Accept: accept(); break;
    case Action::Back: back(); break;
    case Action::Refresh:
        if (screen_ == Screen::Files) rescan();
        break;
    default: break;
    }
}

void Launcher::move(int dy) {
    int n = 1;
    switch (screen_) {
    case Screen::Home: n = 4; break;
    case Screen::Files: n = files_action_count(); break;
    case Screen::Settings: n = (int)settings_.defs().size(); break;
    case Screen::About: return;
    }
    int &f = focus_[(int)screen_];
    f = std::max(0, std::min(n - 1, f + dy));
}

void Launcher::adjust(int dx) {
    if (screen_ != Screen::Settings) return;
    save_failed_ = !settings_.adjust((size_t)focus_[(int)Screen::Settings], dx > 0 ? 1 : -1) && settings_.editable((size_t)focus_[(int)Screen::Settings]);
}

void Launcher::start_play() {
    if (inv_.ready) result_ = Result::Play;
    else modal_ = Modal::NotReady;
}

void Launcher::accept() {
    int f = focus_[(int)screen_];
    switch (screen_) {
    case Screen::Home:
        if (f == 0) start_play();
        else change_screen(f == 1 ? Screen::Files : f == 2 ? Screen::Settings : Screen::About);
        break;
    case Screen::Files: {
        int n = (int)methods_.size();
        if (f < n) {
            if (!methods_[(size_t)f].available) break;
            prog_ = ImportProgress();
            prog_.state = ImportState::Choosing;
            modal_ = Modal::Import;
            import_.start(methods_[(size_t)f].id);
        } else if (f == n) {
            rescan();
        } else {
            modal_ = Modal::Help;
        }
        break;
    }
    case Screen::Settings: adjust(1); break;
    case Screen::About: break;
    }
}

void Launcher::back() {
    if (screen_ != Screen::Home) change_screen(Screen::Home);
}

void Launcher::tick(unsigned dt_ms) {
    dt_ = dt_ms ? dt_ms : 1;
    clock_ms_ += dt_ms;
    fade_ = std::max(0.0f, fade_ - dt_ms / 220.0f);
    if (modal_ == Modal::Import) {
        prog_ = import_.progress();
        if (prog_.state == ImportState::Done && last_import_state_ != ImportState::Done) rescan();
        last_import_state_ = prog_.state;
    } else {
        last_import_state_ = ImportState::Idle;
    }
    if (autostart_ms_ >= 0) {
        autostart_ms_ -= (int)dt_ms;
        if (autostart_ms_ < 0) {
            autostart_ms_ = -1;
            start_play();
        }
    }
}

// ---- drawing helpers -----------------------------------------------------------

void Launcher::card(int x, int y, int w, int h, Color c) { g_.round_rect(x, y, w, h, 22, c); }

void Launcher::pill(int x, int y, const std::string &text, Color c) {
    int w = g_.text_width(text, 22, Weight::Bold, 1) + 56;
    g_.round_rect(x, y, w, 40, 20, with_alpha(c, 50));
    g_.circle(x + 20, y + 20, 6, c);
    g_.text(text, x + 36, y + 6, 22, Weight::Bold, c, 1);
}

void Launcher::chip(int cx, int cy, const std::string &label, Color c, bool lit) {
    g_.circle(cx, cy, 26, lit ? c : rgb(34, 38, 62));
    int px = label.size() > 2 ? 15 : 24;
    g_.text_center(label, cx, cy - g_.line_height(px, Weight::Bold) / 2, px, Weight::Bold, lit ? rgb(14, 16, 26) : kDim);
}

int Launcher::hint(int x, int y, const std::string &btn, Color btn_color, const std::string &text) {
    g_.circle(x + 20, y + 20, 20, btn_color);
    g_.text_center(btn, x + 20, y + 20 - g_.line_height(22, Weight::Bold) / 2, 22, Weight::Bold, rgb(14, 16, 26));
    g_.text(text, x + 52, y + 4, 26, Weight::Medium, kMuted);
    return x + 52 + g_.text_width(text, 26, Weight::Medium) + 48;
}

void Launcher::highlight(int x, int w, int h, int target_y) {
    if (focus_y_ < 0) focus_y_ = (float)target_y;
    float k = std::min(1.0f, dt_ / 85.0f);
    focus_y_ += ((float)target_y - focus_y_) * k;
    if (std::fabs(focus_y_ - target_y) < 0.5f) focus_y_ = (float)target_y;
    int y = (int)std::lround(focus_y_);
    g_.round_rect(x, y, w, h, 22, kCardHi);
    g_.round_rect(x, y + 20, 8, h - 40, 4, kAccent);
}

void Launcher::draw_background() {
    g_.gradient_v(0, 0, W, H, kBgTop, kBgBot);
    g_.glow(260, 1000, 900, rgb(120, 24, 48, 70));
    g_.glow(1700, 120, 640, rgb(40, 52, 130, 60));
    g_.crescent(1720, 190, 240, rgb(210, 214, 240, 22));
}

void Launcher::draw_header() {
    g_.crescent(MARGIN + 30, 92, 34, kText);
    g_.text("NOCTURNE", MARGIN + 86, 52, 54, Weight::Bold, kText, 12);
    g_.text("XBOX PORT   |   UNOFFICIAL", MARGIN + 88, 120, 20, Weight::Medium, kAccent, 4);
}

void Launcher::draw_title(const char *title, const char *subtitle) {
    g_.text(title, MARGIN, 190, 52, Weight::Bold, kText);
    g_.text(subtitle, MARGIN, 252, 26, Weight::Regular, kMuted);
}

void Launcher::draw_hints() {
    int y = 1000, x = MARGIN;
    g_.rect(MARGIN, 984, W - 2 * MARGIN, 1, rgb(255, 255, 255, 18));
    y = 1010;
    if (autostart_ms_ >= 0) {
        hint(x, y, "B", kBtnB, "Cancel auto-start");
        return;
    }
    switch (modal_) {
    case Modal::Import:
        if (prog_.state == ImportState::Working || prog_.state == ImportState::Choosing) hint(x, y, "B", kBtnB, "Cancel");
        else hint(x, y, "A", kBtnA, "OK");
        return;
    case Modal::Help: hint(hint(x, y, "A", kBtnA, "Close"), y, "B", kBtnB, "Close"); return;
    case Modal::NotReady: hint(hint(x, y, "A", kBtnA, "Go to Game files"), y, "B", kBtnB, "Close"); return;
    case Modal::None: break;
    }
    switch (screen_) {
    case Screen::Home: hint(x, y, "A", kBtnA, "Select"); break;
    case Screen::Files: x = hint(x, y, "A", kBtnA, "Select"); x = hint(x, y, "Y", kBtnY, "Rescan"); hint(x, y, "B", kBtnB, "Back"); break;
    case Screen::Settings: x = hint(x, y, "A", kBtnA, "Change"); x = hint(x, y, "<>", kBtnX, "Adjust"); hint(x, y, "B", kBtnB, "Back"); break;
    case Screen::About: hint(x, y, "B", kBtnB, "Back"); break;
    }
}

// ---- screens -------------------------------------------------------------------

void Launcher::draw_home() {
    struct Item { const char *title; std::string sub; Icon icon; };
    std::string files_sub;
    if (!inv_.any) files_sub = "No game files yet - add them here";
    else if (inv_.ready) files_sub = fmt("%d files, %s", inv_.total_files, human_size(inv_.total_bytes).c_str());
    else files_sub = fmt("%d of %d required files found", inv_.required_present, inv_.required_total);
    Item items[4] = {
        {"Play", inv_.ready ? "Start Nocturne" : "Add the game files first", Icon::Play},
        {"Game files", files_sub, Icon::Folder},
        {"Settings", "Resolution, gameplay and sound", Icon::Sliders},
        {"About", "Version, credits and controller test", Icon::Info},
    };
    const int x = MARGIN, y0 = 250, w = 820, h = 112, gap = 20;
    int f = focus_[(int)Screen::Home];
    for (int i = 0; i < 4; ++i) card(x, y0 + i * (h + gap), w, h, kCard);
    highlight(x, w, h, y0 + f * (h + gap));
    for (int i = 0; i < 4; ++i) {
        int y = y0 + i * (h + gap);
        bool on = i == f;
        bool dimmed = i == 0 && !inv_.ready;
        g_.icon(items[i].icon, x + 38, y + 28, 56, on ? kAccent : (dimmed ? kDim : kMuted));
        g_.text(items[i].title, x + 124, y + 16, 38, Weight::Medium, dimmed && !on ? kMuted : kText);
        g_.text(items[i].sub, x + 124, y + 66, 24, Weight::Regular, on ? kMuted : kDim);
    }

    // Status cards on the right.
    const int rx = 1000, rw = W - MARGIN - rx;
    card(rx, 250, rw, 300, kCard);
    g_.text("Game files", rx + 36, 276, 30, Weight::Bold, kText);
    Color sc = inv_.ready ? kOk : (inv_.any ? kWarn : kBad);
    pill(rx + 36, 330, inv_.ready ? "READY" : (inv_.any ? "INCOMPLETE" : "NOT INSTALLED"), sc);
    std::string l1, l2;
    if (inv_.ready) {
        l1 = "All required files found.";
        l2 = fmt("%d files, %s in the app's data folder.", inv_.total_files, human_size(inv_.total_bytes).c_str());
    } else if (inv_.any) {
        l1 = fmt("%d of %d required files found.", inv_.required_present, inv_.required_total);
        l2 = "Open Game files to see what is missing.";
    } else {
        l1 = "Nothing here yet.";
        l2 = "Add your own copy of Nocturne under Game files. This app does not include any game data.";
    }
    int ty = 400;
    for (const std::string &line : g_.wrap(l1 + "\n" + l2, 24, Weight::Regular, rw - 72)) {
        g_.text(line, rx + 36, ty, 24, Weight::Regular, line == l1 ? kText : kMuted);
        ty += 34;
    }

    card(rx, 574, rw, 184, kCard);
    g_.icon(Icon::Gamepad, rx + 36, 604, 52, pad_.connected ? kOk : kDim);
    g_.text("Controller", rx + 112, 600, 30, Weight::Bold, kText);
    g_.text(pad_.connected ? (pad_.name.empty() ? "Connected" : pad_.name) : "No controller detected", rx + 112, 644, 24, Weight::Regular,
            pad_.connected ? kMuted : kWarn);
    int cy = 684;
    for (const std::string &line : g_.wrap(pad_.connected ? "D-pad or left stick to move, A to select, B to go back." : "Connect one, or use the keyboard: arrows, Enter, Esc.", 22, Weight::Regular, rw - 72)) {
        g_.text(line, rx + 36, cy, 22, Weight::Regular, kDim);
        cy += 30;
    }

    if (autostart_ms_ >= 0) {
        card(x, 800, w, 120, rgb(60, 24, 36, 240));
        g_.text(fmt("Starting the game in %d...", autostart_ms_ / 1000 + 1), x + 36, 826, 32, Weight::Bold, kText);
        g_.text("Press any button to stay in the launcher.", x + 36, 874, 24, Weight::Regular, kMuted);
    }
}

void Launcher::draw_files() {
    draw_title("Game files", "Your own copy of Nocturne, stored in this app's data folder.");
    const int lx = MARGIN, lw = 860, top = 300;
    card(lx, top, lw, 640, kCard);
    g_.text("What the game needs", lx + 36, top + 26, 28, Weight::Bold, kText);
    int y = top + 84;
    for (const FileStatus &f : inv_.files) {
        Color c = f.present ? kOk : (f.need == Need::Required ? kBad : (f.need == Need::Recommended ? kWarn : kDim));
        g_.circle(lx + 56, y + 30, 22, with_alpha(c, 46));
        g_.icon(f.present ? Icon::Check : Icon::Cross, lx + 40, y + 14, 32, c);
        g_.text(f.rel, lx + 100, y + 4, 28, Weight::Medium, f.present ? kText : kMuted);
        const char *need = f.need == Need::Required ? "Required" : (f.need == Need::Recommended ? "Recommended" : "Optional");
        g_.text(std::string(need) + " - " + f.note, lx + 100, y + 40, 20, Weight::Regular, kDim);
        if (f.present) g_.text_right(human_size(f.size), lx + lw - 36, y + 12, 24, Weight::Regular, kMuted);
        y += 82;
    }
    if (!inv_.other_pods.empty()) {
        std::string s = "Also found: ";
        for (size_t i = 0; i < inv_.other_pods.size() && i < 6; ++i) s += (i ? ", " : "") + inv_.other_pods[i];
        g_.text(s, lx + 36, y + 4, 22, Weight::Regular, kMuted);
        y += 38;
    }
    g_.rect(lx + 36, 826, lw - 72, 1, rgb(255, 255, 255, 18));
    g_.text("Data folder", lx + 36, 842, 20, Weight::Medium, kDim, 2);
    int py = 872;
    for (const std::string &line : g_.wrap(cfg_.data_root.empty() ? "(not set)" : cfg_.data_root, 20, Weight::Regular, lw - 72)) {
        g_.text(line, lx + 36, py, 20, Weight::Regular, kMuted);
        py += 28;
        if (py > 920) break;
    }

    // Actions
    const int rx = 1020, rw = W - MARGIN - rx;
    g_.text("Add your game files", rx, top + 4, 30, Weight::Bold, kText);
    const int ay = top + 60, ah = 108, gap = 16;
    int n = files_action_count();
    int f = focus_[(int)Screen::Files];
    for (int i = 0; i < n; ++i) card(rx, ay + i * (ah + gap), rw, ah, kCard);
    highlight(rx, rw, ah, ay + f * (ah + gap));
    for (int i = 0; i < n; ++i) {
        int cy = ay + i * (ah + gap);
        bool on = i == f;
        std::string title, sub;
        Icon ic = Icon::Folder;
        bool avail = true;
        if (i < (int)methods_.size()) {
            const ImportMethod &m = methods_[(size_t)i];
            title = m.label;
            sub = m.available ? m.description : m.unavailable_reason;
            avail = m.available;
            ic = (m.id == "usb") ? Icon::Drive : Icon::Folder;
        } else if (i == (int)methods_.size()) {
            title = "Rescan";
            sub = "Look for files copied in by other means";
            ic = Icon::Refresh;
        } else {
            title = "Copy files from a computer";
            sub = "Step-by-step: Device Portal in a web browser";
            ic = Icon::Info;
        }
        g_.icon(ic, rx + 32, cy + 28, 52, !avail ? kDim : (on ? kAccent : kMuted));
        g_.text(title, rx + 110, cy + 16, 30, Weight::Medium, avail ? kText : kMuted);
        auto lines = g_.wrap(sub, 20, Weight::Regular, rw - 140);
        g_.text(lines.empty() ? "" : lines[0], rx + 110, cy + 58, 20, Weight::Regular, on ? kMuted : kDim);
    }
}

void Launcher::draw_settings() {
    draw_title("Settings", settings_.game_ini_present() ? "Saved straight into the game's own settings file." : "Game settings unlock after the game has run once.");
    const int lx = MARGIN, lw = 1040, top = 300, rh = 66, gap = 6;
    const auto &defs = settings_.defs();
    int f = focus_[(int)Screen::Settings];
    for (size_t i = 0; i < defs.size(); ++i) card(lx, top + (int)i * (rh + gap), lw, rh, kCard);
    highlight(lx, lw, rh, top + f * (rh + gap));
    for (size_t i = 0; i < defs.size(); ++i) {
        int y = top + (int)i * (rh + gap);
        bool on = (int)i == f, ed = settings_.editable(i);
        g_.text(defs[i].label, lx + 36, y + 14, 28, Weight::Medium, ed ? kText : kDim);
        std::string v = ed ? settings_.value_label(i) : "After first run";
        bool is_default = settings_.current_index(i) < 0;
        Color vc = !ed ? kDim : (is_default ? kMuted : (on ? kText : kText));
        int right = lx + lw - 36;
        if (on && ed) {
            g_.icon(Icon::ChevRight, right - 24, y + 17, 32, kAccent);
            right -= 40;
        }
        g_.text_right(v, right, y + 14, 28, Weight::Bold, vc);
        if (on && ed) g_.icon(Icon::ChevLeft, right - g_.text_width(v, 28, Weight::Bold) - 40, y + 17, 32, kAccent);
    }
    const int rx = lx + lw + 40, rw = W - MARGIN - rx;
    card(rx, top, rw, 330, kCard);
    g_.text(defs[(size_t)f].label, rx + 32, top + 26, 30, Weight::Bold, kText);
    int ty = top + 84;
    for (const std::string &l : g_.wrap(defs[(size_t)f].help, 22, Weight::Regular, rw - 64)) {
        g_.text(l, rx + 32, ty, 22, Weight::Regular, kMuted);
        ty += 32;
    }
    ty += 14;
    if (defs[(size_t)f].applies_next_launch) {
        g_.text("Applies the next time the game starts.", rx + 32, ty, 20, Weight::Medium, kWarn);
        ty += 30;
    }
    if (!settings_.editable((size_t)f)) {
        for (const std::string &l : g_.wrap("The game creates its settings file the first time it runs. Start the game once, then come back.", 20, Weight::Regular, rw - 64)) {
            g_.text(l, rx + 32, ty, 20, Weight::Regular, kWarn);
            ty += 28;
        }
    }
    if (save_failed_) g_.text("Could not save - the file is read-only?", rx + 32, top + 290, 20, Weight::Bold, kBad);
}

void Launcher::draw_about() {
    draw_title("About", "Version, credits and a controller check.");
    const int lx = MARGIN, lw = 840, top = 300;
    card(lx, top, lw, 640, kCard);
    g_.text("Nocturne", lx + 36, top + 26, 40, Weight::Bold, kText);
    g_.text("Xbox port - unofficial community build", lx + 36, top + 82, 24, Weight::Medium, kAccent);
    int y = top + 140;
    auto para = [&](const std::string &s, int px, Color c) {
        for (const std::string &l : g_.wrap(s, px, Weight::Regular, lw - 72)) {
            g_.text(l, lx + 36, y, px, Weight::Regular, c);
            y += px + 12;
        }
        y += 10;
    };
    para("Runs the NocturneDecomp reconstruction on Xbox in Developer Mode. This app contains none of the game's data: you need your own copy of Nocturne, added under Game files.", 22, kMuted);
    para("Version " + cfg_.version + (cfg_.build_note.empty() ? "" : "  (" + cfg_.build_note + ")"), 22, kText);
    para("Graphics: OpenGL ES 3.0 through ANGLE (Direct3D 11)", 20, kMuted);
    para("Built with SDL2, FFmpeg, ANGLE, FreeType. Interface font: Poppins (SIL Open Font License).", 20, kDim);

    g_.rect(lx + 36, top + 500, lw - 72, 1, rgb(255, 255, 255, 18));
    g_.text("Data folder", lx + 36, top + 516, 20, Weight::Medium, kDim, 2);
    int dy2 = top + 546;
    for (const std::string &l : g_.wrap(cfg_.data_root.empty() ? "(not set)" : cfg_.data_root, 20, Weight::Regular, lw - 72)) {
        g_.text(l, lx + 36, dy2, 20, Weight::Regular, kMuted);
        dy2 += 28;
        if (dy2 > top + 600) break;
    }

    const int rx = lx + lw + 40, rw = W - MARGIN - rx;
    card(rx, top, rw, 640, kCard);
    g_.text("Controller test", rx + 36, top + 26, 30, Weight::Bold, kText);
    g_.text(pad_.connected ? (pad_.name.empty() ? "Controller connected" : pad_.name) : "No controller detected", rx + 36, top + 74, 24, Weight::Regular,
            pad_.connected ? kOk : kWarn);
    const int cx = rx + rw / 2;
    // bumpers and triggers
    g_.round_rect(cx - 300, top + 150, 150, 44, 16, pad_.lb ? kBtnX : rgb(34, 38, 62));
    g_.text_center("LB", cx - 225, top + 156, 24, Weight::Bold, pad_.lb ? rgb(14, 16, 26) : kDim);
    g_.round_rect(cx + 150, top + 150, 150, 44, 16, pad_.rb ? kBtnX : rgb(34, 38, 62));
    g_.text_center("RB", cx + 225, top + 156, 24, Weight::Bold, pad_.rb ? rgb(14, 16, 26) : kDim);
    g_.round_rect(cx - 300, top + 120, 150, 14, 7, rgb(34, 38, 62));
    g_.round_rect(cx - 300, top + 120, std::max(14, (int)(150 * pad_.lt)), 14, 7, kAccent);
    g_.round_rect(cx + 150, top + 120, 150, 14, 7, rgb(34, 38, 62));
    g_.round_rect(cx + 150, top + 120, std::max(14, (int)(150 * pad_.rt)), 14, 7, kAccent);
    // d-pad
    int dx = cx - 230, dy = top + 330;
    chip(dx, dy - 60, "^", kText, pad_.up);
    chip(dx, dy + 60, "v", kText, pad_.down);
    chip(dx - 60, dy, "<", kText, pad_.left);
    chip(dx + 60, dy, ">", kText, pad_.right);
    // face buttons
    int fx = cx + 230, fy = top + 330;
    chip(fx, fy - 60, "Y", kBtnY, pad_.y);
    chip(fx, fy + 60, "A", kBtnA, pad_.a);
    chip(fx - 60, fy, "X", kBtnX, pad_.x);
    chip(fx + 60, fy, "B", kBtnB, pad_.b);
    // centre buttons
    chip(cx - 40, top + 330, "View", kText, pad_.view);
    chip(cx + 40, top + 330, "Menu", kText, pad_.menu);
    // sticks
    auto stick = [&](int sx, int sy, float ax, float ay, bool down) {
        g_.circle(sx, sy, 74, rgb(34, 38, 62));
        g_.circle(sx, sy, 68, rgb(21, 25, 44));
        g_.circle(sx + (int)(ax * 46), sy + (int)(ay * 46), 24, down ? kAccent : kText);
    };
    stick(cx - 130, top + 520, pad_.lx, pad_.ly, pad_.ls);
    stick(cx + 130, top + 520, pad_.rx, pad_.ry, pad_.rs);
    g_.text_center("Press buttons and move the sticks to see them respond.", cx, top + 600, 20, Weight::Regular, kDim);
}

void Launcher::draw_modal() {
    if (modal_ == Modal::None) return;
    g_.rect(0, 0, W, H, rgb(2, 3, 8, 190));
    int missing = 0;
    for (const FileStatus &f : inv_.files)
        if (f.need == Need::Required && !f.present) ++missing;
    const bool busy_import = modal_ == Modal::Import && (prog_.state == ImportState::Working || prog_.state == ImportState::Choosing);
    const int mh = modal_ == Modal::Help ? 600 : (modal_ == Modal::NotReady ? 230 + 44 * missing : (busy_import ? 420 : 300));
    const int mw = 1180, mx = (W - mw) / 2, my = (H - mh) / 2 - 20;
    card(mx, my, mw, mh, rgb(26, 30, 54));
    g_.round_rect(mx, my + 30, 8, 60, 4, kAccent);

    if (modal_ == Modal::Import) {
        const bool busy = prog_.state == ImportState::Working || prog_.state == ImportState::Choosing;
        const char *title = busy ? "Adding game files" : (prog_.state == ImportState::Done ? "Done" : (prog_.state == ImportState::Cancelled ? "Cancelled" : "Could not add the files"));
        Color tc = busy ? kText : (prog_.state == ImportState::Done ? kOk : (prog_.state == ImportState::Cancelled ? kWarn : kBad));
        g_.text(title, mx + 48, my + 36, 40, Weight::Bold, tc);
        std::string msg = prog_.message;
        if (prog_.state == ImportState::Done && inv_.ready) msg += " All the required files are in place - choose Play on the home screen.";
        else if (prog_.state == ImportState::Done) msg += " Some required files are still missing - see the checklist.";
        if (prog_.state == ImportState::Choosing) msg = "Waiting for you to choose a location...";
        else if (prog_.state == ImportState::Working) msg = prog_.current.empty() ? "Preparing..." : prog_.current;
        int y = my + 110;
        for (const std::string &l : g_.wrap(msg, 26, Weight::Regular, mw - 96)) {
            g_.text(l, mx + 48, y, 26, Weight::Regular, busy ? kMuted : kText);
            y += 38;
        }
        if (prog_.state == ImportState::Working && prog_.bytes_total > 0) {
            float frac = std::min(1.0f, (float)((double)prog_.bytes_done / (double)prog_.bytes_total));
            const int bx = mx + 48, bw = mw - 96, by = my + mh - 150;
            g_.round_rect(bx, by, bw, 22, 11, rgb(40, 46, 78));
            g_.round_rect(bx, by, std::max(22, (int)(bw * frac)), 22, 11, kAccent);
            g_.text(fmt("%d of %d files   |   %s of %s", prog_.files_done, prog_.files_total, human_size(prog_.bytes_done).c_str(),
                        human_size(prog_.bytes_total).c_str()), bx, by + 40, 22, Weight::Regular, kMuted);
        }
    } else if (modal_ == Modal::Help) {
        g_.text("Copy files from a computer", mx + 48, my + 36, 40, Weight::Bold, kText);
        const char *steps[] = {
            "On the Xbox, open Dev Home and note the console's IP address.",
            "On a computer on the same network, open https://<console-ip>:11443 in a browser and sign in to Device Portal.",
            "Open the File Explorer section, then LocalAppData, then this app, then LocalState.",
            "Upload your game files there (ACT1.POD, ENEMY.POD ...), keeping folders such as VIDEO and SYSTEM as they are.",
            "Come back here and choose Rescan. The Game files screen shows what was found.",
        };
        int y = my + 110;
        for (int i = 0; i < 5; ++i) {
            g_.circle(mx + 70, y + 20, 18, kAccent);
            g_.text_center(std::to_string(i + 1), mx + 70, y + 20 - g_.line_height(22, Weight::Bold) / 2, 22, Weight::Bold, rgb(14, 16, 26));
            int ly = y;
            for (const std::string &l : g_.wrap(steps[i], 24, Weight::Regular, mw - 190)) {
                g_.text(l, mx + 112, ly + 4, 24, Weight::Regular, kText);
                ly += 34;
            }
            y = std::max(ly, y + 40) + 18;
        }
    } else {
        g_.text("Game files missing", mx + 48, my + 36, 40, Weight::Bold, kWarn);
        int y = my + 110;
        for (const std::string &l : g_.wrap("The game can't start yet. These required files were not found:", 26, Weight::Regular, mw - 96)) {
            g_.text(l, mx + 48, y, 26, Weight::Regular, kMuted);
            y += 38;
        }
        y += 8;
        for (const FileStatus &f : inv_.files) {
            if (f.need != Need::Required || f.present) continue;
            g_.icon(Icon::Cross, mx + 48, y + 2, 30, kBad);
            g_.text(f.rel, mx + 92, y, 28, Weight::Medium, kText);
            y += 44;
        }
    }
}

void Launcher::render() {
    draw_background();
    draw_header();
    switch (screen_) {
    case Screen::Home: draw_home(); break;
    case Screen::Files: draw_files(); break;
    case Screen::Settings: draw_settings(); break;
    case Screen::About: draw_about(); break;
    }
    draw_modal();
    draw_hints();
    if (fade_ > 0.0f) g_.rect(0, 0, W, H, rgb(5, 6, 12, (int)(fade_ * 200)));
}

}  // namespace nl
