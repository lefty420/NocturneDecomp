// Renders every launcher screen into BMP files without a display, and drives the real
// import / scan / settings code against temporary folders. Linux-only dev tool.
//
//   harness <fonts_dir> <out_dir>

#include "../src/app.h"
#include "../src/settings_store.h"
#include "../src/default_config.h"

#include <chrono>
#include <cmath>
#include <vector>
#include <iterator>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
using namespace nl;

static int g_fail = 0;
#define EXPECT(c)                                                                  \
    do {                                                                           \
        if (!(c)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } \
    } while (0)

static void touch(const fs::path &p, size_t bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    std::string s(bytes, 'x');
    o << s;
}

static void make_game(const fs::path &root) {
    // The real layout: a flat folder of .POD archives plus System / video folders and installer leftovers.
    for (int i = 1; i <= 5; ++i) touch(root / ("ACT" + std::to_string(i) + ".POD"), 50000);
    for (int i = 1; i <= 5; ++i) touch(root / ("ACT" + std::to_string(i) + "SND.pod"), 30000);
    for (int i = 0; i < 30; ++i) touch(root / ("misc" + std::to_string(i) + ".pod"), 8000);
    for (int i = 0; i < 3; ++i) touch(root / "System" / ("s" + std::to_string(i) + ".ini"), 500);
    touch(root / "video" / "intro.avi", 20000);
    touch(root / "nocturne.exe", 1000);   // not copied
    touch(root / "tri3dfx.dll", 1000);    // not copied
}

