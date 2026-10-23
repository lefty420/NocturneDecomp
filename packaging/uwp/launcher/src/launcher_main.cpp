#include "launcher_main.h"

#include "app.h"

#include <cmath>
#include <cstdio>

namespace nl {

namespace {

// Held directions repeat: a pause, then a steady rate (the game's own menu uses the same feel).
struct Repeater {
    bool down = false;
    Uint32 next = 0;
    // True on the press and then each time the repeat comes due.
    bool poll(bool now_down, Uint32 t) {
        if (!now_down) { down = false; return false; }
        if (!down) { down = true; next = t + 400; return true; }
        if (t >= next) { next = t + 110; return true; }
        return false;
    }
};

}  // namespace

LaunchResult run_launcher(SDL_Window *window, SDL_Renderer *renderer, const std::filesystem::path &fonts_dir,
                          const std::filesystem::path &data_root) {
    Gfx gfx;
    if (!gfx.init(renderer, fonts_dir.string())) {
        std::fprintf(stderr, "launcher: could not load fonts from %s\n", fonts_dir.string().c_str());
        return LaunchResult::Error;
    }
    std::vector<ImportProvider> providers = make_portable_providers();
    add_platform_providers(providers);
    App app(gfx, data_root, std::move(providers));

    SDL_GameController *pad = nullptr;
    auto open_first_pad = [&] {
        if (pad) return;
        for (int i = 0; i < SDL_NumJoysticks(); ++i) {
            if (SDL_IsGameController(i)) {
                pad = SDL_GameControllerOpen(i);
                if (pad) {
                    const char *n = SDL_GameControllerName(pad);
                    app.set_controller_name(n ? n : "Controller");
                    return;
                }
            }
        }
    };
    open_first_pad();

    int ow = 0, oh = 0;
    Repeater rep[4];   // up, down, left, right
    Uint32 last = SDL_GetTicks();
    LaunchResult result = LaunchResult::Quit;
    bool running = true;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_CONTROLLERDEVICEADDED: open_first_pad(); break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                        SDL_GameControllerClose(pad);
                        pad = nullptr;
                        app.set_controller_name("");
                        open_first_pad();
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    switch (e.cbutton.button) {
                        case SDL_CONTROLLER_BUTTON_A: app.handle(Input::Accept); break;
                        case SDL_CONTROLLER_BUTTON_B: app.handle(Input::Back); break;
                        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: app.handle(Input::PrevTab); break;
                        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: app.handle(Input::NextTab); break;
                        default: break;   // directions are handled below so they can repeat
                    }
                    break;
                case SDL_KEYDOWN:
                    if (e.key.repeat) break;
                    switch (e.key.keysym.sym) {
                        case SDLK_RETURN: case SDLK_SPACE: app.handle(Input::Accept); break;
                        case SDLK_ESCAPE: case SDLK_BACKSPACE: app.handle(Input::Back); break;
                        case SDLK_q: app.handle(Input::PrevTab); break;
                        case SDLK_e: app.handle(Input::NextTab); break;
                        default: break;
                    }
                    break;
                default: break;
            }
        }

        // Directions: d-pad, left stick or arrow keys, with repeat.
        const Uint32 now = SDL_GetTicks();
        float lx = 0, ly = 0, rx = 0, ry = 0;
        bool dpad[4] = {false, false, false, false};
        if (pad) {
            auto ax = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(pad, a) / 32767.0f; };
            lx = ax(SDL_CONTROLLER_AXIS_LEFTX);
            ly = ax(SDL_CONTROLLER_AXIS_LEFTY);
            rx = ax(SDL_CONTROLLER_AXIS_RIGHTX);
            ry = ax(SDL_CONTROLLER_AXIS_RIGHTY);
            dpad[0] = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -0.6f;
            dpad[1] = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > 0.6f;
            dpad[2] = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -0.6f;
            dpad[3] = SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > 0.6f;
        }
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        dpad[0] = dpad[0] || keys[SDL_SCANCODE_UP];
        dpad[1] = dpad[1] || keys[SDL_SCANCODE_DOWN];
        dpad[2] = dpad[2] || keys[SDL_SCANCODE_LEFT];
        dpad[3] = dpad[3] || keys[SDL_SCANCODE_RIGHT];
        static const Input dirs[4] = {Input::Up, Input::Down, Input::Left, Input::Right};
        for (int i = 0; i < 4; ++i)
            if (rep[i].poll(dpad[i], now)) app.handle(dirs[i]);
        app.set_sticks(lx, ly, rx, ry);

        const float dt = std::min(0.1f, (now - last) / 1000.0f);
        last = now;
        app.update(dt);

        int w = 0, h = 0;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        if (w != ow || h != oh) { ow = w; oh = h; gfx.set_output_size(w, h); }
        app.draw();
        SDL_RenderPresent(renderer);

        if (app.result() == Result::Play) { result = LaunchResult::Play; running = false; }
    }

    if (pad) SDL_GameControllerClose(pad);
    (void)window;
    return result;
}

}  // namespace nl
