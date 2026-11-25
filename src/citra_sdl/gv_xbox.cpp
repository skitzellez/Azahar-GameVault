// Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
// PolyForm Noncommercial License 1.0.0 for this file's Game Vault parts; Azahar itself is GPLv2+.
#include "citra_sdl/gv_xbox.h"

#include <cstdio>
#include <SDL.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <appmodel.h>
#endif

namespace gv {

#ifdef _WIN32

bool IsXboxApp() {
    static const bool packaged = [] {
        UINT32 len = 0;
        return GetCurrentPackageFullName(&len, nullptr) == ERROR_INSUFFICIENT_BUFFER;
    }();
    return packaged;
}

std::string LocalDir() {
    // LocalState\Azahar\ from our SDL3 (SDL2 here is sdl2-compat); no shell/COM calls, which an Xbox app may lack
    static std::string dir = [] {
        char* pref = SDL_GetPrefPath("", "Azahar");
        std::string out = pref ? pref : "";
        SDL_free(pref);
        return out;
    }();
    return dir;
}

static std::string g_home;

void SetHome(const std::string& home) {
    g_home = home;
    while (!g_home.empty() && (g_home.back() == '/' || g_home.back() == 92)) {
        g_home.pop_back();
    }
}

static std::wstring Wide(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len > 0 ? len - 1 : 0, L'\0');
    if (len > 1) {
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    }
    return w;
}

static void Append(const std::string& path, const std::string& line, bool reset) {
    if (path.empty()) {
        return;
    }
    if (FILE* f = _wfopen(Wide(path).c_str(), reset ? L"w" : L"a")) {
        if (!reset) {
            fprintf(f, "%s\n", line.c_str());
        }
        fclose(f);
    }
}

static std::string HomeLog() {
    return g_home.empty() ? std::string() : g_home + "\\logs\\3DS (Azahar) - last session.txt";
}

void LogReset() {
    if (!IsXboxApp()) {
        return;
    }
    Append(LocalDir() + "xbox_start.txt", "", true);
    Append(HomeLog(), "", true);
}

void Log(const std::string& line) {
    if (!IsXboxApp()) {
        return;
    }
    Append(LocalDir() + "xbox_start.txt", line, false);
    Append(HomeLog(), line, false);
}

// Crash catcher: the exception, the module and offset it happened in, and the exe's return addresses on the stack
static LONG CALLBACK GvCrashHandler(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    // only real crashes (C++ exceptions, debugger and thread-name notices pass by)
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != 0xC0000409 &&
        code != 0xC06D007E && code != 0xC06D007F && code != EXCEPTION_PRIV_INSTRUCTION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static LONG once = 0;
    if (InterlockedExchange(&once, 1)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    char line[2048];
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    void* addr = info->ExceptionRecord->ExceptionAddress;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(addr), &mod)) {
        GetModuleFileNameA(mod, name, MAX_PATH);
    }
    snprintf(line, sizeof(line), "CRASH: code 0x%08lX in %s +0x%llX (thread %lu)", code, name,
             (unsigned long long)((uintptr_t)addr - (uintptr_t)mod), GetCurrentThreadId());
    Log(line);
    const uintptr_t exe = (uintptr_t)GetModuleHandleW(nullptr);
    const auto* nt = (const IMAGE_NT_HEADERS*)(exe + ((const IMAGE_DOS_HEADER*)exe)->e_lfanew);
    const uintptr_t end = exe + nt->OptionalHeader.SizeOfImage;
    const uintptr_t* sp = (const uintptr_t*)info->ContextRecord->Rsp;
    std::string stack = "CRASH: exe stack";
    int found = 0;
    for (int i = 0; i < 4096 && found < 24; i++) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (i % 512 == 0 && (!VirtualQuery(sp + i, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)) {
            break;
        }
        if (sp[i] > exe + 0x1000 && sp[i] < end) {
            char hex[24];
            snprintf(hex, sizeof(hex), " %llX", (unsigned long long)(sp[i] - exe));
            stack += hex;
            found++;
        }
    }
    Log(stack);
    return EXCEPTION_CONTINUE_SEARCH;
}

