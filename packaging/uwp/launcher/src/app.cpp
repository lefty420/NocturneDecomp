#include "app.h"

#include "settings_store.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace fs = std::filesystem;

namespace nl {

namespace {

// Palette: ink-dark surfaces, a crimson accent, warm off-white text.
const Color kText(236, 233, 228);
const Color kMuted(141, 147, 163);
const Color kFaint(88, 94, 110);
const Color kPanel(18, 22, 32, 235);
const Color kPanel2(28, 33, 46);
const Color kBorder(40, 47, 64);
const Color kAccent(200, 50, 60);
const Color kAccentSoft(228, 86, 94);
const Color kOk(76, 175, 125);
const Color kWarn(224, 164, 58);
const Color kBad(224, 90, 90);

constexpr float kMarginX = 96;
constexpr float kContentY = 250;
constexpr float kContentH = 700;
constexpr float kContentW = kDesignW - 2 * kMarginX;

constexpr int kTabCount = 5;
const char *kTabNames[kTabCount] = {"Play", "Game files", "Settings", "The game", "About"};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

std::string pct(float f) {
    char b[16];
    std::snprintf(b, sizeof b, "%d%%", static_cast<int>(std::lround(f * 100)));
    return b;
}
std::string times(float f) {
    char b[16];
    std::snprintf(b, sizeof b, "%.2fx", f / 0.25f);   // 0.25 is the game's stock speed
    return b;
}

}  // namespace

void App::Ease::step(float dt) {
    v += (t - v) * (1.0f - std::exp(-dt * 16.0f));
    if (std::fabs(t - v) < 0.05f) v = t;
}

App::App(Gfx &gfx, const fs::path &data_root, std::vector<ImportProvider> providers)
    : g_(gfx), root_(data_root), settings_file_(data_root / "launcher.ini"), providers_(std::move(providers)) {
    settings_ = load_settings(settings_file_);
    start_scan();
}

App::~App() {
    if (job_) {
        job_->progress.cancel = true;
        if (job_->thread.joinable()) job_->thread.join();
    }
    if (scan_.valid()) scan_.wait();
    if (probe_.valid()) probe_.wait();
    if (cover_) SDL_DestroyTexture(cover_);
}

void App::start_scan() {
    if (scan_.valid()) return;
    const fs::path root = root_;
    scan_ = std::async(std::launch::async, [root] { return scan_game_files(root); });
}

void App::finish_scan_if_ready() {
    if (scan_.valid() && scan_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        scan_result_ = scan_.get();
        scanned_once_ = true;
    }
    // First time the files turn out to be missing: ask the providers that can look by themselves.
    if (scanned_once_ && !scan_result_.ready && !auto_probed_ && !job_ && modal_ == Modal::None) {
        auto_probed_ = true;
        std::vector<std::pair<size_t, std::function<std::string()>>> probes;
        for (size_t i = 0; i < providers_.size(); i++)
            if (providers_[i].probe) probes.emplace_back(i, providers_[i].probe);
        if (!probes.empty())
            probe_ = std::async(std::launch::async, [probes] {
                for (auto &pr : probes) {
                    std::string where = pr.second();
                    if (!where.empty()) return std::make_pair(pr.first, where);
                }
                return std::make_pair(static_cast<size_t>(0), std::string());
            });
    }
    if (probe_.valid() && probe_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        auto found = probe_.get();
        if (!found.second.empty() && !job_ && modal_ == Modal::None) {
            toast_text_ = "Found game files on " + found.second;
            toast_ = 2.5f;
            start_import(found.first);
        }
    }
}

void App::debug_wait_scan() {
    if (scan_.valid()) scan_.wait();
    finish_scan_if_ready();
}

void App::start_import(size_t idx) {
    if (job_ || idx >= providers_.size()) return;
    job_ = std::make_unique<Job>();
    job_->title = providers_[idx].title;
    Job *j = job_.get();
    const fs::path root = root_;
    auto run = providers_[idx].run;
    j->thread = std::thread([j, run, root] {
        j->outcome = run(j->progress, root);
        j->finished = true;
    });
    modal_ = Modal::Import;
}

void App::close_import() {
    if (job_) {
        if (job_->thread.joinable()) job_->thread.join();
        job_.reset();
    }
    modal_ = Modal::None;
    cover_tried_ = false;
    if (cover_) { SDL_DestroyTexture(cover_); cover_ = nullptr; }
    start_scan();
}

void App::show_message(const std::string &title, const std::string &body) {
    msg_title_ = title;
    msg_body_ = body;
    modal_ = Modal::Message;
}

void App::save() {
    if (save_settings(settings_file_, settings_)) {
        toast_text_ = "Saved";
    } else {
        toast_text_ = "Could not save settings";
    }
    toast_ = 1.6f;
}

void App::reset_settings() {
    nocturne_launcher_settings_defaults(&settings_);
    save();
}

void App::change_setting(int row, int dir) {
    switch (row) {
        case 0: settings_.stick_deadzone = clampf(std::round((settings_.stick_deadzone + dir * 0.01f) * 100) / 100, 0.05f, 0.40f); break;
        case 1: settings_.turn_rate = clampf(std::round((settings_.turn_rate + dir * 0.05f) * 100) / 100, 0.05f, 1.0f); break;
        case 2: settings_.look_rate = clampf(std::round((settings_.look_rate + dir * 0.05f) * 100) / 100, 0.05f, 1.0f); break;
        case 3: settings_.invert_look = settings_.invert_look ? 0 : 1; break;
        default: return;
    }
    save();
}

int App::content_rows() const {
    switch (tab_) {
        case 0: return 1;
        case 1: return static_cast<int>(providers_.size()) + 1;
        case 2: return 5;
        default: return 0;
    }
}

void App::handle(Input in) {
    if (modal_ == Modal::Message) {
        if (in == Input::Accept || in == Input::Back) modal_ = Modal::None;
        return;
    }
    if (modal_ == Modal::Import) {
        if (job_ && job_->finished) {
            if (in == Input::Accept || in == Input::Back) close_import();
        } else if (job_ && in == Input::Back) {
            job_->progress.cancel = true;
        }
        return;
    }

    if (in == Input::PrevTab || in == Input::NextTab) {
        tab_ = (tab_ + (in == Input::NextTab ? 1 : kTabCount - 1)) % kTabCount;
        focus_ = 0;
        return;
    }
    if (in_tabs_) {
        if (in == Input::Left) { tab_ = (tab_ + kTabCount - 1) % kTabCount; focus_ = 0; }
        else if (in == Input::Right) { tab_ = (tab_ + 1) % kTabCount; focus_ = 0; }
        else if ((in == Input::Down || in == Input::Accept) && content_rows() > 0) in_tabs_ = false;
        return;
    }
    const int rows = content_rows();
    if (in == Input::Up) {
        if (focus_ > 0) focus_--;
        else in_tabs_ = true;
        return;
    }
    if (in == Input::Down) {
        if (focus_ + 1 < rows) focus_++;
        return;
    }
    if (in == Input::Back) { in_tabs_ = true; return; }

    switch (tab_) {
        case 0:
            if (in == Input::Accept) {
                if (scan_result_.ready) result_ = Result::Play;
                else { tab_ = 1; focus_ = 0; }
            }
            break;
        case 1:
            if (in == Input::Accept) {
                if (focus_ < static_cast<int>(providers_.size())) {
                    const std::string why = providers_[focus_].unavailable_reason ? providers_[focus_].unavailable_reason() : "";
                    if (!why.empty()) show_message("Can't import from here", why);
                    else start_import(static_cast<size_t>(focus_));
                } else {
                    start_scan();
                    toast_text_ = "Checking your files again";
                    toast_ = 1.4f;
                }
            }
            break;
        case 2:
            if (focus_ <= 2) {
                if (in == Input::Left) change_setting(focus_, -1);
                else if (in == Input::Right) change_setting(focus_, +1);
            } else if (focus_ == 3) {
                if (in == Input::Accept || in == Input::Left || in == Input::Right) change_setting(3, 0);
            } else if (focus_ == 4 && in == Input::Accept) {
                reset_settings();
            }
            break;
        default: break;
    }
}

void App::update(float dt) {
    time_ += dt;
    if (toast_ > 0) toast_ -= dt;
    finish_scan_if_ready();
    tab_x_.step(dt);
    tab_w_.step(dt);
    focus_y_.step(dt);
}

// ---------------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------------

void App::panel(const Rect &r, const char *title) {
    g_.rrect(r, 28, kPanel);
    g_.rrect_outline(r, 28, 1.5f, kBorder);
    if (title) g_.text(r.x + 40, r.y + 32, title, 22, kMuted, FontFace::Bold, 3.0f);
}

void App::button(const Rect &r, const std::string &label, bool focused, bool primary, bool enabled) {
    Color fill = primary ? kAccent : kPanel2;
    Color txt = kText;
    if (!enabled) { fill = kPanel2; txt = kFaint; }
    if (focused) {
        const float pulse = 0.5f + 0.5f * std::sin(time_ * 4.0f);
        g_.rrect({r.x - 8, r.y - 8, r.w + 16, r.h + 16}, r.h * 0.5f + 8, kAccentSoft.alpha(0.18f + 0.12f * pulse));
        if (primary && enabled) fill = kAccentSoft;
    }
    g_.rrect(r, r.h * 0.5f, fill);
    if (focused) g_.rrect_outline(r, r.h * 0.5f, 3.0f, kText);
    g_.text_center(r.cx(), r.y + (r.h - 34) * 0.5f - 2, label, 30, txt, FontFace::Bold);
}

void App::pad_glyph(float x, float y, const char *label, Color c) {
    const bool shoulder = label[0] == 'L' || label[0] == 'R';
    if (shoulder) {
        const float w = g_.text_width(label, 18, FontFace::Bold) + 26;
        g_.rrect({x, y, w, 30}, 10, kPanel2);
        g_.rrect_outline({x, y, w, 30}, 10, 1.5f, kFaint);
        g_.text_center(x + w * 0.5f, y + 3, label, 18, kText, FontFace::Bold);
    } else {
        g_.circle(x + 15, y + 15, 15, c);
        g_.text_center(x + 15, y + 3, label, 19, Color(10, 12, 18), FontFace::Bold);
    }
}

void App::hint(float &x, float y, const char *glyph, Color c, const std::string &label) {
    pad_glyph(x, y, glyph, c);
    const bool shoulder = glyph[0] == 'L' || glyph[0] == 'R';
    const float gw = shoulder ? g_.text_width(glyph, 18, FontFace::Bold) + 26 : 30;
    g_.text(x + gw + 12, y + 1, label, 24, kMuted);
    x += gw + 12 + g_.text_width(label, 24) + 44;
}

void App::status_icon(float cx, float cy, CheckState s) {
    if (s == CheckState::Ok) {
        g_.circle(cx, cy, 18, kOk.alpha(0.22f));
        g_.line(cx - 8, cy + 1, cx - 2, cy + 7, 3.5f, kOk);
        g_.line(cx - 2, cy + 7, cx + 9, cy - 6, 3.5f, kOk);
    } else if (s == CheckState::Missing) {
        g_.circle(cx, cy, 18, kBad.alpha(0.22f));
        g_.line(cx - 7, cy - 7, cx + 7, cy + 7, 3.5f, kBad);
        g_.line(cx - 7, cy + 7, cx + 7, cy - 7, 3.5f, kBad);
    } else {
        g_.circle(cx, cy, 18, kFaint.alpha(0.3f));
        g_.line(cx - 8, cy, cx + 8, cy, 3.5f, kMuted);
    }
}

std::vector<std::string> App::wrap(const std::string &s, float width, int px) {
    std::vector<std::string> lines;
    std::string cur, word;
    auto flush_word = [&] {
        if (word.empty()) return;
        const std::string trial = cur.empty() ? word : cur + " " + word;
        if (g_.text_width(trial, px) <= width || cur.empty()) cur = trial;
        else { lines.push_back(cur); cur = word; }
        word.clear();
    };
    for (char c : s) {
        if (c == ' ') flush_word();
        else if (c == '\n') { flush_word(); lines.push_back(cur); cur.clear(); }
        else word.push_back(c);
    }
    flush_word();
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// ---------------------------------------------------------------------------------
// Chrome: header, tabs, footer
// ---------------------------------------------------------------------------------

void App::draw_header() {
    g_.text(kMarginX, 52, "NOCTURNE", 58, kText, FontFace::Bold, 10.0f);
    g_.rrect({kMarginX + 4, 124, 64, 4}, 2, kAccent);
    g_.text_right(kDesignW - kMarginX, 66, "Xbox port", 26, kMuted);
    g_.text_right(kDesignW - kMarginX, 98, "Unofficial community build", 20, kFaint);
}

void App::draw_tabs() {
    float x = kMarginX + 64;
    const float y = 150, h = 60, gap = 10;
    float sel_x = 0, sel_w = 0;
    float xs[kTabCount], ws[kTabCount];
    for (int i = 0; i < kTabCount; ++i) {
        ws[i] = g_.text_width(kTabNames[i], 28, FontFace::Bold) + 60;
        xs[i] = x;
        x += ws[i] + gap;
    }
    sel_x = xs[tab_];
    sel_w = ws[tab_];
    tab_x_.set(sel_x);
    tab_w_.set(sel_w);
    const Rect pill{tab_x_.v, y, tab_w_.v, h};
    g_.rrect(pill, h * 0.5f, in_tabs_ ? kAccent : kAccent.alpha(0.85f));
    if (in_tabs_) g_.rrect_outline(pill, h * 0.5f, 3.0f, kText);
    for (int i = 0; i < kTabCount; ++i) {
        g_.text_center(xs[i] + ws[i] * 0.5f, y + 13, kTabNames[i], 28, i == tab_ ? kText : kMuted, FontFace::Bold);
    }
    // Shoulder-button hints either side of the tabs.
    float hx = kMarginX;
    pad_glyph(hx, y + 15, "LB", kText);
    pad_glyph(xs[kTabCount - 1] + ws[kTabCount - 1] + 20, y + 15, "RB", kText);
}

void App::draw_footer() {
    float x = kMarginX;
    const float y = 988;
    if (modal_ == Modal::Message) {
        hint(x, y, "A", kOk, "OK");
    } else if (modal_ == Modal::Import) {
        if (job_ && job_->finished) hint(x, y, "A", kOk, "Close");
        else hint(x, y, "B", kBad, "Cancel");
    } else {
        hint(x, y, "A", kOk, "Select");
        if (tab_ == 2 && !in_tabs_ && focus_ <= 2) hint(x, y, "<>", kWarn, "Adjust");
        hint(x, y, "B", kBad, "Back to tabs");
        hint(x, y, "LB", kText, "");
        x -= 44 - 6;
        pad_glyph(x, y, "RB", kText);
        x += g_.text_width("RB", 18, FontFace::Bold) + 26 + 12;
        g_.text(x, y + 1, "Switch tab", 24, kMuted);
    }
}

// ---------------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------------

void App::draw_play() {
    const Rect left{kMarginX, kContentY, 1080, kContentH};
    const Rect right{kMarginX + 1080 + 32, kContentY, kContentW - 1080 - 32, kContentH};
    panel(left, "STATUS");
    if (!cover_tried_) { cover_tried_ = true; cover_ = g_.load_bmp((root_ / "cover.bmp").string(), &cover_w_, &cover_h_); }
    const bool has_cover = cover_ != nullptr;
    if (has_cover) {
        g_.rrect(right, 28, kPanel);
        g_.rrect_outline(right, 28, 1.5f, kBorder);
        draw_cover(right);
    } else {
        panel(right, "AT A GLANCE");
    }

    const bool ready = scanned_once_ && scan_result_.ready;
    const bool checking = !scanned_once_;
    std::string head = checking ? "Checking your game files" : ready ? "Ready to play" : "Your game files are needed";
    g_.text(left.x + 40, left.y + 96, head, 56, kText, FontFace::Bold);

    std::string sub;
    if (checking) {
        sub = "One moment...";
    } else if (ready) {
        sub = "Found " + format_count(scan_result_.total_files) + " files (" + format_bytes(scan_result_.total_bytes) +
              ") in the app's data folder.";
    } else {
        sub = "Nocturne's data is not included with this app. Add your own copy of the game's files from the "
              "Game files tab, then come back here.";
    }
    float y = left.y + 184;
    for (const std::string &line : wrap(sub, left.w - 80, 28)) {
        g_.text(left.x + 40, y, line, 28, kMuted);
        y += 42;
    }

    if (!checking) {
        const float top = left.y + 276;
        if (!ready) {
            const char *steps[3][2] = {{"Open Game files", "Choose how to bring your Nocturne folders in."},
                                       {"Let it copy", "If it is interrupted, it carries on where it stopped."},
                                       {"Come back and press Play", "This screen checks your files for you."}};
            for (int i = 0; i < 3; ++i) {
                const float sy = top + i * 74;
                g_.circle(left.x + 40 + 24, sy + 24, 24, kPanel2);
                g_.ring(left.x + 40 + 24, sy + 24, 24, 2, kAccent);
                g_.text_center(left.x + 40 + 24, sy + 9, std::to_string(i + 1), 26, kText, FontFace::Bold);
                g_.text(left.x + 120, sy - 2, steps[i][0], 28, kText, FontFace::Bold);
                g_.text(left.x + 120, sy + 34, steps[i][1], 21, kMuted);
            }
        } else {
            const float tw = (left.w - 80 - 2 * 24) / 3.0f;
            std::string extras = "Complete";
            Color ec = kOk;
            for (const CheckResult &c : scan_result_.items)
                if (c.state == CheckState::OptionalMissing) { extras = std::string(c.check->label) + " not found"; ec = kWarn; break; }
            const std::string vals[3] = {format_count(scan_result_.total_files), format_bytes(scan_result_.total_bytes), extras};
            const char *labs[3] = {"FILES", "SIZE", "EXTRAS"};
            const Color cols[3] = {kText, kText, ec};
            for (int i = 0; i < 3; ++i) {
                const Rect t{left.x + 40 + i * (tw + 24), top, tw, 150};
                g_.rrect(t, 22, kPanel2);
                g_.text(t.x + 24, t.y + 24, labs[i], 20, kMuted, FontFace::Bold, 2.5f);
                g_.text(t.x + 24, t.y + 70, g_.fit(vals[i], t.w - 48, i == 2 ? 26 : 44, FontFace::Bold), i == 2 ? 26 : 44, cols[i], FontFace::Bold);
            }
        }
    }

    const Rect btn{left.x + 40, left.y + left.h - 190, ready ? 360.0f : 470.0f, 96};
    const bool focused = !in_tabs_ && tab_ == 0;
    button(btn, ready ? "Play" : "Add game files", focused, true, !checking);
    if (!ready && !checking) {
        g_.text(btn.x + 4, btn.bottom() + 24, std::to_string(scan_result_.missing_required) + " required " +
                (scan_result_.missing_required == 1 ? "item is" : "items are") + " missing", 24, kWarn);
    } else if (ready) {
        g_.text(btn.x + 4, btn.bottom() + 24, "Controller settings are on the Settings tab", 24, kFaint);
    }

    // Right: a few facts (the box art takes this space when cover.bmp is present).
    auto row = [&](float ry, const char *label, const std::string &value, Color vc) {
        g_.text(right.x + 40, ry, label, 22, kMuted);
        g_.text(right.x + 40, ry + 32, g_.fit(value, right.w - 80, 28, FontFace::Medium), 28, vc);
    };
    if (!has_cover) {
    row(right.y + 100, "GAME FILES", checking ? "Checking..." : ready ? "All required files found" : "Incomplete",
        checking ? kMuted : ready ? kOk : kWarn);
    row(right.y + 220, "CONTROLLER", controller_.empty() ? "None detected" : controller_, controller_.empty() ? kWarn : kText);
    row(right.y + 340, "STICK DEADZONE", pct(settings_.stick_deadzone), kText);
    row(right.y + 460, "DATA FOLDER", root_.string(), kText);
    }
}

void App::draw_files() {
    const Rect left{kMarginX, kContentY, 980, kContentH};
    const Rect right{kMarginX + 980 + 32, kContentY, kContentW - 980 - 32, kContentH};
    panel(left, "REQUIRED FILES");
    panel(right, "ADD YOUR GAME FILES");

    const auto &checks = game_file_checks();
    const float rowh = 92;
    for (size_t i = 0; i < checks.size(); ++i) {
        const float ry = left.y + 90 + i * rowh;
        const CheckResult *r = (scan_result_.items.size() > i) ? &scan_result_.items[i] : nullptr;
        const CheckState st = r ? r->state : CheckState::OptionalMissing;
        status_icon(left.x + 70, ry + 34, scanned_once_ ? st : CheckState::OptionalMissing);
        g_.text(left.x + 112, ry + 6, checks[i].label, 30, kText);
        std::string detail = (checks[i].dir[0] ? std::string(checks[i].dir) + "/" : std::string(checks[i].pattern)) + (checks[i].required ? "" : "  (optional)");
        g_.text(left.x + 112, ry + 44, detail, 21, kFaint);
        std::string right_txt;
        Color rc = kMuted;
        if (!scanned_once_) right_txt = "...";
        else if (r && r->state == CheckState::Ok) { right_txt = format_count(r->files) + (r->files == 1 ? " file" : " files") + " · " + format_bytes(r->bytes); rc = kOk; }
        else if (r && r->state == CheckState::Missing) { right_txt = "Missing"; rc = kBad; }
        else right_txt = "Not found";
        g_.text_right(left.right() - 40, ry + 18, right_txt, 26, rc);
    }

    // Actions.
    const float ax = right.x + 32, aw = right.w - 64;
    float ay = right.y + 90;
    const int n = static_cast<int>(providers_.size());
    for (int i = 0; i <= n; ++i) {
        const bool rescan = i == n;
        const Rect r{ax, ay, aw, 100};
        const bool focused = !in_tabs_ && tab_ == 1 && focus_ == i;
        if (focused) {
            focus_y_.set(r.y);
            g_.rrect({r.x - 6, r.y - 6, r.w + 12, r.h + 12}, 26, kAccentSoft.alpha(0.2f));
        }
        g_.rrect(r, 20, focused ? kPanel2.mix(kAccent, 0.35f) : kPanel2);
        if (focused) g_.rrect_outline(r, 20, 3.0f, kText);
        const std::string title = rescan ? "Check my files again" : providers_[i].title;
        const std::string subt = rescan ? "After copying files in another way" : providers_[i].subtitle;
        g_.text(r.x + 28, r.y + 16, g_.fit(title, r.w - 56, 28, FontFace::Bold), 28, kText, FontFace::Bold);
        g_.text(r.x + 28, r.y + 58, g_.fit(subt, r.w - 56, 21), 21, kMuted);
        ay += 116;
    }

    // How to copy files without the app's help.
    const float ty = ay + 10;
    g_.text(ax, ty, "OR COPY THEM IN FROM A PC", 20, kMuted, FontFace::Bold, 2.5f);
    float yy = ty + 38;
    const std::string how =
        "Open Device Portal in a browser (https://<console-ip>:11443), choose File explorer, then LocalAppData, "
        "this app, LocalState, and upload your Nocturne folders. Then pick \"Check my files again\".";
    for (const std::string &line : wrap(how, aw, 21)) {
        g_.text(ax, yy, line, 21, kFaint);
        yy += 30;
    }
}

void App::draw_settings() {
    const Rect left{kMarginX, kContentY, 1100, kContentH};
    const Rect right{kMarginX + 1100 + 32, kContentY, kContentW - 1100 - 32, kContentH};
    panel(left, "CONTROLLER");
    panel(right, "LIVE PREVIEW");

    struct Slider { const char *label; float value; float lo, hi; std::string text; };
    const Slider sl[3] = {
        {"Stick deadzone", settings_.stick_deadzone, 0.05f, 0.40f, pct(settings_.stick_deadzone)},
        {"Turn speed", settings_.turn_rate, 0.05f, 1.0f, times(settings_.turn_rate)},
        {"Look up / down speed", settings_.look_rate, 0.05f, 1.0f, times(settings_.look_rate)},
    };
    float y = left.y + 84;
    for (int i = 0; i < 3; ++i) {
        const bool focused = !in_tabs_ && tab_ == 2 && focus_ == i;
        const Rect row{left.x + 24, y, left.w - 48, 120};
        if (focused) {
            g_.rrect(row, 22, kPanel2.mix(kAccent, 0.25f));
            g_.rrect_outline(row, 22, 3.0f, kText);
        }
        g_.text(row.x + 28, row.y + 14, sl[i].label, 30, kText);
        g_.text_right(row.right() - 28, row.y + 14, sl[i].text, 30, focused ? kAccentSoft : kMuted, FontFace::Bold);
        const Rect track{row.x + 28, row.y + 80, row.w - 56, 10};
        g_.rrect(track, 5, kFaint.alpha(0.5f));
        const float f = (sl[i].value - sl[i].lo) / (sl[i].hi - sl[i].lo);
        g_.rrect({track.x, track.y, std::max(10.0f, track.w * f), track.h}, 5, kAccent);
        g_.circle(track.x + track.w * f, track.cy(), focused ? 17.0f : 13.0f, focused ? kText : kMuted);
        y += 132;
    }
    // Toggle
    {
        const bool focused = !in_tabs_ && tab_ == 2 && focus_ == 3;
        const Rect row{left.x + 24, y, left.w - 48, 84};
        if (focused) { g_.rrect(row, 22, kPanel2.mix(kAccent, 0.25f)); g_.rrect_outline(row, 22, 3.0f, kText); }
        g_.text(row.x + 28, row.y + 22, "Invert look (up / down)", 30, kText);
        const Rect sw{row.right() - 28 - 92, row.y + 20, 92, 44};
        g_.rrect(sw, 22, settings_.invert_look ? kAccent : kFaint.alpha(0.6f));
        g_.circle(settings_.invert_look ? sw.right() - 22 : sw.x + 22, sw.cy(), 17, kText);
        y += 96;
    }
    // Reset
    {
        const bool focused = !in_tabs_ && tab_ == 2 && focus_ == 4;
        button({left.x + 40, y + 4, 380, 72}, "Reset to defaults", focused, false);
    }

    // Preview: the left stick, the deadzone, and what the game would see.
    const float cx = right.cx(), cy = right.y + 330, R = 200;
    g_.circle(cx, cy, R, kPanel2);
    g_.ring(cx, cy, R, 3, kFaint);
    g_.circle(cx, cy, R * settings_.stick_deadzone, kBad.alpha(0.28f));
    g_.ring(cx, cy, R * settings_.stick_deadzone, 2.5f, kBad.alpha(0.8f));
    g_.line(cx - R, cy, cx + R, cy, 1.5f, kBorder);
    g_.line(cx, cy - R, cx, cy + R, 1.5f, kBorder);
    // Raw and processed positions of the left stick.
    const float rx = lx_, ry = ly_;
    const float mag = std::sqrt(rx * rx + ry * ry);
    float px = 0, py = 0;
    if (mag > settings_.stick_deadzone) {
        const float m2 = std::min(mag, 1.0f);
        const float scaled = (m2 - settings_.stick_deadzone) / (1.0f - settings_.stick_deadzone);
        px = rx / mag * scaled;
        py = ry / mag * scaled;
    }
    g_.circle(cx + rx * R * 0.95f, cy + ry * R * 0.95f, 12, kMuted);
    g_.circle(cx + px * R * 0.95f, cy + py * R * 0.95f, 17, kAccentSoft);
    g_.text_center(cx, cy + R + 30, controller_.empty() ? "Connect a controller to try it" : "Move the left stick", 24, kMuted);
    float lx = right.x + 70;
    g_.circle(lx, cy + R + 96, 8, kMuted);
    g_.text(lx + 20, cy + R + 82, "Stick", 22, kMuted);
    g_.circle(lx + 130, cy + R + 96, 10, kAccentSoft);
    g_.text(lx + 152, cy + R + 82, "What the game sees", 22, kMuted);
}

// The box art, if the player has put cover.bmp in the data folder. It is not shipped with the app.
void App::draw_cover(const Rect &p) {
    if (!cover_tried_) {
        cover_tried_ = true;
        cover_ = g_.load_bmp((root_ / "cover.bmp").string(), &cover_w_, &cover_h_);
    }
    if (cover_ && cover_w_ > 0 && cover_h_ > 0) {
        const Rect box = p.inset(12);
        const float k = std::min(box.w / cover_w_, box.h / cover_h_);
        const Rect img{box.cx() - cover_w_ * k * 0.5f, box.cy() - cover_h_ * k * 0.5f, cover_w_ * k, cover_h_ * k};
        g_.rrect(img.inset(-3), 6, kBorder);
        g_.image(cover_, img);
    } else {
        g_.text_center(p.cx(), p.cy() - 60, "NOCTURNE", 44, kMuted, FontFace::Bold, 6.0f);
        int n = 0;
        for (const std::string &l : wrap("Put a picture named cover.bmp in the app's data folder to show the box art here.", p.w - 80, 22))
            g_.text_center(p.cx(), p.cy() + 10 + 32 * n++, l, 22, kFaint);
    }
}

void App::draw_story() {
    const Rect art{kMarginX, kContentY, 565, kContentH};
    const Rect text{art.right() + 32, kContentY, kContentW - 565 - 32, kContentH};
    g_.rrect(art, 28, kPanel);
    g_.rrect_outline(art, 28, 1.5f, kBorder);
    draw_cover(art);
    panel(text, "THE GAME");
    float y = text.y + 84;
    g_.text(text.x + 40, y, "Nocturne", 40, kText, FontFace::Bold);
    y += 62;
    auto body = [&](const std::string &s) {
        for (const std::string &l : wrap(s, text.w - 80, 23)) {
            g_.text(text.x + 40, y, l, 23, kMuted);
            y += 33;
        }
    };
    body("Nocturne is a 1999 survival horror game by Terminal Reality in which you play a monster hunter called The Stranger.");
    y += 18;
    g_.text(text.x + 40, y, "PLOT", 20, kAccentSoft, FontFace::Bold, 3.0f);
    y += 36;
    body("At the end of the Prohibition, and the beginning of the Great Depression in the United States of America, "
         "The Stranger is a mysterious operative of a secret organization known as Spookhouse, founded by President "
         "Theodore Roosevelt to combat supernatural creatures. He investigates four cases, which can be played in any "
         "order and involve teaming up with a half-vampire to retrieve a powerful artifact from a castle in Germany, "
         "liberating a Wild West town from a zombie assault, fighting reanimated mobsters created by the mafia boss "
         "Al Capone in Chicago, and solving the mystery of a house with deadly traps.");
}

void App::draw_about() {
    const Rect all{kMarginX, kContentY, kContentW, kContentH};
    panel(all, "ABOUT");
    float y = all.y + 90;
    auto para = [&](const std::string &head, const std::string &body) {
        g_.text(all.x + 40, y, head, 28, kText, FontFace::Bold);
        y += 44;
        for (const std::string &l : wrap(body, all.w - 80, 24)) {
            g_.text(all.x + 40, y, l, 24, kMuted);
            y += 34;
        }
        y += 22;
    };
    para("An unofficial port", "This is a community build of the NocturneDecomp project for Xbox in Developer Mode. "
                              "It is not affiliated with or endorsed by the game's original makers or publishers.");
    para("No game data included", "You need your own copy of Nocturne. Add its files from the Game files tab. "
                                  "Nothing is downloaded, and nothing is sent anywhere.");
    para("Your files live here", root_.string());
    para("Built with", "SDL2 (zlib), ANGLE (BSD), FreeType (FreeType License), FFmpeg (LGPL 2.1+), "
                       "stb_truetype (public domain) and the Poppins typeface (SIL Open Font License 1.1).");
}

void App::draw_modal() {
    if (modal_ == Modal::None) return;
    g_.rrect({-10, -10, kDesignW + 20, kDesignH + 20}, 0, Color(0, 0, 0, 170));
    float mh = 470;
    if (modal_ == Modal::Message) {
        const size_t n = wrap(msg_body_, 1060 - 96, 28).size();
        mh = std::max(330.0f, 124 + n * 42.0f + 48 + 76 + 36);
    }
    const Rect r{(kDesignW - 1060) * 0.5f, (kDesignH - mh) * 0.5f - 30, 1060, mh};
    g_.rrect({r.x - 4, r.y - 4, r.w + 8, r.h + 8}, 36, Color(0, 0, 0, 120));
    g_.rrect(r, 32, Color(22, 27, 39));
    g_.rrect_outline(r, 32, 2.0f, kBorder);

    if (modal_ == Modal::Message) {
        g_.text(r.x + 48, r.y + 44, msg_title_, 40, kText, FontFace::Bold);
        float y = r.y + 124;
        for (const std::string &l : wrap(msg_body_, r.w - 96, 28)) {
            g_.text(r.x + 48, y, l, 28, kMuted);
            y += 42;
        }
        button({r.right() - 48 - 240, r.bottom() - 48 - 76, 240, 76}, "OK", true);
        return;
    }
    if (!job_) return;
    CopyProgress &p = job_->progress;
    const bool fin = job_->finished;
    g_.text(r.x + 48, r.y + 44, job_->title, 36, kText, FontFace::Bold);

    const uint64_t total = p.bytes_total, done = p.bytes_done;
    float frac = total ? clampf(static_cast<float>(done) / static_cast<float>(total), 0, 1) : 0;
    const Rect bar{r.x + 48, r.y + 168, r.w - 96, 22};
    if (fin && job_->outcome == CopyOutcome::Done) frac = 1;
    g_.rrect(bar, 11, kFaint.alpha(0.5f));
    if (frac > 0) g_.rrect({bar.x, bar.y, std::max(22.0f, bar.w * frac), bar.h}, 11, fin && job_->outcome != CopyOutcome::Done ? kWarn : kAccent);
    g_.text(bar.x, bar.y + 40, std::to_string(static_cast<int>(frac * 100)) + "%", 28, kText, FontFace::Bold);
    g_.text_right(bar.right(), bar.y + 42, format_bytes(done) + " of " + format_bytes(total), 24, kMuted);

    std::string line1, line2;
    Color c1 = kMuted;
    if (!fin) {
        line1 = "Copying " + std::to_string(p.files_done.load()) + " of " + std::to_string(p.files_total.load()) + " files";
        line2 = g_.fit(p.current_copy(), r.w - 96, 24);
    } else if (job_->outcome == CopyOutcome::Done) {
        line1 = "All done. " + std::to_string(p.files_done.load()) + " files in place" +
                (p.files_skipped ? " (" + std::to_string(p.files_skipped.load()) + " were already there)." : ".");
        c1 = kOk;
    } else if (job_->outcome == CopyOutcome::Cancelled) {
        line1 = "Cancelled. Files already copied are kept, so you can pick up where you left off.";
        c1 = kWarn;
    } else {
        line1 = "That did not work.";
        line2 = p.error_copy();
        c1 = kBad;
    }
    float y = r.y + 100;
    g_.text(r.x + 48, y, g_.fit(line1, r.w - 96, 28), 28, c1);
    if (!line2.empty()) {
        float yy = bar.y + 100;
        for (const std::string &l : wrap(line2, r.w - 96, 24)) {
            g_.text(r.x + 48, yy, l, 24, fin ? kMuted : kFaint);
            yy += 34;
        }
    }
    if (fin) button({r.right() - 48 - 240, r.bottom() - 48 - 76, 240, 76}, "Close", true);
}

void App::draw_toast() {
    if (toast_ <= 0) return;
    const float a = clampf(toast_ / 0.4f, 0, 1);
    const float w = g_.text_width(toast_text_, 26, FontFace::Bold) + 72;
    const Rect r{(kDesignW - w) * 0.5f, 900, w, 60};
    g_.rrect(r, 30, Color(40, 47, 64).alpha(a));
    g_.rrect_outline(r, 30, 1.5f, kFaint.alpha(a));
    g_.text_center(r.cx(), r.y + 12, toast_text_, 26, kText.alpha(a), FontFace::Bold);
}

void App::draw() {
    g_.clear_background();
    draw_header();
    draw_tabs();
    switch (tab_) {
        case 0: draw_play(); break;
        case 1: draw_files(); break;
        case 2: draw_settings(); break;
        case 3: draw_story(); break;
        default: draw_about(); break;
    }
    draw_footer();
    draw_toast();
    draw_modal();
}

}  // namespace nl
