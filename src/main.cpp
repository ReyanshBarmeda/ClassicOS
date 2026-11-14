#include "app.hpp"
#include <cstring>
#include <iostream>

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::cout << "Classic OS v1.0.0 — Native PlayStation Classic (SCPH-1000R) Dashboard for Project Eris\n";
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0) {
            std::cout << "Usage: classic_os [options]\n"
                      << "  --project-eris-path <dir>  Override /media/project_eris root\n"
                      << "  --database-folder <dir>    Override regional.db / BleemSync.db folder\n"
                      << "  --games-folder <dir>       Override /media/games folder\n"
                      << "  --roms-folder <dir>        Override /media/roms folder\n"
                      << "  --log-folder <dir>         Override /media/logs folder\n"
                      << "  --return-target <token>    Supervisor exit command (launch_bootmenu or launch_stockui)\n"
                      << "  --use-retroarch            Prefer RetroArch cores for PS1 games\n";
            return 0;
        }
    }

    classic_os::Application app(argc, argv);
    return app.Run();
}
