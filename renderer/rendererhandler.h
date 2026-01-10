#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <optional>
#include <utility>

#include "SDL3/SDL.h"

#include "utils/lerp.h"
#include "utils/uniformobject.h"
#include "utils/vectors.h"

#include "input/inputhandler.h"
#include "eventbus/eventbushandler.h"

#include "utils/colors.h"
#include "assettypes/shader.h"
#include "assettypes/texture.h"

#include "renderpass.h"
#include "renderable.h"
#include "sdl_gpu_structs.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef LUMINOVEAU_WITH_IMGUI
#include "imgui.h"
#include "backends/imgui_impl_sdlgpu3.h"
#include "backends/imgui_impl_sdl3.h"

#ifdef WIN32
#include "backends/imgui_impl_win32.h"
#endif
#endif

// SDL Forward Declarations
struct SDL_Window;
union SDL_Event;

struct FrameBuffer {
    SDL_GPUTexture *fbContent = nullptr;          // Resolved non-MSAA texture (for screen display)
    SDL_GPUTexture *fbContentMSAA = nullptr;      // MSAA texture (for rendering when MSAA enabled)
    SDL_GPUTexture *fbDepthMSAA = nullptr;        // MSAA depth texture (shared by all passes)

    uint32_t width  = 0;
    uint32_t height = 0;

    std::vector<std::pair<std::string, RenderPass *>> renderpasses;

    bool renderToScreen = false;
};

class RenderPass;

/**
 * @brief Provides functionality for GPU rendering and resource management.
 */
class Renderer {
public:
    /**
     * @brief Initializes the rendering system and creates the GPU device.
     *
     * Sets up the SDL GPU device, creates samplers, initializes shaders,
     * and creates the primary framebuffer with default render passes.
     */
    static void InitRendering() { get()._initRendering(); }

    /**
     * @brief Retrieves the SDL GPU device.
     *
     * @return Pointer to the SDL GPU device.
     */
    static SDL_GPUDevice *GetDevice() { return get()._getDevice(); }

    /**
     * @brief Starts a new rendering frame.
     *
     * Initializes ImGui frame if enabled. Call this before any rendering commands.
     */
    static void StartFrame() { get()._startFrame(); }

    /**
     * @brief Ends the current rendering frame and submits GPU commands.
     *
     * Acquires the swapchain texture, executes all render passes,
     * renders ImGui if enabled, and submits the command buffer.
     */
    static void EndFrame() { get()._endFrame(); }

    /**
     * @brief Resets all render passes and recreates GPU resources.
     *
     * Releases existing render pass resources and reinitializes them.
     * Typically called after window resize or graphics settings change.
     */
    static void Reset() { get()._reset(); }

    /**
     * @brief Clears the background with the specified color.
     *
     * @param color The color to clear the background with.
     */
    static void ClearBackground(Color color) { get()._clearBackground(color); }

    /**
     * @brief Adds a renderable object to the specified render pass queue.
     *
     * @param passname Name of the render pass to add to.
     * @param renderable The renderable object to queue for rendering.
     */
    static void AddToRenderQueue(const std::string &passname, const Renderable &renderable) {
        get()._addToRenderQueue(passname, renderable);
    }

    /**
     * @brief Creates and adds a custom shader render pass.
     *
     * @param passname Name of the new render pass.
     * @param vertShader Vertex shader asset.
     * @param fragShader Fragment shader asset.
     * @param targetBuffers List of framebuffer names to attach this pass to (defaults to primaryFramebuffer).
     */
    static void AddShaderPass(const std::string &passname, const ShaderAsset &vertShader, const ShaderAsset &fragShader,
                              std::vector<std::string> targetBuffers = std::vector<std::string>()) {
        get()._addShaderPass(passname, vertShader, fragShader, std::move(targetBuffers));
    }

