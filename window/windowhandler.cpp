#include "windowhandler.h"

#include <stdexcept>
#include "audio/audiohandler.h"

#include "assethandler/assethandler.h"
#include "render2d/render2dhandler.h"

void Window::_initWindow(const std::string &title, int width, int height, int scale, unsigned int flags) {

    if (scale > 1) { //when scaling asume width is virtual pixels instead of real screen pixels
        width *= scale;
        height *= scale;
    }

    SDL_InitSubSystem(SDL_INIT_VIDEO);

    SDL_ShaderCross_Init();

    auto t = SDL_ShaderCross_GetSPIRVShaderFormats();

    m_device = SDL_CreateGPUDevice(t, SDL_TRUE, nullptr);
    if (m_device == nullptr) {
        SDL_Log("GPUCreateDevice failed");
        SDL_Log("%s", SDL_GetError());
        return;
    }

    auto window = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (window) {
        m_window.reset(window);
    } else {
        throw std::runtime_error(SDL_GetError());
    }

    if (!SDL_ClaimWindowForGPUDevice(m_device, m_window.get())) {
        SDL_Log("GPUClaimWindow failed");
        SDL_Log("%s", SDL_GetError());
        return;
    }

    Shader vertexShader = AssetHandler::GetShader("assets/TexturedQuad.vert", 0, 0, 0, 0);

    Shader fragmentShader = AssetHandler::GetShader("assets/TexturedQuad.frag", 1, 0, 0, 0);

    SDL_GPUVertexBufferDescription vertexBufferDescriptions[] = {
        {
            .slot = 0,
            .pitch = sizeof(PositionTextureVertex),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            .instance_step_rate = 0,
        }
    };

    SDL_GPUVertexAttribute vertexAttributes[] = {
        {
            .location = 0,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
            .offset = 0
        },
        {
            .location = 1,
            .buffer_slot = 0,
            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
            .offset = sizeof(float) * 3
        }
    };

    SDL_GPUColorTargetDescription colorTargetDescriptions[] = {
        {
            .format = SDL_GetGPUSwapchainTextureFormat(m_device, m_window.get())
        }
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo = {
        .vertex_shader = vertexShader.shader,
        .fragment_shader = fragmentShader.shader,

        .vertex_input_state = {
            .vertex_buffer_descriptions = vertexBufferDescriptions,
            .num_vertex_buffers = 1,
            .vertex_attributes = vertexAttributes,
            .num_vertex_attributes = 2,
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,

        .target_info = {
            .color_target_descriptions = colorTargetDescriptions,
            .num_color_targets = 1,
        },
    };

    m_pipeline = SDL_CreateGPUGraphicsPipeline(m_device, &pipelineCreateInfo);
    if (m_pipeline == nullptr) {
        SDL_Log("Failed to create pipeline!");
        return;
    }

    SDL_GPUBufferCreateInfo vertexBufferCreateInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = sizeof(PositionTextureVertex) * 4
    };

    _vertexBuffer = SDL_CreateGPUBuffer(m_device, &vertexBufferCreateInfo);

    SDL_GPUBufferCreateInfo indexBufferCreateInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_INDEX,
        .size = sizeof(Uint16) * 6
    };

    _indexBuffer = SDL_CreateGPUBuffer(m_device, &indexBufferCreateInfo);

    SDL_GPUSamplerCreateInfo samplerCreateInfo = {
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .max_anisotropy = 4,
        .enable_anisotropy = SDL_TRUE,
    };

    _sampler = SDL_CreateGPUSampler(m_device, &samplerCreateInfo);

    if (scale > 1) {
        _setScale(scale);
    }
}

void Window::_close() {
    // SDL_QuitSubSystem is ref-counted
    Audio::Close();
    SDL_Quit();
}

double Window::_getRunTime() {
    return (double) SDL_GetTicks() * 1000.0;
}

