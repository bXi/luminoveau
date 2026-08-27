/*
 * RmlUI Backend Implementation
 */

#ifdef LUMINOVEAU_WITH_RMLUI

#include "rmluibackend.h"
#include "core/log/log.h"
#include "gpu/IGpu.h"
#include "platform/input/mouseinput_backend.h"
#include "platform/window/window.h"
#include "renderer/renderer.h"

namespace RmlUI {
namespace Backend {

// Global backend data
static BackendData g_backend_data;

BackendData *GetBackendData() {
    return &g_backend_data;
}

bool Initialize(SDL_GPUDevice *device, SDL_Window *window) {
    if (g_backend_data.initialized) {
        LOG_WARNING("RmlUI Backend already initialized");
        return true;
    }

    // **The window is required; the device is not.** `device` is an `SDL_GPUDevice` and is null on
    // the WebGPU backend by definition — demanding it here is what used to fail this function
    // outright on the web and take every document with it. The renderer goes through IGpu now and
    // never sees it; it is kept in the struct only for the SDL platform layer's own use.
    if (!window) {
        LOG_ERROR("RmlUI Backend: no window");
        return false;
    }

    g_backend_data.device = device;
    g_backend_data.window = window;

    // Create system interface
    g_backend_data.system_interface = std::make_unique<SystemInterface_SDL>();
    g_backend_data.system_interface->SetWindow(window);

    // Create render interface
    g_backend_data.render_interface = std::make_unique<RenderInterface_Lumi>();
    if (!g_backend_data.render_interface->Init(Renderer::GetGpu().GetSwapchainFormat())) {
        LOG_ERROR("RmlUI Backend: render interface failed to initialise");
        g_backend_data.render_interface.reset();
        g_backend_data.system_interface.reset();
        return false;
    }

    // Set RmlUi interfaces
    Rml::SetSystemInterface(g_backend_data.system_interface.get());
    Rml::SetRenderInterface(g_backend_data.render_interface.get());

    g_backend_data.initialized = true;

    LOG_INFO("RmlUI Backend initialized successfully");
    return true;
}

void Shutdown() {
    if (!g_backend_data.initialized) {
        return;
    }

    // Clean up render interface
    if (g_backend_data.render_interface) {
        g_backend_data.render_interface->Shutdown();
        g_backend_data.render_interface.reset();
    }

    // Clean up system interface
    g_backend_data.system_interface.reset();

    g_backend_data.device            = nullptr;
    g_backend_data.window            = nullptr;
    g_backend_data.command_buffer    = 0;
    g_backend_data.swapchain_texture = 0;
    g_backend_data.initialized       = false;

    LOG_INFO("RmlUI Backend shut down");
}

void BeginFrame(GpuCmdBufferHandle command_buffer, GpuTextureHandle swapchain_texture,
    uint32_t width, uint32_t height) {
    if (!g_backend_data.initialized) {
        return;
    }

    g_backend_data.command_buffer    = command_buffer;
    g_backend_data.swapchain_texture = swapchain_texture;
    g_backend_data.swapchain_width   = width;
    g_backend_data.swapchain_height  = height;

    if (g_backend_data.render_interface) {
        g_backend_data.render_interface->BeginFrame(command_buffer, swapchain_texture, width, height);
    }
}

void EndFrame() {
    if (!g_backend_data.initialized) {
        return;
    }

    if (g_backend_data.render_interface) {
        g_backend_data.render_interface->EndFrame();
    }

    g_backend_data.command_buffer    = 0;
    g_backend_data.swapchain_texture = 0;
}

bool ProcessEvent(Rml::Context *context, SDL_Event &event) {
    if (!g_backend_data.initialized || !context) {
        return false;
    }

#ifdef __EMSCRIPTEN__
    // **On the web the pointer does not come from SDL, so RmlUi must not read SDL's copy of it.**
    //
    // `PlatformInputBackend` installs its own document-level pointer listeners there and derives the
    // position from `#canvas`'s bounding rect — that is what `Input::GetMousePosition` returns and
    // what the 3D picking in every game state is built on. SDL's own Emscripten translation
    // disagrees with it, and RmlUi was the one consumer still taking SDL's: the cursor hit-tested
    // roughly two thirds of the way up the layout, so pointing at the fourth menu row lit the first.
    //
    // Overwriting the coordinates in the event, rather than bypassing `InputEventHandler`, keeps all
    // of RmlUi's own modifier and button mapping. `InputEventHandler` scales by the window's pixel
    // density on the way in, so divide it back out here; the backend already reports canvas pixels,
    // which is the space the context is sized in.
    const float density = SDL_GetWindowPixelDensity(g_backend_data.window);
    if (density > 0.0f
        && (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
            || event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL)) {
        const vf2d  p  = PlatformInputBackend::GetMousePosition();
        const float px = p.x / density;
        const float py = p.y / density;

        switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            event.motion.x = px;
            event.motion.y = py;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            event.button.x = px;
            event.button.y = py;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            event.wheel.mouse_x = px;
            event.wheel.mouse_y = py;
            break;
        default:
            break;
        }
    }
#endif

    return RmlSDL::InputEventHandler(context, g_backend_data.window, event);
}

} // namespace Backend
} // namespace RmlUI

#endif // LUMINOVEAU_WITH_RMLUI
