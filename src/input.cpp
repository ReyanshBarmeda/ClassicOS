#include "input.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>

namespace classic_os {

InputManager::~InputManager() {
    Shutdown();
}

bool InputManager::Init(const RuntimeConfig& cfg) {
    log_path_ = cfg.runtime_log_path + "/classic_os_diagnostics.log";

    // Load a gamecontrollerdb.txt if one exists. The BleemSync path is the one verified in
    // game_manager / sdl_remap_arm.so; the others are checked so a plain Project Eris install works too.
    const std::string candidates[] = {
        cfg.controller_db,
        cfg.project_eris_path + "/etc/boot_menu/gamecontrollerdb.txt",
        cfg.project_eris_path + "/opt/classic_os/gamecontrollerdb.txt",
        "gamecontrollerdb.txt"
    };
    mappings_loaded_ = false;
    for (const auto& path : candidates) {
        struct stat st{};
        if (stat(path.c_str(), &st) != 0) continue;
        SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
        if (rw && SDL_GameControllerAddMappingsFromRW(rw, 1) >= 0) {
            mappings_loaded_ = true;
            Log("gamecontrollerdb loaded from " + path);
            break;
        }
    }
    if (!mappings_loaded_) {
        Log("no gamecontrollerdb.txt found; pads without a built-in SDL mapping use the raw PSC joystick map");
    }

    ScanDevices();
    return true;
}

void InputManager::Log(const std::string& msg) const {
    if (log_path_.empty()) return;
    std::ofstream out(log_path_, std::ios::out | std::ios::app);
    if (out.is_open()) out << "[input] " << msg << "\n";
}

void InputManager::Shutdown() {
    for (auto* gc : controllers_) {
        if (gc) SDL_GameControllerClose(gc);
    }
    controllers_.clear();

    for (auto* js : raw_joysticks_) {
        if (js) SDL_JoystickClose(js);
    }
    raw_joysticks_.clear();
}

void InputManager::ScanDevices() {
    Shutdown();
    const int num = SDL_NumJoysticks();
    Log("SDL_NumJoysticks=" + std::to_string(num));
    for (int i = 0; i < num; ++i) {
        const char* name = SDL_JoystickNameForIndex(i);
        if (name && i == 0) {
            primary_pad_name_ = name;
        }
        if (SDL_IsGameController(i)) {
            SDL_GameController* gc = SDL_GameControllerOpen(i);
            if (gc) {
                controllers_.push_back(gc);
                Log(std::string("pad ") + std::to_string(i) + " '" + (name ? name : "?") + "': SDL GameController mapping");
                continue;
            }
        }
        SDL_Joystick* js = SDL_JoystickOpen(i);
        if (js) {
            raw_joysticks_.push_back(js);
            Log(std::string("pad ") + std::to_string(i) + " '" + (name ? name : "?") + "': raw joystick (buttons=" +
                std::to_string(SDL_JoystickNumButtons(js)) + " axes=" + std::to_string(SDL_JoystickNumAxes(js)) +
                " hats=" + std::to_string(SDL_JoystickNumHats(js)) + ")");
        } else {
            Log(std::string("pad ") + std::to_string(i) + " could not be opened");
        }
    }
}

NavAction InputManager::MapGameControllerButton(Uint8 button) const {
    switch (static_cast<SDL_GameControllerButton>(button)) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:       return NavAction::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return NavAction::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return NavAction::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return NavAction::Right;
        case SDL_CONTROLLER_BUTTON_A:             return NavAction::Confirm;     // PSC Cross (X)
        case SDL_CONTROLLER_BUTTON_B:             return NavAction::Cancel;      // PSC Circle (O)
        case SDL_CONTROLLER_BUTTON_Y:             return NavAction::Options;     // PSC Triangle
        case SDL_CONTROLLER_BUTTON_X:             return NavAction::ContextView; // PSC Square
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return NavAction::PageLeft;    // PSC L1
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return NavAction::PageRight;   // PSC R1
        case SDL_CONTROLLER_BUTTON_BACK:          return NavAction::SelectBtn;   // PSC Select
        case SDL_CONTROLLER_BUTTON_START:         return NavAction::StartBtn;    // PSC Start
        default:                                  return NavAction::None;
    }
}

