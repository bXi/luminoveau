/*
 * RmlUI Backend Implementation
 */

#ifdef LUMINOVEAU_WITH_RMLUI

#include "rmluibackend.h"
#include "log/loghandler.h"
#include "window/windowhandler.h"

namespace RmlUI {
namespace Backend {

// Global backend data
static BackendData g_backend_data;

BackendData* GetBackendData() {
    return &g_backend_data;
}

bool Initialize(SDL_GPUDevice* device, SDL_Window* window) {
    if (g_backend_data.initialized) {
        LOG_WARNING("RmlUI Backend already initialized");
        return true;
    }

    if (!device || !window) {
        LOG_ERROR("RmlUI Backend: Invalid device or window");
        return false;
    }

    g_backend_data.device = device;
    g_backend_data.window = window;

    // Create system interface
    g_backend_data.system_interface = std::make_unique<SystemInterface_SDL>();
    g_backend_data.system_interface->SetWindow(window);

    // Create render interface
    g_backend_data.render_interface = std::make_unique<RenderInterface_SDL_GPU>(device, window);

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

    g_backend_data.device = nullptr;
    g_backend_data.window = nullptr;
    g_backend_data.command_buffer = nullptr;
    g_backend_data.swapchain_texture = nullptr;
    g_backend_data.initialized = false;

    LOG_INFO("RmlUI Backend shut down");
}

void BeginFrame(SDL_GPUCommandBuffer* command_buffer, SDL_GPUTexture* swapchain_texture, 
                uint32_t width, uint32_t height) {
    if (!g_backend_data.initialized) {
        return;
    }

    g_backend_data.command_buffer = command_buffer;
    g_backend_data.swapchain_texture = swapchain_texture;
    g_backend_data.swapchain_width = width;
    g_backend_data.swapchain_height = height;

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

    g_backend_data.command_buffer = nullptr;
    g_backend_data.swapchain_texture = nullptr;
}

bool ProcessEvent(Rml::Context* context, SDL_Event& event) {
    if (!g_backend_data.initialized || !context) {
        return false;
    }

    return RmlSDL::InputEventHandler(context, g_backend_data.window, event);
}

} // namespace Backend
} // namespace RmlUI

#endif // LUMINOVEAU_WITH_RMLUI
