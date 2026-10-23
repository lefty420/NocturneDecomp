#pragma once
// The launcher: Play, Game files, Settings, About. Driven entirely by a gamepad (the
// Xbox has no mouse); a keyboard maps onto the same inputs for testing.

#include "game_files.h"
#include "gfx.h"
#include "imports.h"

#include "core/launcher_settings.h"

#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace nl {

enum class Input { Up, Down, Left, Right, Accept, Back, PrevTab, NextTab };
enum class Result { Running, Play };

class App {
public:
    App(Gfx &gfx, const std::filesystem::path &data_root, std::vector<ImportProvider> providers);
    ~App();

    void handle(Input in);
    void set_sticks(float lx, float ly, float rx, float ry) { lx_ = lx; ly_ = ly; rx_ = rx; ry_ = ry; }
    void set_controller_name(const std::string &n) { controller_ = n; }
    void update(float dt);
    void draw();
    Result result() const { return result_; }

    // Test hooks.
    void debug_select_tab(int t) { tab_ = t; in_tabs_ = false; focus_ = 0; }
    void debug_set_focus(int f) { focus_ = f; }
    bool scanning() const { return scan_.valid(); }
    void debug_wait_scan();
    // Start the game by itself, after a short countdown, once the files are found (until cancelled).
    void set_auto_start(bool on) { auto_allowed_ = on; }
    void cancel_auto_start() { auto_done_ = true; }
    const ScanResult &scan_result() const { return scan_result_; }
    bool importing() const { return job_ != nullptr; }

private:
    enum class Modal { None, Import, Message };

    struct Job {
        CopyProgress progress;
        std::thread thread;
        CopyOutcome outcome = CopyOutcome::Failed;
        std::atomic<bool> finished{false};
        std::string title;
    };

    // Animated value that eases toward a target.
    struct Ease {
        float v = 0, t = 0;
        bool init = false;
        void set(float target) { t = target; if (!init) { v = target; init = true; } }
        void step(float dt);
    };

    void start_scan();
    void finish_scan_if_ready();
    void start_import(size_t provider);
    void close_import();
    void show_message(const std::string &title, const std::string &body);
    void change_setting(int row, int dir);
    void save();
    void reset_settings();

    void draw_header();
    void draw_tabs();
    void draw_footer();
    void draw_play();
    void draw_files();
    void draw_settings();
    void draw_about();
    void draw_story();
    void draw_auto_start();
    void draw_cover(const Rect &panel_rect);
    SDL_Texture *load_cover();
    SDL_Texture *cover_ = nullptr;   // optional box art: cover.bmp in the data folder
    int cover_w_ = 0, cover_h_ = 0;
    bool cover_tried_ = false;
    void draw_modal();
    void draw_toast();

    void panel(const Rect &r, const char *title = nullptr);
    void button(const Rect &r, const std::string &label, bool focused, bool primary = true, bool enabled = true);
    void pad_glyph(float x, float y, const char *label, Color c);
    void hint(float &x, float y, const char *glyph, Color c, const std::string &label);
    void status_icon(float cx, float cy, CheckState s);
    std::vector<std::string> wrap(const std::string &s, float width, int px);
    int content_rows() const;

    Gfx &g_;
    std::filesystem::path root_;
    std::filesystem::path settings_file_;
    std::vector<ImportProvider> providers_;

    int tab_ = 0;
    bool in_tabs_ = false;
    int focus_ = 0;
    Modal modal_ = Modal::None;
    std::string msg_title_, msg_body_;
    float time_ = 0;
    float toast_ = 0;
    std::string toast_text_;

    NocturneLauncherSettings settings_;
    float lx_ = 0, ly_ = 0, rx_ = 0, ry_ = 0;
    std::string controller_;

    std::future<ScanResult> scan_;
    ScanResult scan_result_;
    bool scanned_once_ = false;
    bool auto_allowed_ = false;
    bool auto_done_ = false;
    float auto_timer_ = -1.0f;                 // seconds left before the game starts; <0 = not counting
    bool auto_probed_ = false;                 // the start-up drive check has been started
    // The start-up drive check runs on a detached thread: on a console the removable-storage call can
    // block indefinitely, and the launcher must still be able to close and start the game.
    struct ProbeState {
        std::mutex m;
        bool done = false;
        size_t index = 0;
        std::string where;
    };
    std::shared_ptr<ProbeState> probe_;

    std::unique_ptr<Job> job_;
    Ease tab_x_, tab_w_, focus_y_;
    Result result_ = Result::Running;
};

}  // namespace nl
