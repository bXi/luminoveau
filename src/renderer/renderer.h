#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <optional>
#include <utility>
#include <unordered_map>
#include <vector>

#include "gpu/IGpu.h"

#include "SDL3/SDL.h"

#include "gpu/types.h"
#include "util/lerp.h"
#include "gpu/buffer/uniformobject.h"
#include "math/vectors.h"

#include "platform/input/input.h"
#include "core/eventbus/eventbus.h"

#include "types/color.h"
#include "config.h"
#include "assets/shader/shader.h"
#include "assets/texture/texture.h"
#include "assets/compute/computepipeline.h"

#include "gpu/renderpass.h"
#include "gpu/renderable.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef LUMINOVEAU_WITH_IMGUI
#include "imgui.h"
#endif

// SDL Forward Declarations
struct SDL_Window;
union SDL_Event;
struct SDL_GPUDevice;

// Forward declare RenderPass before FrameBuffer uses it
class RenderPass;

/// @brief Configuration for creating a sprite render target (off-screen or on-screen).
struct SpriteRenderTargetConfig {
    BlendMode       blendMode     = BlendMode::SrcAlpha; ///< Blend mode used when drawing into the target.
    bool            clearOnLoad   = true;                ///< Whether to clear the target at the start of each frame.
    Color           clearColor    = BLACK;              ///< Clear color used when clearOnLoad is true.
    bool            renderToScreen = false;             ///< If true, composite this target onto the swapchain.
    uint32_t        width         = 0;                  ///< Target width in pixels (0 = desktop size).
    uint32_t        height        = 0;                  ///< Target height in pixels (0 = desktop size).
    size_t          maxSprites    = 0;                  ///< Sprite batch capacity (0 = MAX_SPRITES default).
    GpuTextureFormat format       = GpuTextureFormat::Invalid; ///< Pixel format (Invalid = swapchain format; e.g. R16G16B16A16_Float for HDR).
    bool            preComputeFlush = false;            ///< Render this target's passes before compute dispatches (same-frame compute reads).
};

/// @brief A render target: its textures, the passes that draw into it, and compositing flags.
struct FrameBuffer {
    GpuTextureHandle fbContent     = 0;  ///< Resolved non-MSAA texture (for screen display).
    GpuTextureHandle fbContentMSAA = 0;  ///< MSAA color texture (used when MSAA is enabled).
    GpuTextureHandle fbDepthMSAA   = 0;  ///< MSAA depth texture (shared by all passes).

    uint32_t width  = 0;                 ///< Framebuffer width in pixels.
    uint32_t height = 0;                 ///< Framebuffer height in pixels.

    std::vector<std::pair<std::string, RenderPass*>> renderpasses; ///< Named render passes drawn into this target, in order.

    bool renderToScreen    = false;      ///< Whether this target is composited onto the swapchain.
    bool noMSAA            = false;      ///< True for effect targets that always render to 1× textures.
    bool additiveBlend     = false;      ///< Use the additive pipeline when compositing onto the swapchain.
    bool fixedSize         = false;      ///< True when width/height were set explicitly (skip canvas-resize).
    bool preComputeFlush   = false;      ///< Run passes before compute dispatches (eliminates 1-frame GI lag).

