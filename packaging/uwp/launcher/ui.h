// The launcher UI: Home, Game files, Settings, About. Driven entirely by abstract
// actions (up/down/left/right/accept/back) so a controller, a keyboard or a test
// script all work the same way; it draws itself into a 1920x1080 logical canvas.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "game_files.h"
#include "gfx.h"
#include "importer.h"
#include "settings.h"

namespace nl {

enum class Action { None, Up, Down, Left, Right, Accept, Back, Refresh };
enum class Screen { Home, Files, Settings, About };
enum class Result { Running, Play, Quit };

// Live controller state, shown on the About screen's controller test.
struct PadState {
    bool connected = false;
    std::string name;
    bool a = false, b = false, x = false, y = false;
    bool lb = false, rb = false, view = false, menu = false, ls = false, rs = false;
    bool up = false, down = false, left = false, right = false;
    float lx = 0, ly = 0, rx = 0, ry = 0;   // -1..1
    float lt = 0, rt = 0;                   // 0..1
};

struct LauncherConfig {
    std::string data_root;       // where the game's files live
    std::string version = "0.1";
    std::string build_note;      // e.g. the git commit
    bool show_window_mode = false;
};

class Launcher {
public:
    Launcher(Gfx &gfx, ImportService &import, LauncherConfig cfg);

    void handle(Action a);
    void tick(unsigned dt_ms);
    void render();
    void set_pad(const PadState &p) { pad_ = p; }

    Result result() const { return result_; }
    Screen screen() const { return screen_; }

    // Test hooks.
    void debug_goto(Screen s);
    void debug_open_import_help();
    void debug_focus(int i) { focus_[(int)screen_] = i; focus_y_ = -1; }
    bool in_modal() const { return modal_ != Modal::None; }

private:
    enum class Modal { None, Import, Help, NotReady };

    // input
    void accept();
    void back();
    void move(int dy);
    void adjust(int dx);
    void start_play();
    void rescan();
    void change_screen(Screen s);

    // drawing
    void draw_background();
    void draw_header();
    void draw_title(const char *title, const char *subtitle);
    void draw_hints();
    void draw_home();
    void draw_files();
    void draw_settings();
    void draw_about();
    void draw_modal();
    void card(int x, int y, int w, int h, Color c);
    void pill(int x, int y, const std::string &text, Color c);
    void chip(int cx, int cy, const std::string &label, Color c, bool lit = true);
    int hint(int x, int y, const std::string &btn, Color btn_color, const std::string &text);
    void highlight(int x, int w, int h, int target_y);
    std::vector<ImportMethod> file_actions() const;
    int files_action_count() const;

    Gfx &g_;
    ImportService &import_;
    LauncherConfig cfg_;
    Settings settings_;
    Inventory inv_;
    PadState pad_;

    Screen screen_ = Screen::Home;
    Modal modal_ = Modal::None;
    Result result_ = Result::Running;
    int focus_[4] = {0, 0, 0, 0};
    float focus_y_ = -1;
    float fade_ = 1.0f;
    unsigned clock_ms_ = 0;
    unsigned dt_ = 16;
    bool save_failed_ = false;
    int autostart_ms_ = -1;      // countdown, -1 = not running
    ImportProgress prog_;
    std::vector<ImportMethod> methods_;
    ImportState last_import_state_ = ImportState::Idle;
};

}  // namespace nl
