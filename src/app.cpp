#include "app.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace classic_os {

Application::Application(int argc, char* argv[])
    : cfg_(ConfigLoader::Load(argc, argv)) {}

Application::~Application() {
    input_.Shutdown();
    renderer_.Shutdown();
}

uint64_t Application::MonotonicMicros() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

void Application::SleepMicros(uint64_t micros) {
    if (micros == 0) return;
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
}

int Application::Run() {
    Launcher launcher(cfg_);

    // Pre-seed default return command in /tmp/launchfilecommand (matches opt/emulationstation/launch.sh)
    launcher.WriteRawLaunchCommand(cfg_.return_target);
    launcher.SetPowerButtonLock(2);

    if (!renderer_.Init(cfg_)) {
        std::cerr << "[CLASSIC OS](APP)(ERROR) Failed to initialize SDL2 renderer.\n";
        launcher.SetPowerButtonLock(1);
        return 1;
    }

    TextureCache cache(renderer_.raw(), cfg_.max_vram_bytes);
    cache.InitFonts(cfg_);
    input_.Init(cfg_);
    library_.ScanAll(cfg_);

    UiDashboard dashboard(cfg_, renderer_, cache, input_, library_, launcher);

    // Support bounded frame count via CLASSIC_OS_MAX_FRAMES env var for automated verification
    int max_frames = 0;
    if (const char* env_frames = std::getenv("CLASSIC_OS_MAX_FRAMES")) {
        max_frames = std::atoi(env_frames);
    }

    uint64_t last_frame_us = MonotonicMicros();
    uint64_t last_input_ms = last_frame_us / 1000;
    int rendered_frames    = 0;
    bool running           = true;
    ExitTarget exit_target = (cfg_.return_target == "launch_stockui") ? ExitTarget::StockUI : ExitTarget::BootMenu;

    while (running) {
        const uint64_t frame_start_us = MonotonicMicros();
        const uint64_t now_ms         = frame_start_us / 1000;
        const float dt_sec            = static_cast<float>(frame_start_us - last_frame_us) / 1000000.0f;
        last_frame_us                 = frame_start_us;

        const auto actions = input_.PollActions(now_ms);
        if (!actions.empty()) {
            last_input_ms = now_ms;
        }

        for (NavAction act : actions) {
            if (!dashboard.HandleAction(act, exit_target)) {
                running = false;
                break;
            }
        }
        if (!running) break;

        dashboard.UpdateAnimation(dt_sec);
        renderer_.BeginFrame({11, 10, 18, 255});
        dashboard.Render();
        renderer_.EndFrame();

        ++rendered_frames;
        if (max_frames > 0 && rendered_frames >= max_frames) {
            launcher.WriteLaunchFileCommand(exit_target);
            break;
        }

        // Frame pacing: 16,960 us (~59 FPS, matching game_manager @ 0x6de8) when active,
        // dropping to 100,000 us (10 FPS) after 3 seconds of idle input to minimize CPU & heat
        const uint64_t target_frame_us =
            ((now_ms - last_input_ms) >= kIdleThresholdMs) ? kIdleFrameMicros : kActiveFrameMicros;
        const uint64_t elapsed_us = MonotonicMicros() - frame_start_us;
        if (elapsed_us < target_frame_us) {
            SleepMicros(target_frame_us - elapsed_us);
        }
    }

    cache.ClearAll();
    input_.Shutdown();
    renderer_.Shutdown();
    launcher.SetPowerButtonLock(1);
    return 0;
}

} // namespace classic_os