void Window::_toggleFullscreen() {
    auto *window = m_window.get();

    auto fullscreenFlag = SDL_WINDOW_FULLSCREEN;
    bool isFullscreen   = SDL_GetWindowFlags(window) & fullscreenFlag;

    SDL_SetWindowFullscreen(window, !isFullscreen);
}

SDL_Renderer *Window::_getRenderer() {
    auto *window = m_window.get();
    return SDL_GetRenderer(window);
}

int Window::_getFPS(float milliseconds) {
    auto seconds = milliseconds / 1000.f;

    if (_fpsAccumulator > seconds) {
        _fpsAccumulator -= seconds;

        _fps = (int) (1. / _lastFrameTime);
    }
    return _fps;
}

bool Window::_isFullscreen() {
    auto *window = m_window.get();

    auto flag          = SDL_GetWindowFlags(window);
    auto is_fullscreen = flag & SDL_WINDOW_FULLSCREEN;
    return is_fullscreen == SDL_WINDOW_FULLSCREEN;
}

vf2d Window::_getSize(bool getRealSize) {
    int w, h;

    if (IsFullscreen()) {

        const SDL_DisplayMode *dm;

        int windowX = 10;
        int windowY = 10;

        SDL_GetWindowPosition(Window::GetWindow(), &windowX, &windowY);

        const SDL_Point *point = new const SDL_Point({windowX + 10, windowY + 10});

        dm = SDL_GetCurrentDisplayMode(
            SDL_GetDisplayForPoint(point)
        );

        w = dm->w;
        h = dm->h;
    } else {
        // Get the size of the window's client area
        SDL_GetWindowSize(m_window.get(), &w, &h);
    }

    if (!getRealSize && _scaleFactor > 1) {
        w /= _scaleFactor;
        h /= _scaleFactor;
    }

    return {(float) w, (float) h};
}

void Window::_handleInput() {
    Input::Update();

    SDL_Event event;

    std::vector<Uint8> newKeysDown;
    std::vector<Uint8> newKeysUp;

    while (SDL_PollEvent(&event)) {
#ifdef ADD_IMGUI
        ImGui_ImplSDL3_ProcessEvent(&event);
#endif

        switch (event.type) {
            case SDL_EventType::SDL_EVENT_QUIT:
                _shouldQuit = true;
                break;
            case SDL_EventType::SDL_EVENT_KEY_DOWN:
                newKeysDown.push_back(event.key.scancode);
                break;
            case SDL_EventType::SDL_EVENT_KEY_UP:
                newKeysUp.push_back(event.key.scancode);
                break;
            case SDL_EventType::SDL_EVENT_GAMEPAD_ADDED:
                Input::AddGamepadDevice(event.gdevice.which);
                break;
            case SDL_EventType::SDL_EVENT_GAMEPAD_REMOVED:
                Input::RemoveGamepadDevice(event.gdevice.which);
                break;
            case SDL_EventType::SDL_EVENT_WINDOW_RESIZED:
                EventData eventData;
                eventData.emplace("width", event.window.data1);
                eventData.emplace("height", event.window.data2);
                EventBus::Fire(SystemEvent::WINDOW_RESIZE, eventData);
                break;
        }
    }

    Input::UpdateInputs(newKeysDown, true);
    Input::UpdateInputs(newKeysUp, false);

    if (Input::KeyPressed(SDLK_F11) && Input::KeyDown(SDLK_LSHIFT)) { // && SDL_GetModState() & SDL_KMOD_SHIFT) {
        ToggleDebugMenu();
    }
}

void Window::_setSize(int width, int height) {
    SDL_SetWindowSize(m_window.get(), width, height);
}

void Window::_clearBackground(Color color) {
    SDL_SetRenderDrawColor(GetRenderer(), color.r, color.g, color.b, color.a);
    SDL_RenderClear(GetRenderer());
}

void Window::_startFrame() {
    Lerp::updateLerps();

    Window::HandleInput();
    SDL_SetRenderDrawColor(GetRenderer(), 0, 0, 0, 255);
    SDL_RenderClear(GetRenderer());

    if (_scaleFactor > 1) {
        SDL_SetRenderTarget(GetRenderer(), _screenBuffer.texture);
    }

#ifdef ADD_IMGUI
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
#endif
}