    /**
     * @brief Attaches an existing render pass to a framebuffer.
     *
     * @param renderPass Pointer to the render pass to attach.
     * @param passname Name to identify the render pass.
     * @param fbName Name of the framebuffer to attach to.
     */
    static void AttachRenderPassToFrameBuffer(RenderPass *renderPass, const std::string &passname, const std::string &fbName) {
        get()._attachRenderPassToFrameBuffer(renderPass, passname, fbName);
    }

    /**
     * @brief Retrieves the uniform buffer for a specific render pass.
     *
     * @param passname Name of the render pass.
     * @return Reference to the render pass's uniform buffer.
     */
    static UniformBuffer &GetUniformBuffer(const std::string &passname) {
        return get()._getUniformBuffer(passname);
    }

    /**
     * @brief Creates a new framebuffer with the specified name.
     *
     * @param fbname Name of the framebuffer to create.
     */
    static void CreateFrameBuffer(const std::string &fbname) {
        return get()._createFrameBuffer(fbname);
    }

    /**
     * @brief Sets whether a framebuffer should render directly to screen.
     *
     * @param fbName Name of the framebuffer.
     * @param render True to render to screen, false otherwise.
     */
    static void SetFramebufferRenderToScreen(const std::string &fbName, bool render) {
        get()._setFramebufferRenderToScreen(fbName, render);
    }

    /**
     * @brief Gets and increments the global Z-index counter.
     *
     * Used for ordering renderables in depth.
     *
     * @return The next available Z-index value.
     */
    static uint32_t GetZIndex() { return get()._zIndex++; }

    /**
     * @brief Retrieves a framebuffer by name.
     *
     * @param fbname Name of the framebuffer to retrieve.
     * @return Pointer to the framebuffer, or nullptr if not found.
     */
    static FrameBuffer *GetFramebuffer(std::string fbname) {
        return get()._getFramebuffer(std::move(fbname));
    }

    /**
     * @brief Retrieves a texture sampler for the specified scale mode.
     *
     * @param scalemode The texture scaling mode (NEAREST or LINEAR).
     * @return Pointer to the GPU sampler.
     */
    static SDL_GPUSampler *GetSampler(ScaleMode scalemode) {
        return get()._getSampler(scalemode);
    }

    /**
     * @brief Gets the active SDL GPU render pass for a specific render pass.
     *
     * @param passname Name of the render pass.
     * @return Pointer to the SDL GPU render pass, or nullptr if not found.
     */
    static SDL_GPURenderPass *GetRenderPass(const std::string &passname) {
        return get()._getRenderPass(passname);
    }

    /**
     * @brief Enables scissor testing for a render pass with the specified clip rectangle.
     *
     * @param passname Name of the render pass.
     * @param cliprect Rectangle defining the scissor region.
     */
    static void SetScissorMode(std::string passname, rectf cliprect) {
        get()._setScissorMode(passname, cliprect);
    }

    /**
     * @brief Handles window resize events by updating camera and recreating framebuffers.
     *
     * Waits for GPU idle, updates camera projection, and recreates all framebuffer
     * textures at the new window size.
     */
    static void OnResize() { get()._onResize(); }

    /**
     * @brief Updates the camera projection matrix to match current window size.
     *
     * This is called immediately during window resize to prevent visual artifacts.
     * Does not recreate GPU resources - that happens later in OnResize().
     */
    static void UpdateCameraProjection() { get()._updateCameraProjection(); }

    /**
     * @brief Retrieves a 1x1 white pixel texture for rendering solid colors.
     *
     * @return The white pixel texture.
     */
    static Texture WhitePixel() { return get()._whitePixel(); }

    /**
     * @brief Retrieves the default quad geometry (unit quad from 0,0 to 1,1).
     *
     * @return Pointer to the quad geometry.
     */
    static Geometry2D* GetQuadGeometry() { return get()._getQuadGeometry(); }
    
    /**
     * @brief Retrieves a circle geometry with the specified number of segments.
     *
     * @param segments Number of triangle segments (default 32).
     * @return Pointer to the circle geometry.
     */
    static Geometry2D* GetCircleGeometry(int segments = 32) { return get()._getCircleGeometry(segments); }