NavAction InputManager::MapRawPscJoystickButton(Uint8 button) const {
    // Verified raw button indices from es_input.cfg and sdl_remap_arm.so (.rodata @ 0x2f18)
    switch (button) {
        case kPscBtnTriangle: return NavAction::Options;
        case kPscBtnCircle:   return NavAction::Cancel;
        case kPscBtnCross:    return NavAction::Confirm;
        case kPscBtnSquare:   return NavAction::ContextView;
        case kPscBtnL2:       return NavAction::TabPrev;
        case kPscBtnR2:       return NavAction::TabNext;
        case kPscBtnL1:       return NavAction::PageLeft;
        case kPscBtnR1:       return NavAction::PageRight;
        case kPscBtnSelect:   return NavAction::SelectBtn;
        case kPscBtnStart:    return NavAction::StartBtn;
        default:              return NavAction::None;
    }
}

NavAction InputManager::MapKeyboardKey(SDL_Keycode sym) const {
    // Supports both console GPIO keys (playpause=164, cdselect=161) and desktop keyboard testing
    switch (sym) {
        case SDLK_UP:        return NavAction::Up;
        case SDLK_DOWN:      return NavAction::Down;
        case SDLK_LEFT:      return NavAction::Left;
        case SDLK_RIGHT:     return NavAction::Right;
        case SDLK_RETURN:
        case SDLK_x:         return NavAction::Confirm;
        case SDLK_BACKSPACE:
        case SDLK_z:         return NavAction::Cancel;
        case SDLK_s:         return NavAction::Options;
        case SDLK_a:         return NavAction::ContextView;
        case SDLK_q:         return NavAction::PageLeft;
        case SDLK_w:         return NavAction::PageRight;
        case SDLK_TAB:       return NavAction::SelectBtn;
        case SDLK_SPACE:     return NavAction::StartBtn;
        case SDLK_ESCAPE:    return NavAction::Cancel;
        case 161:            return NavAction::ConsoleOpen;  // DTB gpio-keys button@1 (cdselect)
        case 164:            return NavAction::ConsoleReset; // DTB gpio-keys button@0 (playpause)
        default:             return NavAction::None;
    }
}