void Window::_endFrame() {

    if (_scaleFactor > 1) {
        SDL_SetRenderTarget(GetRenderer(), nullptr);
        Render2D::DrawTexture(_screenBuffer, {0.f, 0.f},
                              {static_cast<float>(GetSize(true).x), static_cast<float>(GetSize(true).y)}, WHITE);
    }

#ifdef ADD_IMGUI
    if (_debugMenuVisible) {
        Helpers::DrawMainMenu();
    }
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), Window::GetRenderer());
#endif

    SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUCommandBuffer(m_device);
    if (cmdbuf == nullptr) {
        SDL_Log("GPUAcquireCommandBuffer failed");
        return;
    }

    Uint32         w, h;
    SDL_GPUTexture *swapchainTexture = SDL_AcquireGPUSwapchainTexture(cmdbuf, m_window.get(), &w, &h);
    if (swapchainTexture != nullptr) {
        SDL_GPUColorTargetInfo colorTargetInfo = {0};
        colorTargetInfo.texture     = swapchainTexture;
        colorTargetInfo.clear_color = (SDL_FColor) {0.0f, 0.0f, 0.0f, 1.0f};
        colorTargetInfo.load_op     = SDL_GPU_LOADOP_CLEAR;
        colorTargetInfo.store_op    = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(cmdbuf, &colorTargetInfo, 1, nullptr);

        SDL_BindGPUGraphicsPipeline(renderPass, m_pipeline);
        SDL_GPUBufferBinding vertexBufferBinding = {
            .buffer = _vertexBuffer,
            .offset = 0
        };
        SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);

        // Index buffer binding
        SDL_GPUBufferBinding indexBufferBinding = {
            .buffer = _indexBuffer,
            .offset = 0
        };
        SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

        // Texture sampler binding
        SDL_GPUTextureSamplerBinding textureSamplerBinding = {
            .texture = _gpuTexture,
            .sampler = _sampler
        };
        SDL_BindGPUFragmentSamplers(renderPass, 0, &textureSamplerBinding, 1);
        SDL_DrawGPUIndexedPrimitives(renderPass, 6, 1, 0, 0, 0);

        SDL_EndGPURenderPass(renderPass);
    }

    SDL_SubmitGPUCommandBuffer(cmdbuf);

    _frameCount++;
    _previousTime = _currentTime;
    _currentTime  = std::chrono::high_resolution_clock::now();
    _lastFrameTime =
        (double) std::chrono::duration_cast<std::chrono::nanoseconds>(_currentTime - _previousTime).count() /
        1000000000.;
    _fpsAccumulator += _lastFrameTime;
}

SDL_Window *Window::_getWindow() {
    return m_window.get();
}

void Window::_toggleDebugMenu() {
#ifdef ADD_IMGUI
    get()._debugMenuVisible = !get()._debugMenuVisible;
#endif
}

void Window::_setScale(int scalefactor) {
    _scaleFactor = scalefactor;

    if (scalefactor > 1) {
        //        _screenBuffer = AssetHandler::CreateEmptyTexture(Window::GetSize());
    } else {
        //        SDL_SetRenderTarget(GetRenderer(), nullptr);
    }
}

void Window::_setScaledSize(int widthInScaledPixels, int heightInScaledPixels, int scale) {

    if (scale > 0) {
        SetScale(scale);
    }

    SetSize(_scaleFactor * widthInScaledPixels, _scaleFactor * heightInScaledPixels);
}

void Window::_setRenderTarget(Texture target) {
    //    SDL_SetRenderTarget(Window::GetRenderer(), target.texture);
}

void Window::_resetRenderTarget() {
    //    if (_scaleFactor > 1) {
    //        SetRenderTarget(_screenBuffer);
    //    } else {
    //        SDL_SetRenderTarget(Window::GetRenderer(), nullptr);
    //    }
}