    TextureAsset textureView;            ///< A TextureAsset view over fbContent for sampling.
};

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
     * @brief Closes the rendering system and releases all GPU resources.
     *
     * Waits for GPU to complete all work, releases all render passes,
     * framebuffers, samplers, shaders, and the GPU device.
     */
    static void Close() { get()._close(); }

    /**
     * @brief Retrieves the SDL GPU device.
     *
     * @return Pointer to the SDL GPU device.
     */
    // Returns the underlying SDL_GPUDevice on the SDL backend; nullptr on WebGPU.
    // Forward-declared SDL_GPUDevice* keeps this signature backend-neutral.
    static SDL_GPUDevice *GetDevice() { return get()._getDevice(); }
    /// @brief Returns true once the GPU backend is initialized and ready to render.
    static bool IsReady() { return get().m_gpu != nullptr; }

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
     * @brief Removes a shader render pass from all framebuffers.
     *
     * Releases the render pass resources and removes it from any framebuffers it was attached to.
     *
     * @param passname Name of the render pass to remove.
     */
    static void RemoveShaderPass(const std::string &passname) {
        get()._removeShaderPass(passname);
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
    static GpuSamplerHandle GetSampler(ScaleMode scalemode) {
        return get()._getSampler(scalemode);
    }

    /**
     * @brief Converts canvas pixel coordinates to logical/render space.
     *
     * On WebGPU builds, applies the inverse of the blit scaling transform so that
     * mouse coordinates (reported in canvas pixels) map to render-space coordinates.
     * On non-WebGPU builds this is a 1:1 pass-through.
     */
    static vf2d CanvasToLogical(float cx, float cy) {
        auto& r = get();
        return { r.m_blitLogicalOffsetX + cx * r.m_blitInvScaleX,
                 r.m_blitLogicalOffsetY + cy * r.m_blitInvScaleY };
    }

    /**
     * @brief Gets the active SDL GPU render pass for a specific render pass.
     *
     * @param passname Name of the render pass.
     * @return Pointer to the SDL GPU render pass, or nullptr if not found.
     */
    static GpuRenderPassHandle GetRenderPass(const std::string &passname) {
        return get()._getRenderPass(passname);
    }

    /**
     * @brief Finds a RenderPass object by name. Used for caching direct pointers.
     *
     * @param passname Name of the render pass.
     * @return Pointer to the RenderPass, or nullptr if not found.
     */
    static RenderPass* FindRenderPass(const std::string& passname) {
        return get()._findRenderPass(passname);
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

    /// @cond INTERNAL
    // Returns true and clears the flag if a deferred reset is pending.
    // Call from Window::_startFrame(), before ImGui::NewFrame(), so _reset()
    // runs before the frame begins (not mid-frame where waitIdle() would abort).
    static bool ConsumePendingReset() {
        auto& r = get();
        if (!r.m_pendingReset) return false;
        r.m_pendingReset = false;
        return true;
    }
    /// @endcond

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
     * @brief Retrieves a rounded rectangle geometry with the specified corner radii and segments.
     *
     * @param cornerRadiusX Normalized radius along X axis (0.0 to 0.5).
     * @param cornerRadiusY Normalized radius along Y axis (0.0 to 0.5).
     * @param cornerSegments Number of segments per corner arc (default 8).
     * @return Pointer to the rounded rectangle geometry.
     */
    static Geometry2D* GetRoundedRectGeometry(float cornerRadiusX, float cornerRadiusY, int cornerSegments = 8) { 
        return get()._getRoundedRectGeometry(cornerRadiusX, cornerRadiusY, cornerSegments); 
    }


    /**
     * @brief Gets the current MSAA sample count setting.
     *
     * @return The current sample count (e.g., SDL_GPU_SAMPLECOUNT_1, SDL_GPU_SAMPLECOUNT_4).
     */
    /**
     * @brief Retrieves the active GPU backend interface.
     */
    static IGpu& GetGpu() { return *get().m_gpu; }

    /**
     * @brief True while the GPU backend is alive. False after Renderer::Close()
     * resets it, so late teardown (e.g. static singleton dtors) can skip GPU calls.
     */
    static bool HasGpu() { return (bool)get().m_gpu; }

    /// @brief Compiles a compute pipeline from a shader file.
    /// @param shaderPath Path to the compute shader source.
    /// @return The created compute pipeline asset (invalid handle on failure).
    static ComputePipelineAsset CreateComputePipelineAsset(const std::string& shaderPath);


    /// @brief Returns the current MSAA sample count of the main render target.
    static GpuSampleCount GetSampleCount() { return get().currentSampleCount; }

    /// @brief Returns the canvas/swapchain width in pixels.
    static uint32_t GetCanvasWidth()  { return get().m_canvasWidth;  }
    /// @brief Returns the canvas/swapchain height in pixels.
    static uint32_t GetCanvasHeight() { return get().m_canvasHeight; }

    /**
     * @brief Publishes the canvas/swapchain dimensions before the first frame is acquired.
     *
     * Lets the GPU backend set the dims as soon as it knows them, so code running between
     * backend init and the first swapchain acquire doesn't see stale window-creation dims.
     *
     * @param w Canvas width in pixels.
     * @param h Canvas height in pixels.
     */
    static void     SetCanvasSize(uint32_t w, uint32_t h) {
        get().m_canvasWidth  = w;
        get().m_canvasHeight = h;
        get()._updateCameraProjection();
    }

    /**
     * @brief Sets the MSAA sample count and recreates render passes.
     *
     * Changes the multi-sample anti-aliasing level. Triggers a full reset
     * of all render passes to apply the new sample count.
     *
     * @param sampleCount The new sample count to use.
     */
    static void SetSampleCount(GpuSampleCount sampleCount) {
        get()._setSampleCount(sampleCount);
    }

    /**
     * @brief Creates a sprite render target with the specified configuration.
     *
     * This is a convenience function that creates a framebuffer, render pass,
     * and configures it with the provided options in a single call.
     *
     * @param name Name of the render target.
     * @param config Configuration options for the render target.
     */
    static void CreateSpriteRenderTarget(const std::string& name, const SpriteRenderTargetConfig& config = {}) {
        get()._createSpriteRenderTarget(name, config);
    }

    /**
     * @brief Removes a sprite render target created with CreateSpriteRenderTarget.
     *
     * Releases the render pass and optionally removes the associated framebuffer.
     *
     * @param name Name of the render target to remove.
     * @param removeFramebuffer If true, also removes the framebuffer (default: true).
     */
    static void RemoveSpriteRenderTarget(const std::string& name, bool removeFramebuffer = true) {
        get()._removeSpriteRenderTarget(name, removeFramebuffer);
    }

    /**
     * @brief Queues a PNG capture of a framebuffer's resolved content. Captured at
     * end of frame (after its passes render) and written asynchronously, like the
     * swapchain screenshot path. Lets callers persist offscreen render targets.
     */
    static void CaptureFramebuffer(const std::string& fbName, const std::string& filename) {
        get().m_pendingFbCaptures.push_back({fbName, filename});
    }

private:
    SDL_GPUDevice        *m_device = nullptr;  // null under WebGPU backend
    GpuCmdBufferHandle    m_cmdbuf = 0;
    std::vector<std::pair<std::string, std::string>> m_pendingFbCaptures;  // (fbName, file)

    std::unique_ptr<IGpu> m_gpu;

    uint32_t _zIndex = 0;

    std::vector<std::pair<std::string, FrameBuffer *>> frameBuffers;

    void
    _addShaderPass(const std::string &passname, const ShaderAsset &vertShader, const ShaderAsset &fragShader, std::vector<std::string> targetBuffers);

    void _removeShaderPass(const std::string &passname);

    void _attachRenderPassToFrameBuffer(RenderPass *renderPass, const std::string &passname, const std::string &fbName);

    void _addToRenderQueue(const std::string &passname, const Renderable &renderable);

    void _initRendering();

    void _close();

    SDL_GPUDevice *_getDevice() { return m_device; }

    void _startFrame() const;

    void _clearBackground(Color color);

    void _endFrame();

    void _reset();

    void _onResize();

    void _updateCameraProjection();

    void _createFrameBuffer(const std::string &fbname, uint32_t width = 0, uint32_t height = 0);
    void _createFrameBuffer(const std::string &fbname, uint32_t width, uint32_t height, GpuTextureFormat format);

    void _setFramebufferRenderToScreen(const std::string &fbName, bool render);

    GpuRenderPassHandle _getRenderPass(const std::string &passname);

    RenderPass* _findRenderPass(const std::string& passname);

    void _setScissorMode(const std::string &passname, const rectf &cliprect);

    TextureAsset _whitePixelTexture;

    Texture _whitePixel();

    Geometry2D* _getQuadGeometry();
    Geometry2D* _getCircleGeometry(int segments);
    Geometry2D* _getRoundedRectGeometry(float cornerRadiusX, float cornerRadiusY, int cornerSegments);

    UniformBuffer &_getUniformBuffer(const std::string &passname);

    TextureAsset _screenBuffer;
    TextureAsset fs;
    GpuShaderHandle rtt_vertex_shader   = 0;
    GpuShaderHandle rtt_fragment_shader = 0;

    void renderFrameBuffer(GpuCmdBufferHandle cmdBuf);

    FrameBuffer *_getFramebuffer(std::string fbname);

    void _setSampleCount(GpuSampleCount sampleCount);
    
    void _createSpriteRenderTarget(const std::string& name, const SpriteRenderTargetConfig& config);
    
    void _removeSpriteRenderTarget(const std::string& name, bool removeFramebuffer);
    

    struct Uniforms {
        glm::mat4 camera;
        glm::mat4 model;
        glm::vec2 flipped;

        // Flat uv0..uv5, not uv[6]: HLSL cbuffer arrays 16-byte-align each element
        // (48 -> 96 bytes), desyncing this CPU struct from the shader. Keep them separate.
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

    GpuSamplerHandle _getSampler(ScaleMode scaleMode);

    std::unordered_map<ScaleMode, GpuSamplerHandle> _samplers;

    GpuSampleCount currentSampleCount = GpuSampleCount::x1;

    GpuGraphicsPipelineHandle m_rendertotexturepipeline          = 0;
    GpuGraphicsPipelineHandle m_rendertotexturepipeline_additive = 0;

    GpuTextureHandle swapchain_texture = 0;
    glm::mat4x4      m_camera          = {};
    uint32_t         m_canvasWidth     = 0;
    uint32_t         m_canvasHeight    = 0;
    bool             m_pendingReset    = false;

    // Inverse blit transform: logical = offset + canvas * invScale
    float m_blitInvScaleX      = 1.0f;
    float m_blitInvScaleY      = 1.0f;
    float m_blitLogicalOffsetX = 0.0f;
    float m_blitLogicalOffsetY = 0.0f;

public:
    /// @cond INTERNAL
    Renderer(const Renderer &) = delete;

    static Renderer &get() {
        static Renderer instance;
        return instance;
    }
    /// @endcond

private:
    Renderer() {
    };
};