// Logic tests for the launcher (no window needed):  tests <scratch_dir>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>

#include "fs_util.h"
#include "game_files.h"
#include "importer.h"
#include "ini_file.h"
#include "settings.h"

using namespace nl;

static int failures = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);     \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static void write(const std::string &path, const std::string &data) {
    make_dirs(fs::path(path).parent_path().string());
    std::ofstream o(path, std::ios::binary);
    o << data;
}
static std::string slurp(const std::string &path) {
    std::ifstream i(path, std::ios::binary);
    std::stringstream ss;
    ss << i.rdbuf();
    return ss.str();
}

int main(int argc, char **argv) {
    std::string base = argc > 1 ? argv[1] : "launcher_test_tmp";
    std::error_code ec;
    fs::remove_all(base, ec);
    make_dirs(base);

    // --- IniFile: edits in place, keeps everything else byte for byte, keeps CRLF
    {
        std::string p = base + "/a.ini";
        write(p, "; comment\r\n[Graphics]\r\ngamePIXX=640\r\ngamePIXY=480\r\n\r\n[Sound]\r\nMovieVolume=50\r\n");
        IniFile ini;
        CHECK(ini.load(p));
        CHECK(ini.get_int("graphics", "gamepixx", 0) == 640);   // case-insensitive
        ini.set_int("Graphics", "gamePIXX", 800);
        ini.set_int("Graphics", "shadowFlag", 1);   // appended inside [Graphics]
        ini.set_int("Control", "alwaysRun", 1);     // new section
        CHECK(ini.save(p));
        std::string out = slurp(p);
        CHECK(out == "; comment\r\n[Graphics]\r\ngamePIXX=800\r\ngamePIXY=480\r\nshadowFlag=1\r\n\r\n[Sound]\r\nMovieVolume=50\r\n\r\n[Control]\r\nalwaysRun=1\r\n");
        IniFile ini2;
        CHECK(ini2.load(p) && ini2.get_int("Control", "alwaysRun", 0) == 1 && ini2.get_int("Sound", "MovieVolume", 0) == 50);
        CHECK(!IniFile().load(base + "/missing.ini"));
    }

    // --- case-insensitive lookup
    {
        write(base + "/ci/System/Nocturne.INI", "x");
        CHECK(!find_ci(base + "/ci", "system\\nocturne.ini").empty());
        CHECK(find_ci(base + "/ci", "system\\nope.ini").empty());
        CHECK(resolve_for_write(base + "/ci", "SYSTEM\\new.ini") == base + "/ci/System/new.ini");
    }

    // --- game files scan + manifest
    {
        std::string r = base + "/gf";
        make_dirs(r);
        CHECK(!scan_game_files(r).ready && !scan_game_files(r).any);
        write(r + "/act1.pod", std::string(100, 'a'));
        write(r + "/Enemy.POD", std::string(50, 'b'));
        write(r + "/Bonus.pod", "c");
        Inventory inv = scan_game_files(r);
        CHECK(inv.ready && !inv.complete && inv.required_present == 2);
        CHECK(inv.other_pods.size() == 1 && inv.other_pods[0] == "Bonus.pod");
        write(r + "/launcher_manifest.txt", "# my list\nrequired extra\\file.bin   # my file\n");
        inv = scan_game_files(r);
        CHECK(!inv.ready && inv.required_total == 3);
        write(r + "/extra/file.bin", "1");
        CHECK(scan_game_files(r).ready);
    }

    // --- settings: edit existing ini, defaults when keys are missing, launcher-only without game ini
    {
        std::string r = base + "/st";
        write(r + "/system/nocturne.ini", "[Graphics]\ngamePIXX=640\ngamePIXY=480\n[Sound]\nMovieVolume=100\n");
        Settings s(r);
        CHECK(s.game_ini_present());
        CHECK(s.value_label(0) == "640 x 480");
        CHECK(s.adjust(0, 1) && s.value_label(0) == "800 x 600");
        CHECK(s.adjust(0, -1) && s.adjust(0, -1) && s.value_label(0) == "512 x 384");
        CHECK(s.value_label(1) == "Game default");           // shadows: key absent
        CHECK(s.adjust(1, 1) && s.value_label(1) == "Off");  // first press picks the first choice
        CHECK(s.adjust(1, 1) && s.value_label(1) == "On");
        std::string ini = slurp(r + "/system/nocturne.ini");
        CHECK(ini.find("gamePIXX=512") != std::string::npos && ini.find("gamePIXY=384") != std::string::npos);
        CHECK(ini.find("shadowFlag=1") != std::string::npos && ini.find("MovieVolume=100") != std::string::npos);
        CHECK(!s.autostart());
        size_t auto_idx = s.defs().size() - 1;
        CHECK(s.adjust(auto_idx, 1) && s.autostart());
        Settings again(r);
        CHECK(again.autostart() && again.value_label(0) == "512 x 384");

        std::string r2 = base + "/st2";   // no game ini yet
        make_dirs(r2);
        Settings s2(r2);
        CHECK(!s2.game_ini_present() && !s2.editable(0) && !s2.adjust(0, 1));
        CHECK(s2.editable(s2.defs().size() - 1));
        CHECK(!fs::exists(r2 + "/system"));   // nothing was created behind the user's back
    }

    // --- import layout rules
    {
        CHECK(import_skip("setup.EXE") && import_skip("x.dll") && !import_skip("ACT1.POD") && !import_skip("opening.avi"));
        CHECK(import_dest_for_loose("opening.avi") == "video/opening.avi");
        CHECK(import_dest_for_loose("nocturne.ini") == "system/nocturne.ini");
        CHECK(import_dest_for_loose("ACT1.POD") == "ACT1.POD");

        std::string src = base + "/disc/Games/Nocturne";   // game folder two levels down
        write(src + "/ACT1.POD", std::string(1000, 'x'));
        write(src + "/ENEMY.POD", std::string(500, 'y'));
        write(src + "/VIDEO/opening.avi", std::string(300, 'z'));
        write(src + "/setup.exe", "bin");
        ImportPlan plan = plan_folder_import(base + "/disc");
        CHECK(plan.error.empty() && plan.items.size() == 3 && plan.total_bytes == 1800);
        CHECK(!plan_folder_import(base + "/gf/extra").error.empty());      // no pods there
        CHECK(!plan_folder_import(base + "/does-not-exist").error.empty());

        std::string dst = base + "/dst";
        auto svc = make_folder_import_service(dst, base + "/disc");
        CHECK(svc->methods().size() == 1 && svc->methods()[0].available);
        svc->start("folder");
        for (int i = 0; i < 500 && svc->progress().state == ImportState::Working; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ImportProgress p = svc->progress();
        CHECK(p.state == ImportState::Done && p.files_done == 3 && p.bytes_done == 1800);
        CHECK(file_size_of(dst + "/ACT1.POD") == 1000 && file_size_of(dst + "/VIDEO/opening.avi") == 300);
        CHECK(!fs::exists(dst + "/setup.exe") && !fs::exists(dst + "/ACT1.POD.part"));
        svc->reset();
        svc->start("folder");   // second run: nothing to copy again
        for (int i = 0; i < 500 && svc->progress().state == ImportState::Working; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        CHECK(svc->progress().state == ImportState::Done && svc->progress().message.find("3 already present") != std::string::npos);
        CHECK(scan_game_files(dst).ready);
    }

    std::printf(failures ? "%d FAILED\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
