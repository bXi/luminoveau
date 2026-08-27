/*
 * RmlUI Backend - SDL3 GPU Integration for Lumi
 * Wraps RmlUi's platform and renderer for seamless integration
 */

#pragma once

#ifdef LUMINOVEAU_WITH_RMLUI

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include "RmlUi_Platform_SDL.h"
#include "integrations/rmlui/rmluirenderinterface.h"
#include <memory>

namespace RmlUI {
namespace Backend {

/**
 * @brief Backend data structure
 * Manages the RmlUi platform and renderer interfaces
 */
struct BackendData {
    std::unique_ptr<SystemInterface_SDL>      system_interface;
    std::unique_ptr<RenderInterface_Lumi>     render_interface;

    SDL_Window    *window = nullptr;

    /// Kept for the SDL platform layer, which still wants a device on that backend. Null on
    /// WebGPU, and nothing in the renderer reads it any more.
    SDL_GPUDevice *device = nullptr;

    GpuCmdBufferHandle command_buffer    = 0;
    GpuTextureHandle   swapchain_texture = 0;
    uint32_t           swapchain_width   = 0;
    uint32_t           swapchain_height  = 0;

    bool initialized = false;
};

/**
 * @brief Initialize the backend
 * @param device SDL GPU device
 * @param window SDL window
 * @return True on success
 */
bool Initialize(SDL_GPUDevice *device, SDL_Window *window);

/**
 * @brief Shutdown the backend
 */
void Shutdown();

/**
 * @brief Get the backend data
 * @return Pointer to backend data
 */
BackendData *GetBackendData();

/**
 * @brief Begin a new frame
 * @param command_buffer Command buffer for this frame
 * @param swapchain_texture Swapchain texture to render to
 * @param width Swapchain width
 * @param height Swapchain height
 */
/// **Engine handles, not SDL pointers.** The renderer behind this goes through IGpu now, and
/// `GpuCmdBufferHandle`/`GpuTextureHandle` are what both backends speak — on WebGPU there is no
/// `SDL_GPUTexture` to pass. Callers previously reinterpret_cast into the SDL types; they now
/// pass the handles straight through.
void BeginFrame(GpuCmdBufferHandle command_buffer, GpuTextureHandle swapchain_texture,
    uint32_t width, uint32_t height);

/**
 * @brief End the current frame
 */
void EndFrame();

/**
 * @brief Process an SDL event
 * @param context RmlUi context to send events to
 * @param event SDL event
 * @return True if event was handled by RmlUi
 */
bool ProcessEvent(Rml::Context *context, SDL_Event &event);

} // namespace Backend
} // namespace RmlUI

#endif // LUMINOVEAU_WITH_RMLUI
