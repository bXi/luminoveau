#include "windowhandler.h"

#include <stdexcept>
#include "audio/audiohandler.h"

#include "assethandler/assethandler.h"

#include "utils/helpers.h"

#include "renderer/rendererhandler.h"

void Window::_initWindow(const std::string &title, int width, int height, int scale, unsigned int flags) {

    if (scale > 1) { //when scaling asume width is virtual pixels instead of real screen pixels
        //TODO: fix scaling
    }

    if (!AssetHandler::InitPhysFS()) {
        throw std::runtime_error(Helpers::TextFormat("%s AssetHandler::InitPhysFS failed. %s", CURRENT_METHOD()));
    }

    _lastWindowWidth = width;
    _lastWindowHeight = height;

    SDL_Init(SDL_INIT_VIDEO);

    auto window = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (window) {
        m_window = window;
    } else {
        throw std::runtime_error(SDL_GetError());
    }

#ifdef ADD_IMGUI
    ImGui::CreateContext();
    SetupImGuiStyle();
#endif

    Renderer::InitRendering();


    if (scale > 1) {
        //TODO: fix scaling
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
    bool isFullscreen = _isFullscreen();

    if (!isFullscreen) {
        _maximized = true;
        _lastWindowWidth  = (int) _getSize().x;
        _lastWindowHeight = (int) _getSize().y;

        SDL_SetWindowFullscreen(m_window, true);
        SDL_SyncWindow(m_window);

        _setSize((int) _getSize().x, (int) _getSize().y);
    } else {

        SDL_SetWindowFullscreen(m_window, false);
        SDL_SyncWindow(m_window);
        _setSize(_lastWindowWidth, _lastWindowHeight);
    }
}

int Window::_getFPS(float milliseconds) {
    auto seconds = milliseconds / 1000.f;

    if (EngineState::_fpsAccumulator > seconds) {
        EngineState::_fpsAccumulator -= seconds;

        EngineState::_fps = (int) (1. / EngineState::_lastFrameTime);
    }
    return EngineState::_fps;
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
                EngineState::_shouldQuit = true;
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
            case SDL_EventType::SDL_EVENT_WINDOW_RESIZED: {
                EventData resizeEventData;
                resizeEventData.emplace("width", event.window.data1);
                resizeEventData.emplace("height", event.window.data2);
                EventBus::Fire(SystemEvent::WINDOW_RESIZE, resizeEventData);
                _setSize(event.window.data1, event.window.data2);

                if (!_maximized) {
                    _lastWindowWidth = event.window.data1;
                    _lastWindowHeight = event.window.data2;
                }

                break;
            }
            case SDL_EventType::SDL_EVENT_WINDOW_MAXIMIZED: {
                _maximized = true;
                break;
            }
            case SDL_EventType::SDL_EVENT_WINDOW_RESTORED: {
                _maximized = false;
                EventData restoreEventData;
                restoreEventData.emplace("width", _lastWindowWidth);
                restoreEventData.emplace("height", _lastWindowHeight);
                _setSize(_lastWindowWidth, _lastWindowHeight);
                EventBus::Fire(SystemEvent::WINDOW_RESIZE, restoreEventData);
                break;
            }
        }
    }

    Input::UpdateInputs(newKeysDown, true);
    Input::UpdateInputs(newKeysUp, false);
    #ifdef ADD_IMGUI
    if (Input::KeyPressed(SDLK_F11) && Input::KeyDown(SDLK_LSHIFT)) { // && SDL_GetModState() & SDL_KMOD_SHIFT) {
        EngineState::_debugMenuVisible = !EngineState::_debugMenuVisible;
    }
    #endif
}

bool Window::_isFullscreen() {
    auto flag          = SDL_GetWindowFlags(m_window);
    auto is_fullscreen = flag & SDL_WINDOW_FULLSCREEN;
    return is_fullscreen == SDL_WINDOW_FULLSCREEN;
}

vf2d Window::_getSize(bool getRealSize) {
    int w, h;

    if (_isFullscreen()) {

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
        SDL_GetWindowSize(m_window, &w, &h);
    }

    if (!getRealSize && EngineState::_scaleFactor > 1) {
        w /= EngineState::_scaleFactor;
        h /= EngineState::_scaleFactor;
    }

    return {(float) w, (float) h};
}

void Window::_setSize(int width, int height) {
    SDL_SetWindowSize(m_window, width, height);
    //SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SyncWindow(m_window);
    _sizeDirty = true;
    Renderer::OnResize();
}

