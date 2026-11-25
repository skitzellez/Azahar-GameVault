// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <glad/glad.h>
#include "citra_sdl/emu_window/emu_window_sdl2_gl.h"
#include "citra_sdl/gv_xbox.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "core/core.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

class SDLGLContext : public Frontend::GraphicsContext {
public:
    using SDL_GLContext = void*;

    // Game Vault (Xbox): one window only in an Xbox app, so the extra contexts draw with no window (surfaceless,
    // our Mesa); they share with the window's context (gv_share)
    static inline void* gv_hdc = nullptr;
    static inline void* gv_share = nullptr;

    SDLGLContext() {
        if (gv::IsXboxApp()) {
            window = nullptr;
            // Making a shared context must not change this thread's current context: the frame dumper's
            // context used to take the renderer's place (its VAOs/FBOs in the wrong context = black picture)
            void* before = gv::gl::GetCurrentContext();
            context = gv::gl::CreateContext(gv_hdc, gv_share, 4, 3);
            if (before) {
                gv::gl::MakeCurrent(before == gv_share ? gv_hdc : nullptr, before);
            }
            return;
        }
        window = SDL_CreateWindow(NULL, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                                  SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
        context = SDL_GL_CreateContext(window);
    }

    ~SDLGLContext() override {
        if (gv::IsXboxApp()) {
            gv::gl::DeleteContext(context);
            return;
        }
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
    }

    void MakeCurrent() override {
        if (gv::IsXboxApp()) {
            gv::gl::MakeCurrent(nullptr, context);
            return;
        }
        SDL_GL_MakeCurrent(window, context);
    }

    void DoneCurrent() override {
        if (gv::IsXboxApp()) {
            gv::gl::MakeCurrent(nullptr, nullptr);
            return;
        }
        SDL_GL_MakeCurrent(window, nullptr);
    }

private:
    SDL_Window* window;
    SDL_GLContext context;
};

static SDL_Window* CreateGLWindow(const std::string& window_title, bool gles) {
    if (gles) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    } else {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    }
    return SDL_CreateWindow(window_title.c_str(),
                            SDL_WINDOWPOS_UNDEFINED, // x position
                            SDL_WINDOWPOS_UNDEFINED, // y position
                            Core::kScreenTopWidth,
                            Core::kScreenTopHeight + Core::kScreenBottomHeight,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
}

EmuWindow_SDL2_GL::EmuWindow_SDL2_GL(Core::System& system_, bool fullscreen, bool is_secondary)
    : EmuWindow_SDL2{system_, is_secondary} {
    if (gv::IsXboxApp()) {
        // Game Vault (Xbox): the CoreWindow, OpenGL 4.3 core from our Mesa (D3D12) on it
        gv::Log("gl: creating the SDL window");
        render_window = SDL_CreateWindow("Azahar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                         Core::kScreenTopWidth,
                                         Core::kScreenTopHeight + Core::kScreenBottomHeight,
                                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        dummy_window = nullptr;
        gv::Log(std::string("gl: SDL window ") + (render_window ? "made" : "NOT made"));
        if (!render_window || !gv::gl::Load()) {
            gv::Log(std::string("gl: no window or no Mesa: ") + SDL_GetError());
            exit(1);
        }
        gv_hdc = gv::gl::CoreWindow(render_window);
        window_context = gv::gl::CreateContext(gv_hdc, nullptr, 0, 0);
        if (!window_context || !gv::gl::MakeCurrent(gv_hdc, window_context)) {
            gv::Log("gl: the window's context could not be made");
            exit(1);
        }
        SDLGLContext::gv_hdc = gv_hdc;
        SDLGLContext::gv_share = window_context;
        core_context = CreateSharedContext();
        if (core_context) {
            core_context->MakeCurrent(); // current (surfaceless) on this thread from here
        }
        last_saved_context = nullptr;
        render_window_id = SDL_GetWindowID(render_window);
        if (!core_context || !gladLoadGLLoader(static_cast<GLADloadproc>(gv::gl::GetProcAddress))) {
            gv::Log("gl: the GL functions could not be loaded");
            exit(1);
        }
        // Game Vault (Xbox): OpenGL's own messages about failed calls in the game's context, the first 40
        if (GLAD_GL_VERSION_4_3) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(
                [](GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* message,
                   const void*) {
                    static std::atomic<int> count{0};
                    if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH) {
                        if (++count <= 40) {
                            gv::Log("gl message: type " + std::to_string(type) + " id " + std::to_string(id) +
                                    ": " + message);
                        }
                    }
                },
                nullptr);
        }
        gv::Log(std::string("gl: ") + reinterpret_cast<const char*>(glGetString(GL_RENDERER)) + " | " +
                reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        if (FILE* f = fopen((gv::LocalDir() + "gvtest_red.txt").c_str(), "r")) {
            // TEST: red on the TV for 20 seconds straight from the window's context, no game involved
            fclose(f);
            gv::gl::MakeCurrent(gv_hdc, window_context);
            gv::Log("test: red for 20 s");
            for (int i = 0; i < 1200; i++) {
                glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                gv::gl::SwapBuffers(gv_hdc);
                SDL_PumpEvents();
                SDL_Delay(16);
            }
            gv::Log("test: red done, GL error " + std::to_string(glGetError()));
            core_context->MakeCurrent();
        }
        OnResize();
        SDL_PumpEvents();
        return;
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    // Enable context sharing for the shared context
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    // Enable vsync
    SDL_GL_SetSwapInterval(1);
    // Enable debug context
    if (Settings::values.renderer_debug) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }

    std::string window_title = fmt::format("Azahar {} | {}-{}", Common::g_build_fullname,
                                           Common::g_scm_branch, Common::g_scm_desc);

    // First, try to create a context with the requested type.
    render_window = CreateGLWindow(window_title, Settings::values.use_gles.GetValue());
    if (render_window == nullptr) {
        // On failure, fall back to context with flipped type.
        render_window = CreateGLWindow(window_title, !Settings::values.use_gles.GetValue());
        if (render_window == nullptr) {
            LOG_CRITICAL(Frontend, "Failed to create SDL2 window: {}", SDL_GetError());
            exit(1);
        }
    }

    strict_context_required = std::strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0;

    dummy_window = SDL_CreateWindow(NULL, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                                    SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);

    if (fullscreen) {
        Fullscreen();
    }

    window_context = SDL_GL_CreateContext(render_window);
    core_context = CreateSharedContext();
    last_saved_context = nullptr;

    if (window_context == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to create SDL2 GL context: {}", SDL_GetError());
        exit(1);
    }
    if (core_context == nullptr) {
        LOG_CRITICAL(Frontend, "Failed to create shared SDL2 GL context: {}", SDL_GetError());
        exit(1);
    }

    render_window_id = SDL_GetWindowID(render_window);

    int profile_mask = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile_mask);
    auto gl_load_func =
        profile_mask == SDL_GL_CONTEXT_PROFILE_ES ? gladLoadGLES2Loader : gladLoadGLLoader;

