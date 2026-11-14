#ifndef CLASSIC_OS_INPUT_HPP
#define CLASSIC_OS_INPUT_HPP

#include "sdl_sqlite_abi.hpp"
#include "config.hpp"
#include <string>
#include <vector>

namespace classic_os {

enum class NavAction {
    None = 0,
    Up,
    Down,
    Left,
    Right,
    Confirm,      // PSC Cross (X) — raw button 2 / SDL_CONTROLLER_BUTTON_A
    Cancel,       // PSC Circle (O) — raw button 1 / SDL_CONTROLLER_BUTTON_B
    Options,      // PSC Triangle  — raw button 0 / SDL_CONTROLLER_BUTTON_Y
    ContextView,  // PSC Square    — raw button 3 / SDL_CONTROLLER_BUTTON_X
    PageLeft,     // PSC L1        — raw button 6 / SDL_CONTROLLER_BUTTON_LEFTSHOULDER
    PageRight,    // PSC R1        // raw button 7 / SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
    TabPrev,      // PSC L2        // raw button 4 / SDL_CONTROLLER_AXIS_TRIGGERLEFT
    TabNext,      // PSC R2        // raw button 5 / SDL_CONTROLLER_AXIS_TRIGGERRIGHT
    SelectBtn,    // PSC Select    // raw button 8 / SDL_CONTROLLER_BUTTON_BACK
    StartBtn,     // PSC Start     // raw button 9 / SDL_CONTROLLER_BUTTON_START
    ConsoleOpen,  // PSC OPEN btn  // GPIO keycode 161 (KEY_EJECTCD)
    ConsoleReset, // PSC RESET btn // GPIO keycode 164 (KEY_PLAYPAUSE)
    QuitApp
};

class InputManager {
public:
    // Verified PSC Controller constants from es_input.cfg & sdl_remap_arm.so (.rodata @ 0x2f18)
    static constexpr int kPscBtnTriangle = 0;
    static constexpr int kPscBtnCircle   = 1;
    static constexpr int kPscBtnCross    = 2;
    static constexpr int kPscBtnSquare   = 3;
    static constexpr int kPscBtnL2       = 4;
    static constexpr int kPscBtnR2       = 5;
    static constexpr int kPscBtnL1       = 6;
    static constexpr int kPscBtnR1       = 7;
    static constexpr int kPscBtnSelect   = 8;
    static constexpr int kPscBtnStart    = 9;
    static constexpr Sint16 kAxisDeadzone= 16000;

    InputManager() = default;
    ~InputManager();

    bool Init(const RuntimeConfig& cfg);
    void Shutdown();

    // Polls all pending SDL events and returns discrete navigation actions
    std::vector<NavAction> PollActions(uint64_t now_ms);

    int connected_pad_count() const { return static_cast<int>(controllers_.size() + raw_joysticks_.size()); }
    const std::string& primary_pad_name() const { return primary_pad_name_; }
    bool mappings_loaded() const { return mappings_loaded_; }

private:
    void ScanDevices();
    NavAction MapGameControllerButton(Uint8 button) const;
    NavAction MapRawPscJoystickButton(Uint8 button) const;
    NavAction MapKeyboardKey(SDL_Keycode sym) const;

    std::vector<SDL_GameController*> controllers_;
    std::vector<SDL_Joystick*> raw_joysticks_;
    std::string primary_pad_name_ = "Sony Interactive Entertainment Controller";
    bool mappings_loaded_ = false;

    // Axis digital edge & auto-repeat state
    int held_dir_x_ = 0;
    int held_dir_y_ = 0;
    bool l2_digital_latched_ = false;
    bool r2_digital_latched_ = false;
    uint64_t next_repeat_x_ms_ = 0;
    uint64_t next_repeat_y_ms_ = 0;
};

} // namespace classic_os

#endif // CLASSIC_OS_INPUT_HPP
