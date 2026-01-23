/*
 * RmlUI Backend - SDL3 GPU Integration for Luminoveau
 * Wraps RmlUi's platform and renderer for seamless integration
 */

#pragma once

#ifdef LUMINOVEAU_WITH_RMLUI

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include "RmlUi_Platform_SDL.h"
#include "RmlUi_Renderer_SDL_GPU.h"
#include <memory>

namespace RmlUI {
namespace Backend {

/**
 * @brief Backend data structure
 * Manages the RmlUi platform and renderer interfaces
 */
struct BackendData {
    std::unique_ptr<SystemInterface_SDL> system_interface;
    std::unique_ptr<RenderInterface_SDL_GPU> render_interface;
    
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUCommandBuffer* command_buffer = nullptr;
    SDL_GPUTexture* swapchain_texture = nullptr;
    uint32_t swapchain_width = 0;
    uint32_t swapchain_height = 0;
    
    bool initialized = false;
};

/**
 * @brief Initialize the backend
 * @param device SDL GPU device
 * @param window SDL window
 * @return True on success
 */
bool Initialize(SDL_GPUDevice* device, SDL_Window* window);

/**
 * @brief Shutdown the backend
 */
void Shutdown();

/**
 * @brief Get the backend data
 * @return Pointer to backend data
 */
BackendData* GetBackendData();

/**
 * @brief Begin a new frame
 * @param command_buffer Command buffer for this frame
 * @param swapchain_texture Swapchain texture to render to
 * @param width Swapchain width
 * @param height Swapchain height
 */
void BeginFrame(SDL_GPUCommandBuffer* command_buffer, SDL_GPUTexture* swapchain_texture, 
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
bool ProcessEvent(Rml::Context* context, SDL_Event& event);

} // namespace Backend
} // namespace RmlUI

#endif // LUMINOVEAU_WITH_RMLUI