    /**
     * @brief Gets the current MSAA sample count setting.
     *
     * @return The current sample count (e.g., SDL_GPU_SAMPLECOUNT_1, SDL_GPU_SAMPLECOUNT_4).
     */
    static SDL_GPUSampleCount GetSampleCount() { return get().currentSampleCount; }

    /**
     * @brief Sets the MSAA sample count and recreates render passes.
     *
     * Changes the multi-sample anti-aliasing level. Triggers a full reset
     * of all render passes to apply the new sample count.
     *
     * @param sampleCount The new sample count to use.
     */
    static void SetSampleCount(SDL_GPUSampleCount sampleCount) {
        get()._setSampleCount(sampleCount);
    }

private:
    SDL_GPUDevice        *m_device = nullptr;
    SDL_GPUCommandBuffer *m_cmdbuf = nullptr;

    uint32_t _zIndex = 0;

    std::vector<std::pair<std::string, FrameBuffer *>> frameBuffers;

    void
    _addShaderPass(const std::string &passname, const ShaderAsset &vertShader, const ShaderAsset &fragShader, std::vector<std::string> targetBuffers);

    void _attachRenderPassToFrameBuffer(RenderPass *renderPass, const std::string &passname, const std::string &fbName);

    void _addToRenderQueue(const std::string &passname, const Renderable &renderable);

    void _initRendering();

    void _close();

    SDL_GPUDevice *_getDevice();

    void _startFrame() const;

    void _clearBackground(Color color);

    void _endFrame();

    void _reset();

    void _onResize();

    void _updateCameraProjection();

    void _createFrameBuffer(const std::string &fbname);

    void _setFramebufferRenderToScreen(const std::string &fbName, bool render);

    SDL_GPURenderPass *_getRenderPass(const std::string &passname);

    void _setScissorMode(const std::string &passname, const rectf &cliprect);

    TextureAsset _whitePixelTexture;

    Texture _whitePixel();

    Geometry2D* _getQuadGeometry();
    Geometry2D* _getCircleGeometry(int segments);

    UniformBuffer &_getUniformBuffer(const std::string &passname);

    TextureAsset _screenBuffer;
    TextureAsset fs;
    SDL_GPUShader *rtt_vertex_shader;
    SDL_GPUShader *rtt_fragment_shader;

    void renderFrameBuffer(SDL_GPUCommandBuffer *cmd_buffer);

    FrameBuffer *_getFramebuffer(std::string fbname);

    void _setSampleCount(SDL_GPUSampleCount sampleCount);

    struct Uniforms {
        glm::mat4 camera;
        glm::mat4 model;
        glm::vec2 flipped;

        // TODO: fix this ugly mess
        glm::vec2 uv0 = glm::vec2(1.0, 1.0);
        glm::vec2 uv1 = glm::vec2(0.0, 1.0);
        glm::vec2 uv2 = glm::vec2(1.0, 0.0);
        glm::vec2 uv3 = glm::vec2(0.0, 1.0);
        glm::vec2 uv4 = glm::vec2(0.0, 0.0);
        glm::vec2 uv5 = glm::vec2(1.0, 0.0);

        float tintColorR = 1.0f;
        float tintColorG = 1.0f;
        float tintColorB = 1.0f;
        float tintColorA = 1.0f;
    };

    SDL_GPUSampler *_getSampler(ScaleMode scaleMode);

    std::unordered_map<ScaleMode, SDL_GPUSampler *> _samplers;

    SDL_GPUSampleCount currentSampleCount = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUGraphicsPipeline *m_rendertotexturepipeline{nullptr};

    SDL_GPUTexture *swapchain_texture = nullptr;
    glm::mat4x4    m_camera           = {};

    SDL_GPUColorTargetDescription     color_target_descriptions;
    SDL_GPUGraphicsPipelineCreateInfo rtt_pipeline_create_info;

public:
    Renderer(const Renderer &) = delete;

    static Renderer &get() {
        static Renderer instance;
        return instance;
    }

private:
    Renderer() {
    };
};