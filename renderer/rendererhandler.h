#pragma once

#include <memory>
#include <string>
#include <optional>

#include "SDL3/SDL.h"

#include "utils/vectors.h"
#include "utils/lerp.h"
#include "input/inputhandler.h"
#include "eventbus/eventbushandler.h"

#include <chrono>
#include "utils/colors.h"
#include "assettypes/shader.h"
#include "assettypes/texture.h"

#include "renderpass.h"
#include "renderable.h"
#include "utils/uniformobject.h"

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

    static void AddToRenderQueue(const std::string &passname, const Renderable &renderable) { get()._addToRenderQueue(passname, renderable); };


    static void AddShaderPass(const std::string& passname, const ShaderAsset& vertShader, const ShaderAsset& fragShader) { get()._addShaderPass(passname, vertShader, fragShader); }

    static UniformBuffer& GetUniformBuffer(const std::string& passname) { return get()._getUniformBuffer(passname); }

    static uint32_t GetZIndex() { return get()._zIndex--; }

private:
    SDL_GPUDevice         *m_device = nullptr;
    SDL_GPUCommandBuffer  *m_cmdbuf = nullptr;

    uint32_t              _zIndex = INT_MAX;

    std::vector<std::pair<std::string, RenderPass*>> renderpasses;

    void _addShaderPass(const std::string& passname, const ShaderAsset& vertShader, const ShaderAsset& fragShader);

    void _addToRenderQueue(const std::string &passname, const Renderable &renderable);

    void _initRendering();

    void _close();

    SDL_GPUDevice *_getDevice();

    void _startFrame() const;

    void _clearBackground(Color color);

    void _endFrame();

    void _reset();

    UniformBuffer& _getUniformBuffer(const std::string& passname);

    TextureAsset _screenBuffer;

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