struct Shot {
    SDL_Surface *surf;
    SDL_Renderer *r;
    Gfx *gfx;
    std::string dir;
    void save(App &app, const char *name) {
        app.draw();                                        // lets the tab pill learn its target
        app.update(0.016f);
        for (int i = 0; i < 40; ++i) app.update(0.05f);   // let animations settle
        app.draw();
        SDL_RenderPresent(r);
        const std::string path = dir + "/" + name + ".bmp";
        SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, out->pixels, out->pitch);
        SDL_SaveBMP(out, path.c_str());
        SDL_FreeSurface(out);
    }
};

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    const std::string fonts = argv[1], outdir = argv[2];
    fs::create_directories(outdir);
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, 1920, 1080, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surf);
    Gfx gfx;
    if (!gfx.init(r, fonts)) { std::fprintf(stderr, "fonts failed to load from %s\n", fonts.c_str()); return 1; }
    gfx.set_output_size(1920, 1080);
    Shot shot{surf, r, &gfx, outdir};

    const fs::path tmp = fs::temp_directory_path() / "nl_harness";
    // ---- Scenario 5: shipped defaults merge into the player's ini --------------------------------
    {
        const std::string existing = "[Graphics]\r\ngamePIXX=640\r\ngamePIXY=480\r\ngamma=5\r\n\r\n[Control]\r\nkeyWalk=17\r\n";
        const std::string preset = "; defaults-version: 3\n[Graphics]\ngamePIXX=1600\nGAMEPIXY=1200\nuseDirect3D=1\n[Control]\nkeyRun=42\n[Sound]\nvolume=7\n";
        const std::string m = merge_ini(existing, preset);
        EXPECT(preset_version(preset) == 3);
        EXPECT(m.find("gamePIXX=1600") != std::string::npos);
        EXPECT(m.find("gamePIXY=1200") != std::string::npos);          // key matched ignoring case, player's spelling kept
        EXPECT(m.find("gamma=5") != std::string::npos);                // untouched
        EXPECT(m.find("keyWalk=17") != std::string::npos);
        EXPECT(m.find("[Sound]\r\nvolume=7") != std::string::npos);    // new section appended, CRLF kept
        // a new key lands inside its section, before the next one
        EXPECT(m.find("useDirect3D=1") < m.find("[Control]"));
        EXPECT(m.find("keyRun=42") > m.find("[Control]") && m.find("keyRun=42") < m.find("[Sound]"));
        EXPECT(merge_ini(m, preset) == m);                              // applying twice changes nothing

        const fs::path d = tmp / "defaults_test";
        fs::create_directories(d);
        write_text_atomic(d / "preset.ini", preset);
        write_text_atomic(d / "target.ini", existing);
        EXPECT(apply_preset(d / "preset.ini", d / "target.ini", false, d / "marker") == ApplyResult::Applied);
        std::string after;
        read_text(d / "target.ini", &after);
        EXPECT(after == m);
        // the player changes something afterwards: it must not be undone
        write_text_atomic(d / "target.ini", "[Graphics]\ngamePIXX=800\n");
        EXPECT(apply_preset(d / "preset.ini", d / "target.ini", false, d / "marker") == ApplyResult::AlreadyApplied);
        read_text(d / "target.ini", &after);
        EXPECT(after == "[Graphics]\ngamePIXX=800\n");
        // a newer preset version is applied once more
        write_text_atomic(d / "preset.ini", "; defaults-version: 4\n[Graphics]\ngamePIXX=1280\n");
        EXPECT(apply_preset(d / "preset.ini", d / "target.ini", false, d / "marker") == ApplyResult::Applied);
        // no target and not allowed to create: nothing happens and the marker is not set
        EXPECT(apply_preset(d / "preset.ini", d / "missing.ini", false, d / "marker2") == ApplyResult::TargetMissing);
        EXPECT(!fs::exists(d / "marker2"));
    }

    fs::remove_all(tmp);

    // ---- Scenario 1: nothing installed ------------------------------------------------
    {
        const fs::path data = tmp / "empty";
        fs::create_directories(data);
        App app(gfx, data, {});
        app.debug_wait_scan();
        EXPECT(!app.scan_result().ready);
        EXPECT(app.scan_result().missing_required == 3);
        shot.save(app, "01_play_missing");
        app.handle(Input::Accept);          // "Add game files" jumps to the Files tab
        shot.save(app, "02_files_missing");
    }

    // ---- Scenario 2: import from a folder, with a nested install ---------------------
    {
        const fs::path src = tmp / "usb" / "Games" / "Nocturne";
        make_game(src);
        touch(src / "video" / "opening.avi", 90000);
        ::setenv("NOCTURNE_IMPORT_DIR", (tmp / "usb").string().c_str(), 1);
        EXPECT(find_game_root(tmp / "usb") == src);

        const fs::path data = tmp / "data";
        fs::create_directories(data);
        std::vector<ImportProvider> prov = make_portable_providers();
        // A slow provider so the progress screen can be captured mid-copy.
        ImportProvider slow;
        slow.title = "Import from a USB drive";
        slow.subtitle = "Copies the game folder from a drive plugged into the console";
        slow.unavailable_reason = [] { return std::string(); };
        slow.run = [&](CopyProgress &p, const fs::path &) -> CopyOutcome {
            p.bytes_total = 800ull << 20;
            p.files_total = 9100;
            for (int i = 0; i < 600 && !p.cancel; ++i) {
                p.bytes_done = (p.bytes_total * i) / 1000;
                p.files_done = i * 9;
                p.set_current("models/hero_" + std::to_string(i) + ".bin");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return p.cancel ? CopyOutcome::Cancelled : CopyOutcome::Done;
        };
        ImportProvider refused;
        refused.title = "Pick a folder with the file picker";
        refused.subtitle = "Uses the system file picker";
        refused.unavailable_reason = [] {
            return std::string("The console refused the file picker. Xbox limits folder pickers, so use Device Portal "
                               "instead (the steps are on this screen).");
        };
        refused.run = [](CopyProgress &, const fs::path &) { return CopyOutcome::Failed; };
        std::vector<ImportProvider> all;
        all.push_back(prov[0]);
        all.push_back(slow);
        all.push_back(refused);

        App app(gfx, data, all);
        app.debug_wait_scan();
        app.debug_select_tab(1);
        shot.save(app, "03_files_actions");

        // Refused provider -> message.
        app.debug_set_focus(2);
        app.handle(Input::Accept);
        shot.save(app, "04_files_refused");
        app.handle(Input::Accept);

        // Slow provider: capture progress, then cancel.
        app.debug_set_focus(1);
        app.handle(Input::Accept);
        EXPECT(app.importing());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        shot.save(app, "05_import_progress");
        app.handle(Input::Back);                 // cancel
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        shot.save(app, "06_import_cancelled");
        app.handle(Input::Accept);               // close

        // Real provider: copies the nested install for real.
        app.debug_set_focus(0);
        app.handle(Input::Accept);
        for (int i = 0; i < 200 && app.importing(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            app.update(0.02f);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        shot.save(app, "07_import_done");
        app.handle(Input::Accept);               // close -> rescans
        app.debug_wait_scan();
        EXPECT(app.scan_result().ready);
        EXPECT(app.scan_result().total_files == 40 + 3 + 2);
        EXPECT(fs::exists(data / "ACT1.POD"));
        EXPECT(!fs::exists(data / "ACT1.POD.part"));
        EXPECT(!fs::exists(data / "nocturne.exe"));
        shot.save(app, "08_files_ready");
        app.debug_select_tab(0);
        shot.save(app, "09_play_ready");
        app.handle(Input::Accept);
        EXPECT(app.result() == Result::Play);

        // Re-running skips everything that is already there.
        CopyProgress again;
        EXPECT(copy_tree(src, data, again) == CopyOutcome::Done);
        EXPECT(again.files_skipped == again.files_total);
    }

    // ---- Scenario 3: settings ------------------------------------------------------
    {
        const fs::path data = tmp / "data";
        App app(gfx, data, {});
        app.debug_wait_scan();
        app.set_controller_name("Xbox Wireless Controller");
        app.set_sticks(0.55f, -0.30f, 0, 0);
        app.debug_select_tab(2);
        for (int i = 0; i < 5; ++i) app.handle(Input::Right);          // deadzone 20% -> 25%
        app.debug_set_focus(2);
        for (int i = 0; i < 3; ++i) app.handle(Input::Left);           // look speed down
        app.debug_set_focus(3);
        app.handle(Input::Accept);                                      // invert on
        shot.save(app, "10_settings");

        NocturneLauncherSettings s = load_settings(data / "launcher.ini");
        EXPECT(std::fabs(s.stick_deadzone - 0.25f) < 1e-4f);
        EXPECT(std::fabs(s.look_rate - 0.10f) < 1e-4f);
        EXPECT(s.invert_look == 1);
        app.debug_set_focus(4);
        app.handle(Input::Accept);                                      // reset
        s = load_settings(data / "launcher.ini");
        NocturneLauncherSettings d;
        nocturne_launcher_settings_defaults(&d);
        EXPECT(s.stick_deadzone == d.stick_deadzone && s.invert_look == d.invert_look);

        // Graphics tab: 4K, FSR on, sharpness up; the reset on the controller page must leave these alone.
        app.debug_select_tab(3);
        app.handle(Input::Right);                                       // screen shape 4:3 -> Smart
        app.debug_set_focus(1);
        app.handle(Input::Accept);                                      // output 1080p -> 4K
        app.debug_set_focus(2);
        app.handle(Input::Accept);                                      // FSR on
        app.debug_set_focus(3);
        for (int i = 0; i < 3; ++i) app.handle(Input::Right);           // sharpness 50% -> 80%
        shot.save(app, "10b_graphics");
        s = load_settings(data / "launcher.ini");
        EXPECT(s.output_height == 2160 && s.upscaler == 1 && std::fabs(s.fsr_sharpness - 0.8f) < 1e-4f);
        EXPECT(s.screen_shape == 1);
        app.debug_select_tab(2);
        app.debug_set_focus(4);
        app.handle(Input::Accept);                                      // controller reset
        s = load_settings(data / "launcher.ini");
        EXPECT(s.output_height == 2160 && s.upscaler == 1 && s.screen_shape == 1);
        app.debug_select_tab(3);
        app.debug_set_focus(4);
        app.handle(Input::Accept);                                      // graphics reset
        s = load_settings(data / "launcher.ini");
        EXPECT(s.output_height == 1080 && s.upscaler == 0 && std::fabs(s.fsr_sharpness - 0.5f) < 1e-4f && s.screen_shape == 0);

        app.debug_select_tab(5);
        shot.save(app, "11_about");
        app.debug_select_tab(4);
        shot.save(app, "12_story_no_cover");
    }

    // ---- Scenario 4: box art present (a generated stand-in, not the real cover) ----------
    {
        const fs::path data = tmp / "data";
        SDL_Surface *c = SDL_CreateRGBSurfaceWithFormat(0, 400, 500, 24, SDL_PIXELFORMAT_RGB24);
        for (int y = 0; y < 500; ++y)
            for (int x = 0; x < 400; ++x) {
                Uint8 *px = static_cast<Uint8 *>(c->pixels) + y * c->pitch + x * 3;
                px[0] = static_cast<Uint8>(40 + y / 4);
                px[1] = static_cast<Uint8>(60 + x / 8);
                px[2] = 90;
            }
        EXPECT(SDL_SaveBMP(c, (data / "cover.bmp").string().c_str()) == 0);
        SDL_FreeSurface(c);
        App app(gfx, data, {});
        app.debug_wait_scan();
        shot.save(app, "13_play_with_cover");
        app.debug_select_tab(4);
        shot.save(app, "14_story_with_cover");
    }

    // ---- Scenario 4b: no cover supplied; a picture inside a .pod archive is found and remembered ----------
    {
        const fs::path data = tmp / "data_search";
        fs::create_directories(data / "System");
        auto bmp_bytes = [&](int w, int h) {
            SDL_Surface *c = SDL_CreateRGBSurfaceWithFormat(0, w, h, 24, SDL_PIXELFORMAT_RGB24);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    Uint8 *px = static_cast<Uint8 *>(c->pixels) + y * c->pitch + x * 3;
                    px[0] = static_cast<Uint8>(x); px[1] = static_cast<Uint8>(y); px[2] = 120;
                }
            const fs::path f = tmp / "t.bmp";
            EXPECT(SDL_SaveBMP(c, f.string().c_str()) == 0);
            SDL_FreeSurface(c);
            std::ifstream in(f, std::ios::binary);
            return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        };
        struct E { std::string name; std::vector<char> data; };
        std::vector<E> es = {{"Textures\\wall.bmp", bmp_bytes(400, 300)},
                             {"Interface\\TitleScreen.bmp", bmp_bytes(640, 480)},
                             {"Interface\\icon.bmp", bmp_bytes(32, 32)},
                             {"Scripts\\a.txt", std::vector<char>(100, 'x')},
                             {"ART\\STATS.RAW", std::vector<char>(640 * 480)},
                             {"ART\\STATS.ACT", std::vector<char>(768)}};
        for (int i = 0; i < 640 * 480; ++i) es[4].data[i] = static_cast<char>((i % 640) / 3 + (i / 640) / 4);
        for (int i = 0; i < 256; ++i) { es[5].data[i * 3] = static_cast<char>(i); es[5].data[i * 3 + 1] = static_cast<char>(255 - i); es[5].data[i * 3 + 2] = 90; }
        std::string names;
        std::vector<std::uint32_t> name_off;
        for (auto &e : es) { name_off.push_back(static_cast<std::uint32_t>(names.size())); names += e.name; names.push_back('\0'); }
        const std::uint32_t table = 0x60 + static_cast<std::uint32_t>(es.size()) * 20;
        std::uint32_t pos = table + static_cast<std::uint32_t>(names.size());
        std::string pod(0x60, '\0');
        std::memcpy(&pod[0], "POD2", 4);
        const std::uint32_t count = static_cast<std::uint32_t>(es.size());
        std::memcpy(&pod[0x58], &count, 4);
        for (size_t i = 0; i < es.size(); ++i) {
            const std::uint32_t row[5] = {name_off[i], static_cast<std::uint32_t>(es[i].data.size()), pos, 0, 0};
            pod.append(reinterpret_cast<const char *>(row), 20);
            pos += static_cast<std::uint32_t>(es[i].data.size());
        }
        pod += names;
        for (auto &e : es) pod.append(e.data.data(), e.data.size());
        { std::ofstream out(data / "System" / "x.pod", std::ios::binary); out.write(pod.data(), static_cast<std::streamsize>(pod.size())); }
        { std::ofstream out(data / "ACT1.POD", std::ios::binary); out.write(pod.data(), static_cast<std::streamsize>(pod.size())); }
        App app(gfx, data, {});
        app.debug_wait_scan();
        shot.save(app, "13b_play_found_picture");
        std::ifstream in(data / "cover_choice.txt");
        std::string pod_name, chosen;
        std::getline(in, pod_name);
        std::getline(in, chosen);
        EXPECT(chosen.find("STATS.RAW") != std::string::npos);   // the known picture beats a name match
        EXPECT(fs::exists(data / "pod_pictures.txt"));
    }

    fs::remove_all(tmp);
    std::printf("%s (%d failure(s))\n", g_fail ? "FAILED" : "ok", g_fail);
    gfx.shutdown();
    return g_fail ? 1 : 0;
}
