#include "host.h"

#include <cmath>
#include <map>

namespace nl {

namespace {

// Same feel as the game's own menus (shim_config_input.h).
constexpr unsigned kRepeatDelayMs = 400, kRepeatRateMs = 110;
constexpr float kStickDead = 0.5f;

struct Repeater {
    Action held = Action::None;
    unsigned since = 0, next = 0;

    // Returns the action to fire this frame (None if nothing is due).
    Action update(Action now, unsigned dt) {
        if (now == Action::None) {
            held = Action::None;
            return Action::None;
        }
        if (now != held) {
            held = now;
            since = 0;
            next = kRepeatDelayMs;
            return now;
        }
        since += dt;
        if (since >= next) {
            next += kRepeatRateMs;
            return now;
        }
        return Action::None;
    }
};

Action key_action(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: return Action::Up;
    case SDLK_DOWN: return Action::Down;
    case SDLK_LEFT: return Action::Left;
    case SDLK_RIGHT: return Action::Right;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE: return Action::Accept;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE: return Action::Back;
    case SDLK_F5:
    case SDLK_y: return Action::Refresh;
    default: return Action::None;
    }
}

PadState read_pad(SDL_GameController *c) {
    PadState p;
    if (!c) return p;
    p.connected = true;
    const char *n = SDL_GameControllerName(c);
    p.name = n ? n : "";
    auto b = [&](SDL_GameControllerButton btn) { return SDL_GameControllerGetButton(c, btn) != 0; };
    auto ax = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(c, a) / 32767.0f; };
    p.a = b(SDL_CONTROLLER_BUTTON_A);
    p.b = b(SDL_CONTROLLER_BUTTON_B);
    p.x = b(SDL_CONTROLLER_BUTTON_X);
    p.y = b(SDL_CONTROLLER_BUTTON_Y);
    p.lb = b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    p.rb = b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    p.view = b(SDL_CONTROLLER_BUTTON_BACK);
    p.menu = b(SDL_CONTROLLER_BUTTON_START);
    p.ls = b(SDL_CONTROLLER_BUTTON_LEFTSTICK);
    p.rs = b(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    p.up = b(SDL_CONTROLLER_BUTTON_DPAD_UP);
    p.down = b(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    p.left = b(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    p.right = b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    p.lx = ax(SDL_CONTROLLER_AXIS_LEFTX);
    p.ly = ax(SDL_CONTROLLER_AXIS_LEFTY);
    p.rx = ax(SDL_CONTROLLER_AXIS_RIGHTX);
    p.ry = ax(SDL_CONTROLLER_AXIS_RIGHTY);
    p.lt = std::fmax(0.0f, ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    p.rt = std::fmax(0.0f, ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
    return p;
}

Action direction_of(const PadState &p) {
    if (p.up || p.ly < -kStickDead) return Action::Up;
    if (p.down || p.ly > kStickDead) return Action::Down;
    if (p.left || p.lx < -kStickDead) return Action::Left;
    if (p.right || p.lx > kStickDead) return Action::Right;
    return Action::None;
}

}  // namespace

Result run_launcher(SDL_Window *window, SDL_Renderer *renderer, ImportService &import, const LauncherConfig &cfg,
                    const std::string &assets_dir) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_RenderSetLogicalSize(renderer, 1920, 1080);

    Gfx gfx;
    if (!gfx.init(renderer, assets_dir + "fonts/")) return Result::Quit;
    Launcher ui(gfx, import, cfg);

    std::map<SDL_JoystickID, SDL_GameController *> pads;
    auto open_pad = [&](int index) {
        if (!SDL_IsGameController(index)) return;
        SDL_GameController *c = SDL_GameControllerOpen(index);
        if (c) pads[SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c))] = c;
    };
    for (int i = 0; i < SDL_NumJoysticks(); ++i) open_pad(i);

    Repeater repeat;
    Uint32 last = SDL_GetTicks();
    Result result = Result::Running;
    while (result == Result::Running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: result = Result::Quit; break;
            case SDL_KEYDOWN:
                if (!e.key.repeat || key_action(e.key.keysym.sym) == Action::Up || key_action(e.key.keysym.sym) == Action::Down ||
                    key_action(e.key.keysym.sym) == Action::Left || key_action(e.key.keysym.sym) == Action::Right)
                    ui.handle(key_action(e.key.keysym.sym));
                break;
            case SDL_CONTROLLERDEVICEADDED: open_pad(e.cdevice.which); break;
            case SDL_CONTROLLERDEVICEREMOVED: {
                auto it = pads.find(e.cdevice.which);
                if (it != pads.end()) {
                    SDL_GameControllerClose(it->second);
                    pads.erase(it);
                }
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN:
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A: ui.handle(Action::Accept); break;
                case SDL_CONTROLLER_BUTTON_B: ui.handle(Action::Back); break;
                case SDL_CONTROLLER_BUTTON_Y: ui.handle(Action::Refresh); break;
                default: break;
                }
                break;
            default: break;
            }
        }

        Uint32 now = SDL_GetTicks();
        unsigned dt = (unsigned)(now - last);
        last = now;

        PadState pad = read_pad(pads.empty() ? nullptr : pads.begin()->second);
        ui.set_pad(pad);
        // D-pad presses are sent once as events above only for A/B/Y; directions come from the repeater.
        ui.handle(repeat.update(direction_of(pad), dt));
        ui.tick(dt);
        if (ui.result() != Result::Running) result = ui.result();

        int ow = 0, oh = 0;
        SDL_GetRendererOutputSize(renderer, &ow, &oh);
        gfx.set_scale(oh > 0 ? oh / 1080.0f : 1.0f);

        SDL_SetRenderDrawColor(renderer, 5, 6, 12, 255);
        SDL_RenderClear(renderer);
        ui.render();
        SDL_RenderPresent(renderer);
        if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_SHOWN)) SDL_Delay(16);
    }
    for (auto &kv : pads) SDL_GameControllerClose(kv.second);
    return result;
}

}  // namespace nl
