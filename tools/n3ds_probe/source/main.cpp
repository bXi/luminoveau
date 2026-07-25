// Luminoveau 3DS toolchain probe. Answers the M0 bring-up questions on real
// hardware / Azahar before any engine code is ported. Results print to the
// bottom-screen console; each probe is independently compile-toggled from CMake
// so one failure doesn't hide the other answers.
//
// PASS/FAIL lines to record (they pick the primary vs. fallback designs in the
// engine port):
//   [threads]    -> ThreadPool as-is vs. LUMINOVEAU_NO_THREADS
//   [miniaudio]  -> null-device miniaudio vs. LUMINOVEAU_NO_AUDIO
//   [filesystem] -> std::filesystem over romfs vs. plain stdio
//   [c3d]        -> SDL window + citro3d coexist (triangle on top screen)
//   [sdl]        -> SDL3 video/input works at all

#include <3ds.h>
#include <cstdio>
#include <cstring>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef PROBE_THREADS
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

#ifdef PROBE_MINIAUDIO
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DEVICE_IO // decode/mix only; playback goes through ndsp later
#define MA_NO_THREADING // pessimistic default; flip off to probe MA threading
#include "miniaudio.h"
#endif

#ifdef PROBE_FILESYSTEM
#include <filesystem>
#endif

#ifdef PROBE_C3D
#include <citro3d.h>
extern "C" {
extern const u8  probe_shaders_shbin[]; // dkp_add_embedded_binary_library
extern const u32 probe_shaders_shbin_size;
}
#endif

static void report(const char *tag, bool ok, const char *detail = "") {
    std::printf("[%s] %s %s\n", tag, ok ? "PASS" : "FAIL", detail);
}

// ── std::thread probe ────────────────────────────────────────────────────────
static void probeThreads() {
#ifdef PROBE_THREADS
    bool ok = false;
    {
        std::mutex              m;
        std::condition_variable cv;
        int                     v = 0;
        std::thread             t([&] {
            std::lock_guard<std::mutex> lk(m);
            v = 42;
            cv.notify_one();
        });
        {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&] { return v == 42; });
        }
        t.join();
        ok = (v == 42);
    }
    report("threads", ok, "std::thread/mutex/condition_variable");
#else
    report("threads", false, "COMPILE-DISABLED (did not build)");
#endif
}

// ── miniaudio probe ──────────────────────────────────────────────────────────
static void probeMiniaudio() {
#ifdef PROBE_MINIAUDIO
    ma_engine_config cfg = ma_engine_config_init();
    cfg.noDevice         = MA_TRUE;
    cfg.channels         = 2;
    cfg.sampleRate       = 48000;
    ma_engine engine;
    ma_result r = ma_engine_init(&cfg, &engine);
    if (r == MA_SUCCESS) {
        float frames[256 * 2] = {};
        ma_engine_read_pcm_frames(&engine, frames, 256, nullptr);
        ma_engine_uninit(&engine);
    }
    report("miniaudio", r == MA_SUCCESS, "null-device engine init + read");
#else
    report("miniaudio", false, "COMPILE-DISABLED (did not build)");
#endif
}

// ── std::filesystem-over-romfs probe ─────────────────────────────────────────
static void probeFilesystem() {
#ifdef PROBE_FILESYSTEM
    bool iter = false, stdio = false;
    std::error_code ec;
    for (auto &e : std::filesystem::directory_iterator("romfs:/", ec)) {
        (void)e;
        iter = true;
        break;
    }
    if (FILE *f = std::fopen("romfs:/probe.txt", "rb")) {
        char buf[16] = {};
        std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        stdio = (std::strncmp(buf, "ok", 2) == 0);
    }
    std::printf("[filesystem] iter=%s stdio=%s (%s)\n",
        iter ? "PASS" : "FAIL", stdio ? "PASS" : "FAIL", ec.message().c_str());
#else
    report("filesystem", false, "COMPILE-DISABLED (did not build)");
#endif
}

