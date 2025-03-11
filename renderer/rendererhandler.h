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

#ifdef ADD_IMGUI
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
    SDL_GPUTexture *fbContent = nullptr;

    uint32_t width  = 0;
    uint32_t height = 0;

    std::vector<std::pair<std::string, RenderPass *>> renderpasses;
};

class RenderPass;

/**
 * @brief Provides functionality for managing the application window.
 */
class Renderer {
public:

    static void InitRendering() { get()._initRendering(); }

    static SDL_GPUDevice *GetDevice() { return get()._getDevice(); };

    static void StartFrame() { get()._startFrame(); }

    static void EndFrame() { get()._endFrame(); }

    static void Reset() { get()._reset(); }

    static void ClearBackground(Color color) { get()._clearBackground(color); }

    static void AddToRenderQueue(const std::string &passname, const Renderable &renderable) { get()._addToRenderQueue(passname, renderable); }

    static void AddShaderPass(const std::string &passname, const ShaderAsset &vertShader, const ShaderAsset &fragShader,
                              std::vector<std::string> targetBuffers = std::vector<std::string>()) {
        get()._addShaderPass(passname, vertShader, fragShader, std::move(targetBuffers));
    }

    static void AttachRenderPassToFrameBuffer(RenderPass* renderPass, const std::string &passname, const std::string &fbName) { get()._attachRenderPassToFrameBuffer(renderPass, passname, fbName); }

    static UniformBuffer &GetUniformBuffer(const std::string &passname) { return get()._getUniformBuffer(passname); }

    static void CreateFrameBuffer(const std::string& fbname) { return get()._createFrameBuffer(fbname); }


    static uint32_t GetZIndex() { return get()._zIndex++; }

    static FrameBuffer *GetFramebuffer(std::string fbname) { return get()._getFramebuffer(std::move(fbname)); };

    static SDL_GPUSampler* GetSampler(ScaleMode scalemode) { return get()._getSampler(scalemode); };

    static void OnResize() { get()._onResize(); }

private:
    SDL_GPUDevice        *m_device = nullptr;
    SDL_GPUCommandBuffer *m_cmdbuf = nullptr;

    uint32_t _zIndex = 0;

    std::vector<std::pair<std::string, FrameBuffer *>> frameBuffers;

    void
    _addShaderPass(const std::string &passname, const ShaderAsset &vertShader, const ShaderAsset &fragShader, std::vector<std::string> targetBuffers);

    void _attachRenderPassToFrameBuffer(RenderPass* renderPass, const std::string &passname, const std::string &fbName);

    void _addToRenderQueue(const std::string &passname, const Renderable &renderable);

    void _initRendering();

    void _close();

    SDL_GPUDevice *_getDevice();

    void _startFrame() const;

    void _clearBackground(Color color);

    void _endFrame();

    void _reset();

    void _onResize();

    void _createFrameBuffer(const std::string& fbname);

    UniformBuffer &_getUniformBuffer(const std::string &passname);

    TextureAsset _screenBuffer;
    TextureAsset fs;
    SDL_GPUShader* rtt_vertex_shader;
    SDL_GPUShader* rtt_fragment_shader;
    void renderFrameBuffer(SDL_GPUCommandBuffer *cmd_buffer);

    FrameBuffer *_getFramebuffer(std::string fbname);

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


    SDL_GPUSampler* _getSampler(ScaleMode scaleMode);
    std::unordered_map<ScaleMode, SDL_GPUSampler*> _samplers;

    SDL_GPUGraphicsPipeline *m_rendertotexturepipeline{nullptr};

    SDL_GPUTexture *swapchain_texture = nullptr;
    glm::mat4x4    m_camera = {};

    SDL_GPUColorTargetDescription color_target_descriptions;
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