void Window::_startFrame() const {
    Lerp::updateLerps();

    Window::HandleInput();

    if (EngineState::_scaleFactor > 1) {
        //TODO: fix scaling
    }

    Renderer::StartFrame();
}

void Window::_endFrame() {

    if (EngineState::_scaleFactor > 1) {
        //TODO: fix scaling
    }

#ifdef ADD_IMGUI
    if (EngineState::_debugMenuVisible) {
        Helpers::DrawMainMenu();
    }
#endif

    Renderer::EndFrame();


    if (_sizeDirty) {
        Renderer::Reset();

        _sizeDirty = false;
    }

    EngineState::_frameCount++;
    EngineState::_previousTime = EngineState::_currentTime;
    EngineState::_currentTime  = std::chrono::high_resolution_clock::now();
    EngineState::_lastFrameTime =
        (double) std::chrono::duration_cast<std::chrono::nanoseconds>(EngineState::_currentTime - EngineState::_previousTime).count() /
        1000000000.;
    EngineState::_fpsAccumulator += EngineState::_lastFrameTime;
}

SDL_Window *Window::_getWindow() {
    return m_window;
}

void Window::_toggleDebugMenu() {
#ifdef ADD_IMGUI
    EngineState::_debugMenuVisible = !EngineState::_debugMenuVisible;
#endif
}

void Window::_setScale(int scalefactor) {
    EngineState::_scaleFactor = scalefactor;

    if (scalefactor > 1) {
        //TODO: fix scaling

        //        _screenBuffer = AssetHandler::CreateEmptyTexture(Window::GetSize());
    } else {
        //        SDL_SetRenderTarget(GetRenderer(), nullptr);
    }
}

void Window::_setScaledSize(int widthInScaledPixels, int heightInScaledPixels, int scale) {

    if (scale > 0) {
        SetScale(scale);
    }

    _setSize(EngineState::_scaleFactor * widthInScaledPixels, EngineState::_scaleFactor * heightInScaledPixels);
}

float Window::_getScale() {
    return (float) EngineState::_scaleFactor;
}

#ifdef ADD_IMGUI

