// Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
// Game Vault (Xbox): helpers for running Azahar's SDL frontend as an Xbox Dev Mode app. In the package: our Xbox SDL3
// (sdl3-uwp) as SDL3.dll, sdl2-compat as SDL2.dll, and our Mesa (D3D12) as opengl32.dll for OpenGL.
#pragma once

#include <string>

struct SDL_Window;

namespace gv {

// True inside an app package (the Xbox app); false for the normal PC build.
bool IsXboxApp();

// Lines in <LocalState>\Azahar\xbox_start.txt (and <home>\logs\3DS (Azahar) - last session.txt when a home is set).
void Log(const std::string& line);
void LogReset();
void SetHome(const std::string& home);

// <LocalState>\Azahar\ (ends with a backslash)
std::string LocalDir();

// Runs `main` as the WinRT app (our SDL3's SDL_RunApp); returns its exit code.
int RunXboxApp(int (*main)(int, char**));

// OpenGL through our Mesa on the Xbox window (WGL on the CoreWindow, extra contexts surfaceless)
namespace gl {
bool Load();                        // opengl32.dll from the package
void* CoreWindow(SDL_Window* window); // the window's CoreWindow (the "HDC" Mesa takes)
void* CreateContext(void* hdc, void* share, int major, int minor); // core profile, shared with `share`
bool MakeCurrent(void* hdc, void* context); // hdc nullptr = surfaceless
void DeleteContext(void* context);
void SwapBuffers(void* hdc);
void SwapInterval(int interval);
void* GetCurrentContext();
void* GetProcAddress(const char* name);
} // namespace gl

} // namespace gv