    if (!gl_load_func(static_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        LOG_CRITICAL(Frontend, "Failed to initialize GL functions: {}", SDL_GetError());
        exit(1);
    }

    OnResize();
    OnMinimalClientAreaChangeRequest(GetActiveConfig().min_client_area_size);
    SDL_PumpEvents();
}

EmuWindow_SDL2_GL::~EmuWindow_SDL2_GL() {
    core_context.reset();
    if (gv::IsXboxApp()) {
        gv::gl::DeleteContext(window_context);
        SDL_DestroyWindow(render_window);
        return;
    }
    SDL_DestroyWindow(render_window);
    SDL_GL_DeleteContext(window_context);
}

std::unique_ptr<Frontend::GraphicsContext> EmuWindow_SDL2_GL::CreateSharedContext() const {
    return std::make_unique<SDLGLContext>();
}

void EmuWindow_SDL2_GL::MakeCurrent() {
    core_context->MakeCurrent();
}

void EmuWindow_SDL2_GL::DoneCurrent() {
    core_context->DoneCurrent();
}

namespace OpenGL {
extern bool g_gv_frame_presented; // renderer_opengl.cpp
extern void (*g_gv_log)(const char*);
}

bool EmuWindow_SDL2_GL::GvPresentOnMainThread() {
    if (!gv::IsXboxApp()) {
        return false;
    }
    void* game_context = gv::gl::GetCurrentContext();
    gv::gl::MakeCurrent(gv_hdc, window_context);
    static int presented = 0;
    if (presented == 0) {
        OpenGL::g_gv_log = [](const char* line) { gv::Log(std::string("present: ") + line); };
        gv::gl::SwapInterval(0); // the game's own pacing paces the loop
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f); // TEST: red behind the game picture
    }
    OpenGL::g_gv_frame_presented = false;
    system.GPU().Renderer().TryPresent(0, is_secondary);
    if (!OpenGL::g_gv_frame_presented) {
        gv::gl::MakeCurrent(nullptr, game_context); // no new frame yet
        return false;
    }
    gv::gl::SwapBuffers(gv_hdc);
    if (++presented == 1 || presented == 600) {
        gv::Log("present (main thread): " + std::to_string(presented) + " swaps, GL error " +
                std::to_string(glGetError()));
    }
    gv::gl::MakeCurrent(nullptr, game_context);
    return true;
}

void EmuWindow_SDL2_GL::SaveContext() {
    if (gv::IsXboxApp()) {
        last_saved_context = gv::gl::GetCurrentContext();
        return;
    }
    last_saved_context = SDL_GL_GetCurrentContext();
}

void EmuWindow_SDL2_GL::RestoreContext() {
    if (gv::IsXboxApp()) {
        gv::gl::MakeCurrent(last_saved_context == window_context ? gv_hdc : nullptr, last_saved_context);
        return;
    }
    SDL_GL_MakeCurrent(render_window, last_saved_context);
}

void EmuWindow_SDL2_GL::Present() {
    if (gv::IsXboxApp()) {
        // Game Vault (Xbox): frames reach the TV only when sent from the window's own thread, so the main loop
        // shows them (GvPresentOnMainThread); this thread just waits
        while (IsOpen()) {
            SDL_Delay(50);
        }
        return;
    }
    if (false) {
        const bool current = gv::gl::MakeCurrent(gv_hdc, window_context);
        GLint fb = -1;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
        gv::Log("present: window context current " + std::string(current ? "yes" : "NO") + ", draw framebuffer " +
                std::to_string(fb) + ", GL error " + std::to_string(glGetError()));
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f); // TEST: red behind the game picture
        gv::gl::SwapInterval(1);
        int presented = 0;
        while (IsOpen()) {
            system.GPU().Renderer().TryPresent(100, is_secondary);
            gv::gl::SwapBuffers(gv_hdc);
            if (++presented == 1 || presented == 60 || presented == 600) {
                gv::Log("present: " + std::to_string(presented) + " swaps, GL error " + std::to_string(glGetError()));
            }
        }
        gv::gl::MakeCurrent(nullptr, nullptr);
        return;
    }
    SDL_GL_MakeCurrent(render_window, window_context);
    SDL_GL_SetSwapInterval(1);
    while (IsOpen()) {
        system.GPU().Renderer().TryPresent(100, is_secondary);
        SDL_GL_SwapWindow(render_window);
    }
    SDL_GL_MakeCurrent(render_window, nullptr);
}