void Window::SetupImGuiStyle() {
    // Bootstrap Dark style by Madam-Herta from ImThemes
    ImGuiStyle &style = ImGui::GetStyle();

    style.Alpha                     = 1.0f;
    style.DisabledAlpha             = 0.5f;
    style.WindowPadding             = ImVec2(11.69999980926514f, 6.0f);
    style.WindowRounding            = 10.0f;
    style.WindowBorderSize          = 0.0f;
    style.WindowMinSize             = ImVec2(20.0f, 20.0f);
    style.WindowTitleAlign          = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition  = ImGuiDir_Right;
    style.ChildRounding             = 0.0f;
    style.ChildBorderSize           = 1.0f;
    style.PopupRounding             = 0.0f;
    style.PopupBorderSize           = 1.0f;
    style.FramePadding              = ImVec2(20.0f, 9.899999618530273f);
    style.FrameRounding             = 5.0f;
    style.FrameBorderSize           = 0.0f;
    style.ItemSpacing               = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing          = ImVec2(4.0f, 4.0f);
    style.CellPadding               = ImVec2(4.0f, 2.0f);
    style.IndentSpacing             = 21.0f;
    style.ColumnsMinSpacing         = 6.0f;
    style.ScrollbarSize             = 14.0f;
    style.ScrollbarRounding         = 9.0f;
    style.GrabMinSize               = 10.0f;
    style.GrabRounding              = 0.0f;
    style.TabRounding               = 4.0f;
    style.TabBorderSize             = 0.0f;
    style.TabMinWidthForCloseButton = 0.0f;
    style.ColorButtonPosition       = ImGuiDir_Right;
    style.ButtonTextAlign           = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign       = ImVec2(0.0f, 0.0f);

    style.Colors[ImGuiCol_Text]                  = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.5843137502670288f, 0.5960784554481506f, 0.615686297416687f, 1.0f);
    style.Colors[ImGuiCol_WindowBg]              = ImVec4(0.062745101749897f, 0.06666667014360428f, 0.08627451211214066f, 0.7f);
    style.Colors[ImGuiCol_ChildBg]               = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 0.0f);
    style.Colors[ImGuiCol_PopupBg]               = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 1.0f);
    style.Colors[ImGuiCol_Border]                = ImVec4(0.1098039224743843f, 0.1137254908680916f, 0.1333333402872086f, 1.0f);
    style.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.1098039224743843f, 0.1137254908680916f, 0.1333333402872086f, 1.0f);
    style.Colors[ImGuiCol_FrameBg]               = ImVec4(0.062745101749897f, 0.06666667014360428f, 0.08627451211214066f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.05882352963089943f, 0.529411792755127f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.05882352963089943f, 0.529411792755127f, 0.9764705896377563f, 0.0f);
    style.Colors[ImGuiCol_TitleBg]               = ImVec4(0.062745101749897f, 0.06666667014360428f, 0.08627451211214066f, 0.7f);
    style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 0.7f);
    style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 0.7f);
    style.Colors[ImGuiCol_MenuBarBg]             = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 0.0f);
    style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.04313725605607033f, 0.0470588244497776f, 0.05882352963089943f, 0.0f);
    style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.05882352963089943f, 0.529411792755127f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.1450980454683304f, 0.1490196138620377f, 0.1843137294054031f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.4862745106220245f, 0.4862745106220245f, 0.4862745106220245f, 1.0f);
    style.Colors[ImGuiCol_CheckMark]             = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab]            = ImVec4(1.0f, 1.0f, 1.0f, 0.2274678349494934f);
    style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.8196078538894653f, 0.8196078538894653f, 0.8196078538894653f, 0.3304721117019653f);
    style.Colors[ImGuiCol_Button]                = ImVec4(0.2274509817361832f, 0.4431372582912445f, 0.7568627595901489f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.2078431397676468f, 0.4705882370471954f, 0.8509804010391235f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive]          = ImVec4(0.05882352963089943f, 0.529411792755127f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_Header]                = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.3100000023841858f);
    style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
    style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_Separator]             = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 0.6200000047683716f);
    style.Colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.1372549086809158f, 0.4392156898975372f, 0.800000011920929f, 0.7799999713897705f);
    style.Colors[ImGuiCol_SeparatorActive]       = ImVec4(0.1372549086809158f, 0.4392156898975372f, 0.800000011920929f, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(0.3490196168422699f, 0.3490196168422699f, 0.3490196168422699f, 0.1700000017881393f);
    style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.949999988079071f);
    style.Colors[ImGuiCol_Tab]                   = ImVec4(0.0f, 0.4745098054409027f, 1.0f, 0.9309999942779541f);
    style.Colors[ImGuiCol_TabHovered]            = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
    style.Colors[ImGuiCol_TabActive]             = ImVec4(0.2078431397676468f, 0.2078431397676468f, 0.2078431397676468f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocused]          = ImVec4(0.9176470637321472f, 0.9254902005195618f, 0.9333333373069763f, 0.9861999750137329f);
    style.Colors[ImGuiCol_TabUnfocusedActive]    = ImVec4(0.7411764860153198f, 0.8196078538894653f, 0.9137254953384399f, 1.0f);
    style.Colors[ImGuiCol_PlotLines]             = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3882353007793427f, 1.0f);
    style.Colors[ImGuiCol_PlotLinesHovered]      = ImVec4(1.0f, 0.4274509847164154f, 0.3490196168422699f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram]         = ImVec4(0.8980392217636108f, 0.6980392336845398f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(1.0f, 0.4470588266849518f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.7764706015586853f, 0.8666666746139526f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_TableBorderStrong]     = ImVec4(0.5686274766921997f, 0.5686274766921997f, 0.6392157077789307f, 1.0f);
    style.Colors[ImGuiCol_TableBorderLight]      = ImVec4(0.6784313917160034f, 0.6784313917160034f, 0.7372549176216125f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_TableRowBgAlt]         = ImVec4(0.2980392277240753f, 0.2980392277240753f, 0.2980392277240753f, 0.09000000357627869f);
    style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.3499999940395355f);
    style.Colors[ImGuiCol_DragDropTarget]        = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.949999988079071f);
    style.Colors[ImGuiCol_NavHighlight]          = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.800000011920929f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.6980392336845398f, 0.6980392336845398f, 0.6980392336845398f, 0.699999988079071f);
    style.Colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f);
    style.Colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.2000000029802322f, 0.2000000029802322f, 0.2000000029802322f, 0.3499999940395355f);
}

#endif

void Window::_setIcon(Texture icon) {
    if (icon.surface)
        SDL_SetWindowIcon(_getWindow(), icon.surface);
}

void Window::_setTitle(const std::string &title) {
    SDL_SetWindowTitle(_getWindow(), title.c_str());
}

