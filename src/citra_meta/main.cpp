// Copyright 2024-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <iostream>

#include "citra_cli/citra_cli.h"
#include "common/detached_tasks.h"
#include "common/scope_exit.h"

#if !defined(ENABLE_QT) && !defined(ENABLE_SDL2_FRONTEND)
#error "citra_meta is somehow building with no frontend. This should be impossible!"
#endif

#ifdef ENABLE_QT
#include "citra_qt/citra_qt.h"
#endif
#ifdef ENABLE_ROOM
#include "citra_room/citra_room.h"
#endif
#ifdef ENABLE_SDL2_FRONTEND
#include "citra_sdl/citra_sdl.h"
#include "citra_sdl/gv_xbox.h"
#include "common/file_util.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cctype>
#include <exception>
#include <cstdlib>
#include <string>

// Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
// Game Vault (Xbox): start-up as an Xbox app. Game Vault starts a game with the link
// gamevault-azahar:?game=<path>[&home=<drive folder>][&launchOnExit=gamevault:...] (cmd= works too).
// Settings, saves and the system files live in LocalState\Azahar.
static std::string GvUrlDecode(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '%' && i + 2 < in.size() && isxdigit((unsigned char)in[i + 1]) &&
            isxdigit((unsigned char)in[i + 2])) {
            out += (char)strtol(in.substr(i + 1, 2).c_str(), nullptr, 16);
            i += 2;
        } else {
            out += in[i] == '+' ? ' ' : in[i];
        }
    }
    return out;
}

static std::string GvLinkValue(const std::string& link, const char* key) {
    const std::string k = std::string(key) + "=";
    size_t q = link.find("?" + k);
    if (q == std::string::npos) {
        q = link.find("&" + k);
    }
    if (q == std::string::npos) {
        return {};
    }
    q += 1 + k.size();
    const size_t end = link.find('&', q);
    return GvUrlDecode(link.substr(q, end == std::string::npos ? std::string::npos : end - q));
}

static int __cdecl GvXboxMain(int, char**) {
    // SDL audio devices hang in an Xbox app; the Xbox controller needs Windows.Gaming.Input (off unless asked)
    SDL_SetHint("SDL_AUDIO_DRIVER", "dummy");
    SDL_SetHint("SDL_JOYSTICK_WGI", "1");
    // only Windows.Gaming.Input: SDL's XInput fallback re-enters it mid-dispatch (memory corruption at start)
    SDL_SetHint("SDL_XINPUT_ENABLED", "0");
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    const char* uri = nullptr;
    for (int i = 0; i < 300 && (uri = SDL_GetHint("SDL_WINRT_ACTIVATION_URI")) == nullptr; i++) {
        SDL_PumpEvents();
        SDL_Delay(10);
    }
    // an unhandled C++ error on any thread ends the app silently: log it first
    std::set_terminate([] {
        std::string what = "unknown";
        if (auto e = std::current_exception()) {
            try {
                std::rethrow_exception(e);
            } catch (const std::exception& ex) {
                what = ex.what();
            } catch (...) {
            }
        }
        gv::Log("terminate: unhandled error: " + what);
        std::abort();
    });
    const std::string link = uri ? uri : "";
    gv::SetHome(GvLinkValue(link, "home"));
    gv::LogReset();
    gv::Log("Azahar for Game Vault, link: " + link);
    static std::string s_game;
    s_game = GvLinkValue(link, "game");
    if (s_game.empty()) {
        s_game = GvLinkValue(link, "cmd");
    }
    if (s_game.empty()) {
        // test hook: LocalState\Azahar\gvtest_game.txt holds a game path for a start from the tile
        if (FILE* f = fopen((gv::LocalDir() + "gvtest_game.txt").c_str(), "r")) {
            char line[1024] = {};
            if (fgets(line, sizeof(line), f)) {
                s_game = line;
                while (!s_game.empty() && (s_game.back() == 0x0A || s_game.back() == 0x0D)) {
                    s_game.pop_back();
                }
            }
            fclose(f);
        }
    }
    gv::Log("user folder: " + gv::LocalDir());
    gv::Log("game: " + (s_game.empty() ? std::string("(none)") : s_game));
    FileUtil::SetUserPath(gv::LocalDir());
    if (FILE* f = fopen((gv::LocalDir() + "gvtest_nopad.txt").c_str(), "r")) {
        fclose(f);
        _putenv("GV_NOPAD=1"); // TEST: no SDL controller code
        gv::Log("TEST: controller code off");
    }
    if (s_game.empty()) {
        gv::Log("no game: started from the tile without one");
        return 0;
    }
    static char a0[] = "azahar";
    static char a1[] = "-f";
    static char* s_argv[] = {a0, a1, s_game.data(), nullptr};
    const int ret = LaunchSdlFrontend(3, s_argv);
    gv::Log("exit code " + std::to_string(ret));
    return ret;
}
#endif

#ifdef _WIN32
extern "C" {
// tells Nvidia and AMD drivers to use the dedicated GPU by default on laptops with switchable
// graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#if CITRA_HAS_SSE42
#include "common/x64/cpu_detect.h"
#ifdef _WIN32
#include <windows.h>
#endif

static bool CheckAndReportSSE42() {
    const auto& caps = Common::GetCPUCaps();
    if (!caps.sse4_2) {
        const std::string error_msg =
            "This application requires a CPU with SSE4.2 support or higher.\nTo run on unsupported "
            "systems, recompile the application with the ENABLE_SSE42 option disabled.";
#ifdef _WIN32
        MessageBoxA(nullptr, error_msg.c_str(), "Incompatible CPU", MB_OK | MB_ICONERROR);
#endif
        std::cerr << "Error: " << error_msg << std::endl;
        return false;
    }
    return true;
}
#endif

int main(int argc, char* argv[]) {
#ifdef ENABLE_SDL2_FRONTEND
    // Game Vault (Xbox): inside an app package our SDL3 runs the WinRT app and calls GvXboxMain
    if (gv::IsXboxApp()) {
        return gv::RunXboxApp(GvXboxMain);
    }
#endif
    Common::DetachedTasks detached_tasks;
    SCOPE_EXIT({ detached_tasks.WaitForAllTasks(); });

#if CITRA_HAS_SSE42
    if (!CheckAndReportSSE42()) {
        return 1;
    }
#endif

    if (CitraCLI::CheckForOptions(CitraCLI::cli_capture_optstring, argc, argv)) {
        return CitraCLI::ParseCommand(argc, argv);
    }

#if ENABLE_ROOM
    bool launch_room = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--room") == 0) {
            launch_room = true;
        }
    }

    if (launch_room) {
        return LaunchRoom(argc, argv, true);
    }
#endif

#if ENABLE_QT
    bool no_gui = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-gui") == 0 || strcmp(argv[i], "-n") == 0) {
            no_gui = true;
        }
    }
    if (!no_gui) {
        return LaunchQtFrontend(argc, argv);
    }
#endif

#if ENABLE_SDL2_FRONTEND
    return LaunchSdlFrontend(argc, argv);
#else
    return -1;
#endif
}
