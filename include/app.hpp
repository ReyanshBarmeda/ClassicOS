#ifndef CLASSIC_OS_APP_HPP
#define CLASSIC_OS_APP_HPP

#include "config.hpp"
#include "renderer.hpp"
#include "texture_cache.hpp"
#include "input.hpp"
#include "library.hpp"
#include "launcher.hpp"
#include "ui_dashboard.hpp"
#include <cstdint>

namespace classic_os {

class Application {
public:
    // Verified 16,960 us (~59.0 FPS) active frame budget from game_manager @ 0x6de8
    static constexpr uint64_t kActiveFrameMicros = 16960;
    static constexpr uint64_t kIdleFrameMicros   = 100000; // 10 FPS low-power poll after 3s idle
    static constexpr uint64_t kIdleThresholdMs   = 3000;

    Application(int argc, char* argv[]);
    ~Application();

    int Run();

private:
    static uint64_t MonotonicMicros();
    static void SleepMicros(uint64_t micros);

    RuntimeConfig cfg_;
    Renderer renderer_;
    InputManager input_;
    GameLibrary library_;
};

} // namespace classic_os

#endif // CLASSIC_OS_APP_HPP