float Window::_getScale() {
    return (float) _scaleFactor;
}

#ifdef ADD_IMGUI
void Window::SetupImGuiStyle()
{
    // Bootstrap Dark style by Madam-Herta from ImThemes
    ImGuiStyle& style = ImGui::GetStyle();

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.5f;
    style.WindowPadding = ImVec2(11.69999980926514f, 6.0f);
    style.WindowRounding = 10.0f;
    style.WindowBorderSize = 0.0f;
    style.WindowMinSize = ImVec2(20.0f, 20.0f);
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_Right;
    style.ChildRounding = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupRounding = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FramePadding = ImVec2(20.0f, 9.899999618530273f);
    style.FrameRounding = 5.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.CellPadding = ImVec2(4.0f, 2.0f);
    style.IndentSpacing = 21.0f;
    style.ColumnsMinSpacing = 6.0f;
    style.ScrollbarSize = 14.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabMinSize = 10.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 4.0f;
    style.TabBorderSize = 0.0f;
    style.TabMinWidthForCloseButton = 0.0f;
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.5843137502670288f, 0.5960784554481506f, 0.615686297416687f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.062745101749897f, 0.06666667014360428f, 0.08627451211214066f, 0.7f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 0.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.1098039224743843f, 0.1137254908680916f, 0.1333333402872086f, 1.0f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.1098039224743843f, 0.1137254908680916f, 0.1333333402872086f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.062745101749897f, 0.06666667014360428f, 0.08627451211214066f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.05882352963089943f, 0.529411792755127f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.05882352963089943f, 0.529411792755127f, 0.9764705896377563f, 0.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.062745101749897f, 0.06666667014360428f, 0.08627451211214066f, 0.7f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 0.7f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 0.7f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 0.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 0.0f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.05882352963089943f, 0.529411792755127f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.1450980454683304f, 0.1490196138620377f, 0.1843137294054031f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.4862745106220245f, 0.4862745106220245f, 0.4862745106220245f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 1.0f, 1.0f, 0.2274678349494934f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.8196078538894653f, 0.8196078538894653f, 0.8196078538894653f, 0.3304721117019653f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.2274509817361832f, 0.4431372582912445f, 0.7568627595901489f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.2078431397676468f, 0.4705882370471954f, 0.8509804010391235f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.05882352963089943f, 0.529411792755127f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.3100000023841858f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 0.6200000047683716f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.1372549086809158f, 0.4392156898975372f, 0.800000011920929f, 0.7799999713897705f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.1372549086809158f, 0.4392156898975372f, 0.800000011920929f, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.3490196168422699f, 0.3490196168422699f, 0.3490196168422699f, 0.1700000017881393f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.949999988079071f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.0f, 0.4745098054409027f, 1.0f, 0.9309999942779541f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.2078431397676468f, 0.2078431397676468f, 0.2078431397676468f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.9176470637321472f, 0.9254902005195618f, 0.9333333373069763f, 0.9861999750137329f);
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.7411764860153198f, 0.8196078538894653f, 0.9137254953384399f, 1.0f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 1.0f);
    style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.4274509847164154f, 0.3490196168422699f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8980392217636108f, 0.6980392336845398f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.4470588266849518f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.7764706015586853f, 0.8666666746139526f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.5686274766921997f, 0.5686274766921997f, 0.6392157077789307f, 1.0f);
    style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.6784313917160034f, 0.6784313917160034f, 0.7372549176216125f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.2980392277240753f, 0.2980392277240753f, 0.2980392277240753f, 0.09000000357627869f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.3499999940395355f);
    style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.949999988079071f);
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.6980392336845398f, 0.6980392336845398f, 0.6980392336845398f, 0.699999988079071f);
    style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f, 0.3499999940395355f);
}

#endif

void Window::_setIcon(Texture icon) {

    if (icon.surface)
        SDL_SetWindowIcon(_getWindow(), icon.surface);
}

SDL_GPUDevice *Window::_getDevice() {
    return m_device;
}