int RunXboxApp(int (*main)(int, char**)) {
    // SDL2 here is sdl2-compat, which has no SDL_RunApp: our SDL3 runs the WinRT app
    AddVectoredExceptionHandler(1, GvCrashHandler);
    HMODULE sdl3 = LoadPackagedLibrary(L"SDL3.dll", 0);
    using RunApp = int(__cdecl*)(int, char**, int(__cdecl*)(int, char**), void*);
    auto run = sdl3 ? reinterpret_cast<RunApp>(::GetProcAddress(sdl3, "SDL_RunApp")) : nullptr;
    if (!run) {
        Log("start: SDL3.dll has no SDL_RunApp");
        return -1;
    }
    return run(0, nullptr, main, nullptr);
}

namespace gl {

static HMODULE g_gl;
using CreateContextFn = HGLRC(WINAPI*)(HDC);
using CreateAttribsFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using MakeCurrentFn = BOOL(WINAPI*)(HDC, HGLRC);
using DeleteFn = BOOL(WINAPI*)(HGLRC);
using SwapFn = BOOL(WINAPI*)(HDC);
using GetProcFn = PROC(WINAPI*)(LPCSTR);
using CurrentFn = HGLRC(WINAPI*)();
using ShareFn = BOOL(WINAPI*)(HGLRC, HGLRC);
using IntervalFn = BOOL(WINAPI*)(int);
static CreateContextFn p_create;
static CreateAttribsFn p_create_attribs;
static MakeCurrentFn p_make_current;
static DeleteFn p_delete;
static SwapFn p_swap;
static GetProcFn p_get_proc;
static CurrentFn p_current;
static ShareFn p_share;
static IntervalFn p_interval;

bool Load() {
    if (g_gl) {
        return true;
    }
    // our Mesa from the package (LoadPackagedLibrary only looks in the package, never at Windows' opengl32)
    g_gl = LoadPackagedLibrary(L"opengl32.dll", 0);
    if (!g_gl) {
        Log("gl: opengl32.dll (Mesa) did not load, error " + std::to_string(GetLastError()));
        return false;
    }
    p_create = reinterpret_cast<CreateContextFn>(::GetProcAddress(g_gl, "wglCreateContext"));
    p_make_current = reinterpret_cast<MakeCurrentFn>(::GetProcAddress(g_gl, "wglMakeCurrent"));
    p_delete = reinterpret_cast<DeleteFn>(::GetProcAddress(g_gl, "wglDeleteContext"));
    p_swap = reinterpret_cast<SwapFn>(::GetProcAddress(g_gl, "wglSwapBuffers"));
    p_get_proc = reinterpret_cast<GetProcFn>(::GetProcAddress(g_gl, "wglGetProcAddress"));
    p_current = reinterpret_cast<CurrentFn>(::GetProcAddress(g_gl, "wglGetCurrentContext"));
    p_share = reinterpret_cast<ShareFn>(::GetProcAddress(g_gl, "wglShareLists"));
    const bool ok = p_create && p_make_current && p_delete && p_swap && p_get_proc;
    Log(std::string("gl: Mesa loaded") + (ok ? "" : ", but WGL functions are missing"));
    return ok;
}

void* CoreWindow(SDL_Window* window) {
    // sdl2-compat hands out SDL3's own windows: ask SDL3 for the CoreWindow property
    HMODULE sdl3 = GetModuleHandleW(L"SDL3.dll");
    using PropsFn = unsigned(__cdecl*)(SDL_Window*);
    using PtrFn = void*(__cdecl*)(unsigned, const char*, void*);
    auto props = sdl3 ? reinterpret_cast<PropsFn>(::GetProcAddress(sdl3, "SDL_GetWindowProperties")) : nullptr;
    auto ptr = sdl3 ? reinterpret_cast<PtrFn>(::GetProcAddress(sdl3, "SDL_GetPointerProperty")) : nullptr;
    void* cw = (props && ptr) ? ptr(props(window), "SDL.window.winrt.window", nullptr) : nullptr;
    Log(std::string("gl: CoreWindow ") + (cw ? "found" : "NOT found"));
    return cw;
}

void* CreateContext(void* hdc, void* share, int major, int minor) {
    HDC dc = static_cast<HDC>(hdc);
    HGLRC ctx = nullptr;
    // wglCreateContextAttribsARB needs a current context to be looked up: make a plain one first
    HGLRC plain = p_create(dc);
    if (!plain) {
        Log("gl: wglCreateContext failed");
        return nullptr;
    }
    if (major == 0) {
        // the window's own context: plain, like RetroArch's main context on Xbox (it presents on the CoreWindow)
        if (share && p_share) {
            p_share(static_cast<HGLRC>(share), plain);
        }
        return plain;
    }
    if (!p_create_attribs) {
        // wglGetProcAddress needs a current context. Never make a second context current on the CoreWindow: with
        // our Mesa it takes the window's picture over and nothing reaches the TV. The window's own context is
        // current here already; else the plain one is made current with no window (surfaceless).
        HGLRC before = p_current ? p_current() : nullptr;
        if (!before) {
            p_make_current(nullptr, plain);
        }
        p_create_attribs = reinterpret_cast<CreateAttribsFn>(p_get_proc("wglCreateContextAttribsARB"));
        p_interval = reinterpret_cast<IntervalFn>(p_get_proc("wglSwapIntervalEXT"));
        if (!before) {
            p_make_current(nullptr, nullptr);
        }
    }
    if (p_create_attribs) {
        const int attribs[] = {0x2091 /*MAJOR*/, major, 0x2092 /*MINOR*/, minor,
                               0x9126 /*PROFILE_MASK*/, 0x1 /*CORE*/, 0};
        ctx = p_create_attribs(dc, static_cast<HGLRC>(share), attribs);
    }
    if (ctx) {
        p_delete(plain);
    } else {
        Log("gl: no core " + std::to_string(major) + "." + std::to_string(minor) +
            " context, using the plain one");
        ctx = plain;
        if (share && p_share) {
            p_share(static_cast<HGLRC>(share), ctx);
        }
    }
    return ctx;
}

bool MakeCurrent(void* hdc, void* context) {
    return p_make_current(static_cast<HDC>(hdc), static_cast<HGLRC>(context)) != FALSE;
}

void DeleteContext(void* context) {
    if (context) {
        p_delete(static_cast<HGLRC>(context));
    }
}

void SwapBuffers(void* hdc) {
    p_swap(static_cast<HDC>(hdc));
}

void SwapInterval(int interval) {
    if (p_interval) {
        p_interval(interval);
    }
}

void* GetCurrentContext() {
    return p_current ? p_current() : nullptr;
}

void* GetProcAddress(const char* name) {
    void* p = reinterpret_cast<void*>(p_get_proc(name));
    if (!p) {
        p = reinterpret_cast<void*>(::GetProcAddress(g_gl, name)); // GL 1.1 functions
    }
    return p;
}

} // namespace gl

#else

bool IsXboxApp() {
    return false;
}
std::string LocalDir() {
    return {};
}
void SetHome(const std::string&) {}
void Log(const std::string&) {}
void LogReset() {}
int RunXboxApp(int (*)(int, char**)) {
    return -1;
}
namespace gl {
bool Load() {
    return false;
}
void* CoreWindow(SDL_Window*) {
    return nullptr;
}
void* CreateContext(void*, void*, int, int) {
    return nullptr;
}
bool MakeCurrent(void*, void*) {
    return false;
}
void DeleteContext(void*) {}
void SwapBuffers(void*) {}
void SwapInterval(int) {}
void* GetCurrentContext() {
    return nullptr;
}
void* GetProcAddress(const char*) {
    return nullptr;
}
} // namespace gl

#endif

} // namespace gv