// ── citro3d state ────────────────────────────────────────────────────────────
#ifdef PROBE_C3D
namespace c3dprobe {
C3D_RenderTarget *target = nullptr;
DVLB_s           *dvlb   = nullptr;
shaderProgram_s   program;
s8                uLocProjection = -1;
void             *vbo            = nullptr;
bool              ready          = false;

struct Vertex {
    float x, y, z;
    float r, g, b;
};

bool init() {
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
        return false;
    // Top screen: physical framebuffer is 240x400 (rotated).
    target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, C3D_DEPTHTYPE(-1));
    if (!target)
        return false;
    C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT,
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));

    dvlb = DVLB_ParseFile((u32 *)probe_shaders_shbin, probe_shaders_shbin_size);
    if (!dvlb)
        return false;
    shaderProgramInit(&program);
    shaderProgramSetVsh(&program, &dvlb->DVLE[0]);
    uLocProjection = shaderInstanceGetUniformLocation(program.vertexShader, "projection");

    static const Vertex verts[3] = {
        { 200.0f, 40.0f, 0.5f, 1.0f, 0.0f, 0.0f },
        { 100.0f, 200.0f, 0.5f, 0.0f, 1.0f, 0.0f },
        { 300.0f, 200.0f, 0.5f, 0.0f, 0.0f, 1.0f },
    };
    vbo = linearAlloc(sizeof(verts));
    if (!vbo)
        return false;
    std::memcpy(vbo, verts, sizeof(verts));
    ready = true;
    return true;
}

void frame() {
    if (!ready)
        return;
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_RenderTargetClear(target, C3D_CLEAR_ALL, 0x203040FF, 0);
    C3D_FrameDrawOn(target);

    C3D_BindProgram(&program);

    C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
    AttrInfo_Init(attrInfo);
    AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0 = position
    AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 3); // v1 = color

    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, vbo, sizeof(Vertex), 2, 0x10);

    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

    C3D_Mtx projection;
    Mtx_OrthoTilt(&projection, 0.0f, 400.0f, 240.0f, 0.0f, 0.0f, 1.0f, true);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLocProjection, &projection);

    C3D_DrawArrays(GPU_TRIANGLES, 0, 3);
    C3D_FrameEnd(0);
}

void quit() {
    if (vbo)
        linearFree(vbo);
    if (dvlb) {
        shaderProgramFree(&program);
        DVLB_Free(dvlb);
    }
    if (target)
        C3D_RenderTargetDelete(target);
    C3D_Fini();
}
} // namespace c3dprobe
#endif

// ── SDL callbacks ────────────────────────────────────────────────────────────
struct ProbeState {
    SDL_Window *window = nullptr;
};
static ProbeState g_state;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    *appstate = &g_state;

    // Bottom screen console BEFORE SDL video init, so we see logs even if SDL
    // resets the top screen.
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, nullptr);
    std::printf("Luminoveau n3ds_probe\n=====================\n");

    romfsInit();

    probeThreads();
    probeMiniaudio();
    probeFilesystem();

    bool sdlOk = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    report("sdl", sdlOk, sdlOk ? "" : SDL_GetError());
    if (sdlOk) {
        g_state.window = SDL_CreateWindow("probe", 400, 240, 0);
        report("sdl-window", g_state.window != nullptr,
            g_state.window ? "" : SDL_GetError());
    }

#ifdef PROBE_C3D
    // The critical probe: citro3d AFTER SDL created its window. If the triangle
    // shows on the top screen while SDL input works, the coexistence route is
    // viable exactly as the engine will use it.
    report("c3d", c3dprobe::init(), "C3D init after SDL window");
#else
    report("c3d", false, "COMPILE-DISABLED (did not build)");
#endif

    std::printf("\nPress A/B/dpad/touch to test input.\nSTART exits.\n");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;
    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_GAMEPAD_ADDED:
        SDL_OpenGamepad(event->gdevice.which);
        std::printf("gamepad added\n");
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        std::printf("button %d down\n", event->gbutton.button);
        if (event->gbutton.button == SDL_GAMEPAD_BUTTON_START)
            return SDL_APP_SUCCESS;
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
        std::printf("touch %.2f,%.2f\n", event->tfinger.x, event->tfinger.y);
        break;
    case SDL_EVENT_KEY_DOWN:
        std::printf("key %d down\n", (int)event->key.key);
        break;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    (void)appstate;
    if (!aptMainLoop())
        return SDL_APP_SUCCESS;
#ifdef PROBE_C3D
    c3dprobe::frame();
#else
    gspWaitForVBlank();
#endif
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate;
    (void)result;
#ifdef PROBE_C3D
    c3dprobe::quit();
#endif
    if (g_state.window)
        SDL_DestroyWindow(g_state.window);
    SDL_Quit();
    romfsExit();
    gfxExit();
}