std::vector<NavAction> InputManager::PollActions(uint64_t now_ms) {
    std::vector<NavAction> actions;
    SDL_Event ev{};

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                actions.push_back(NavAction::QuitApp);
                break;

            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
            case SDL_JOYDEVICEADDED:
            case SDL_JOYDEVICEREMOVED:
                ScanDevices();
                break;

            case SDL_CONTROLLERBUTTONDOWN: {
                const NavAction act = MapGameControllerButton(ev.cbutton.button);
                if (act != NavAction::None) {
                    actions.push_back(act);
                }
                break;
            }

            case SDL_CONTROLLERAXISMOTION: {
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                    const int dir = (ev.caxis.value < -kAxisDeadzone) ? -1 : ((ev.caxis.value > kAxisDeadzone) ? 1 : 0);
                    if (dir != held_dir_x_) {
                        held_dir_x_ = dir;
                        if (dir < 0) actions.push_back(NavAction::Left);
                        if (dir > 0) actions.push_back(NavAction::Right);
                        next_repeat_x_ms_ = now_ms + 260;
                    }
                } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    const int dir = (ev.caxis.value < -kAxisDeadzone) ? -1 : ((ev.caxis.value > kAxisDeadzone) ? 1 : 0);
                    if (dir != held_dir_y_) {
                        held_dir_y_ = dir;
                        if (dir < 0) actions.push_back(NavAction::Up);
                        if (dir > 0) actions.push_back(NavAction::Down);
                        next_repeat_y_ms_ = now_ms + 260;
                    }
                } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
                    const bool pressed = (ev.caxis.value > kAxisDeadzone);
                    if (pressed && !l2_digital_latched_) {
                        actions.push_back(NavAction::TabPrev);
                    }
                    l2_digital_latched_ = pressed;
                } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                    const bool pressed = (ev.caxis.value > kAxisDeadzone);
                    if (pressed && !r2_digital_latched_) {
                        actions.push_back(NavAction::TabNext);
                    }
                    r2_digital_latched_ = pressed;
                }
                break;
            }

            case SDL_JOYBUTTONDOWN: {
                // Only process raw joystick events if not already handled by SDL_GameController
                if (SDL_GameControllerFromInstanceID(ev.jbutton.which) == nullptr) {
                    const NavAction act = MapRawPscJoystickButton(ev.jbutton.button);
                    if (act != NavAction::None) {
                        actions.push_back(act);
                    }
                }
                break;
            }

            case SDL_JOYAXISMOTION: {
                if (SDL_GameControllerFromInstanceID(ev.jaxis.which) == nullptr) {
                    if (ev.jaxis.axis == 0) {
                        const int dir = (ev.jaxis.value < -kAxisDeadzone) ? -1 : ((ev.jaxis.value > kAxisDeadzone) ? 1 : 0);
                        if (dir != held_dir_x_) {
                            held_dir_x_ = dir;
                            if (dir < 0) actions.push_back(NavAction::Left);
                            if (dir > 0) actions.push_back(NavAction::Right);
                            next_repeat_x_ms_ = now_ms + 260;
                        }
                    } else if (ev.jaxis.axis == 1) {
                        const int dir = (ev.jaxis.value < -kAxisDeadzone) ? -1 : ((ev.jaxis.value > kAxisDeadzone) ? 1 : 0);
                        if (dir != held_dir_y_) {
                            held_dir_y_ = dir;
                            if (dir < 0) actions.push_back(NavAction::Up);
                            if (dir > 0) actions.push_back(NavAction::Down);
                            next_repeat_y_ms_ = now_ms + 260;
                        }
                    }
                }
                break;
            }

            case SDL_JOYHATMOTION: {
                // The PSC d-pad may be reported as a hat rather than axes 0/1 on raw (unmapped) pads
                if (SDL_GameControllerFromInstanceID(ev.jhat.which) == nullptr) {
                    const int dx = (ev.jhat.value & SDL_HAT_LEFT) ? -1 : ((ev.jhat.value & SDL_HAT_RIGHT) ? 1 : 0);
                    const int dy = (ev.jhat.value & SDL_HAT_UP)   ? -1 : ((ev.jhat.value & SDL_HAT_DOWN)  ? 1 : 0);
                    if (dx != held_dir_x_) {
                        held_dir_x_ = dx;
                        if (dx < 0) actions.push_back(NavAction::Left);
                        if (dx > 0) actions.push_back(NavAction::Right);
                        next_repeat_x_ms_ = now_ms + 260;
                    }
                    if (dy != held_dir_y_) {
                        held_dir_y_ = dy;
                        if (dy < 0) actions.push_back(NavAction::Up);
                        if (dy > 0) actions.push_back(NavAction::Down);
                        next_repeat_y_ms_ = now_ms + 260;
                    }
                }
                break;
            }

            case SDL_KEYDOWN: {
                if (ev.key.repeat == 0) {
                    const NavAction act = MapKeyboardKey(ev.key.keysym.sym);
                    if (act != NavAction::None) {
                        actions.push_back(act);
                    }
                }
                break;
            }

            default:
                break;
        }
    }

    // Handle smooth D-pad / axis auto-repeat when held
    if (held_dir_x_ != 0 && now_ms >= next_repeat_x_ms_) {
        actions.push_back(held_dir_x_ < 0 ? NavAction::Left : NavAction::Right);
        next_repeat_x_ms_ = now_ms + 85;
    }
    if (held_dir_y_ != 0 && now_ms >= next_repeat_y_ms_) {
        actions.push_back(held_dir_y_ < 0 ? NavAction::Up : NavAction::Down);
        next_repeat_y_ms_ = now_ms + 85;
    }

    return actions;
}

} // namespace classic_os
