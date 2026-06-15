#ifdef LUMINOVEAU_WITH_IMGUI

#include "imgui_integration.h"
#include "imgui_backend.h"  // ImGuiBackend::{InitRenderer,Shutdown,NewFrame,RenderFrame}

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"

#include "SDL3/SDL.h"
#include "assets/assethandler.h"
#include "core/state/state.h"
#include "core/log/log.h"
#include "renderer/renderer.h"

#include <map>
#include <algorithm>

namespace {

struct GamepadTest {
    bool North = false, South = false, East = false, West = false;
    bool DPadUp = false, DPadDown = false, DPadLeft = false, DPadRight = false;
    bool Back = false, Start = false, Guide = false;
    bool LeftStickClick = false, RightStickClick = false;
    bool LeftShoulder = false, RightShoulder = false;
    float LeftStickX = 0, LeftStickY = 0;
    float RightStickX = 0, RightStickY = 0;
    float LeftTrigger = 0, RightTrigger = 0;
    float rumbleLeft = 0, rumbleRight = 0;
    float rumbleTriggerLeft = 0, rumbleTriggerRight = 0;
};

typedef struct {
    Uint8 ucEnableBits1;
    Uint8 ucEnableBits2;
    Uint8 ucRumbleRight;
    Uint8 ucRumbleLeft;
    Uint8 ucHeadphoneVolume;
    Uint8 ucSpeakerVolume;
    Uint8 ucMicrophoneVolume;
    Uint8 ucAudioEnableBits;
    Uint8 ucMicLightMode;
    Uint8 ucAudioMuteBits;
    Uint8 rgucRightTriggerEffect[11];
    Uint8 rgucLeftTriggerEffect[11];
    Uint8 rgucUnknown1[6];
    Uint8 ucLedFlags;
    Uint8 rgucUnknown2[2];
    Uint8 ucLedAnim;
    Uint8 ucLedBrightness;
    Uint8 ucPadLights;
    Uint8 ucLedRed;
    Uint8 ucLedGreen;
    Uint8 ucLedBlue;
} DS5EffectsState_t;

bool texturesVisible = false;
bool audioVisible    = false;
bool inputVisible    = false;
bool demoVisible     = false;

SDL_Window* g_imguiWindow = nullptr;

void SetupStyle() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
#ifdef LUMINOVEAU_WITH_IMGUI_DOCKING
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif

    ImGuiStyle& style = ImGui::GetStyle();

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

} // anonymous namespace

namespace ImGuiIntegration {

void Init(SDL_Window* window) {
    g_imguiWindow = window;
    ImGui::CreateContext();
    SetupStyle();
}

void InitRenderer(SDL_Window* window) {
    ImGuiBackend::InitRenderer(window);
}

void Shutdown() {
    ImGuiBackend::Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void ProcessEvent(SDL_Event* event) {
#ifdef __EMSCRIPTEN__
    // SDL mouse events are in SDL window space (e.g. 1280×720). ImGui needs canvas/
    // swapchain space — scale mouse coords before ImGui sees them. Browser only.
    const uint32_t cw = Renderer::GetCanvasWidth();
    const uint32_t ch = Renderer::GetCanvasHeight();
    int sdlW = 0, sdlH = 0;
    SDL_GetWindowSize(g_imguiWindow, &sdlW, &sdlH);
    if (cw > 0 && ch > 0 && sdlW > 0 && sdlH > 0 &&
        (cw != (uint32_t)sdlW || ch != (uint32_t)sdlH)) {
        const float sx = (float)cw / (float)sdlW;
        const float sy = (float)ch / (float)sdlH;
        SDL_Event scaled = *event;
        switch (event->type) {
            case SDL_EVENT_MOUSE_MOTION:
                scaled.motion.x *= sx; scaled.motion.y *= sy;
                scaled.motion.xrel *= sx; scaled.motion.yrel *= sy;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                scaled.button.x *= sx; scaled.button.y *= sy;
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                // wheel coords are not positional, no scaling needed
                break;
            default: break;
        }
        ImGui_ImplSDL3_ProcessEvent(&scaled);
        return;
    }
#endif
    ImGui_ImplSDL3_ProcessEvent(event);
}

void NewFrame() {
    ImGui_ImplSDL3_NewFrame();
#ifdef __EMSCRIPTEN__
    {
        ImGuiIO& io = ImGui::GetIO();
        const uint32_t cw = Renderer::GetCanvasWidth();
        const uint32_t ch = Renderer::GetCanvasHeight();
        if (cw > 0 && ch > 0) {
            io.DisplaySize = ImVec2((float)cw, (float)ch);
        }
    }
#endif
    ImGuiBackend::NewFrame();
    ImGui::NewFrame();
    // Docking is enabled via the flag in SetupStyle; the application owns the
    // dockspace + layout (so it can lay panels out / save arrangements).
}

void RenderFrame(GpuCmdBufferHandle cmd, GpuTextureHandle swapchain) {
    ImGui::Render();
    ImGuiBackend::RenderFrame(cmd, swapchain);
}

void EndFrame() {
    ImGui::EndFrame();
}

void DrawDebugMenu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) {
                State::SetState("quit");
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug")) {
            if (ImGui::MenuItem("Textures"))      texturesVisible = !texturesVisible;
            if (ImGui::MenuItem("Audio chunks"))  audioVisible    = !audioVisible;
            if (ImGui::MenuItem("Input devices")) inputVisible    = !inputVisible;
            if (ImGui::MenuItem("ImGui Demo"))    demoVisible     = !demoVisible;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (texturesVisible) {
        ImGui::SetNextWindowSizeConstraints({200, 200}, {FLT_MAX, FLT_MAX});
        ImGui::Begin("Textures", &texturesVisible);
        ImGui::BeginChild("Loaded textures");
        for (auto& texture : AssetHandler::GetTextures()) {
            ImGui::Text("%s", texture.first.c_str());
        }
        ImGui::EndChild();
        ImGui::End();
    }

    if (inputVisible) {
        ImGui::SetNextWindowSizeConstraints({200, 200}, {FLT_MAX, FLT_MAX});
        ImGui::Begin("Gamepads", &inputVisible);

        int numJoysticks;
        const SDL_JoystickID* joysticks = SDL_GetGamepads(&numJoysticks);

        for (int i = 0; i < numJoysticks; ++i) {
            int offSetX = 40;
            int offSetY = 70;

            SDL_Gamepad* gamepad = SDL_OpenGamepad(joysticks[i]);
            ImGui::BeginChild(SDL_GetGamepadName(gamepad));
            ImGui::Text("Gamepad %d: %s", i + 1, SDL_GetGamepadName(gamepad));

            GamepadTest d;
            d.DPadUp    = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
            d.DPadDown  = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            d.DPadLeft  = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            d.DPadRight = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            d.North     = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH);
            d.South     = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
            d.East      = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST);
            d.West      = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST);
            d.LeftShoulder  = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
            d.RightShoulder = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
            d.Start  = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START);
            d.Back   = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK);
            d.Guide  = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_GUIDE);
            d.LeftStickX  = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX)  / 32768.f;
            d.LeftStickY  = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY)  / 32768.f;
            d.RightStickX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32768.f;
            d.RightStickY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32768.f;
            d.LeftTrigger  = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)  / 32768.f;
            d.RightTrigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32768.f;

            std::map<SDL_GamepadButton, std::string> buttonNames;
            switch (SDL_GetGamepadType(gamepad)) {
                case SDL_GAMEPAD_TYPE_XBOXONE:
                    buttonNames[SDL_GAMEPAD_BUTTON_NORTH] = "Y"; buttonNames[SDL_GAMEPAD_BUTTON_SOUTH] = "A";
                    buttonNames[SDL_GAMEPAD_BUTTON_EAST]  = "B"; buttonNames[SDL_GAMEPAD_BUTTON_WEST]  = "X";
                    break;
                case SDL_GAMEPAD_TYPE_PS3: case SDL_GAMEPAD_TYPE_PS4: case SDL_GAMEPAD_TYPE_PS5:
                    buttonNames[SDL_GAMEPAD_BUTTON_NORTH] = "Triangle"; buttonNames[SDL_GAMEPAD_BUTTON_SOUTH] = "X";
                    buttonNames[SDL_GAMEPAD_BUTTON_EAST]  = "Circle";   buttonNames[SDL_GAMEPAD_BUTTON_WEST]  = "Square";
                    break;
                case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
                    buttonNames[SDL_GAMEPAD_BUTTON_NORTH] = "X"; buttonNames[SDL_GAMEPAD_BUTTON_SOUTH] = "B";
                    buttonNames[SDL_GAMEPAD_BUTTON_EAST]  = "A"; buttonNames[SDL_GAMEPAD_BUTTON_WEST]  = "Y";
                    break;
                default:
                    buttonNames[SDL_GAMEPAD_BUTTON_NORTH] = "2"; buttonNames[SDL_GAMEPAD_BUTTON_SOUTH] = "0";
                    buttonNames[SDL_GAMEPAD_BUTTON_EAST]  = "1"; buttonNames[SDL_GAMEPAD_BUTTON_WEST]  = "3";
                    break;
            }

            ImGui::SetCursorPos(ImVec2(offSetX + 20,  offSetY + 70));  ImGui::Checkbox("Left",  &d.DPadLeft);
            ImGui::SetCursorPos(ImVec2(offSetX + 70,  offSetY + 120)); ImGui::Checkbox("Down",  &d.DPadDown);
            ImGui::SetCursorPos(ImVec2(offSetX + 70,  offSetY + 20));  ImGui::Checkbox("Up",    &d.DPadUp);
            ImGui::SetCursorPos(ImVec2(offSetX + 120, offSetY + 70));  ImGui::Checkbox("Right", &d.DPadRight);

            ImGui::SetCursorPos(ImVec2(offSetX + 60,  offSetY + 200)); ImGui::SetNextItemWidth(150.0f);
            ImGui::SliderFloat(" ",  &d.LeftStickX, -1.0f, 1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 30,  offSetY + 230));
            ImGui::VSliderFloat("  ", {30, 150}, &d.LeftStickY, 1.0f, -1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 70,  offSetY + 240)); ImGui::Text("Left joystick");
            ImGui::SetCursorPos(ImVec2(offSetX + 70,  offSetY + 260)); ImGui::Text("X: %.3f", d.LeftStickX);
            ImGui::SetCursorPos(ImVec2(offSetX + 70,  offSetY + 280)); ImGui::Text("Y: %.3f", d.LeftStickY);

            ImGui::SetCursorPos(ImVec2(offSetX + 330, offSetY + 200)); ImGui::SetNextItemWidth(150.0f);
            ImGui::SliderFloat(" ",  &d.RightStickX, -1.0f, 1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 300, offSetY + 230));
            ImGui::VSliderFloat("  ", {30, 150}, &d.RightStickY, 1.0f, -1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 340, offSetY + 240)); ImGui::Text("Right joystick");
            ImGui::SetCursorPos(ImVec2(offSetX + 340, offSetY + 260)); ImGui::Text("X: %.3f", d.RightStickX);
            ImGui::SetCursorPos(ImVec2(offSetX + 340, offSetY + 280)); ImGui::Text("Y: %.3f", d.RightStickY);

            ImGui::SetCursorPos(ImVec2(offSetX - 30,  offSetY - 40));
            ImGui::VSliderFloat("      ", {30, 150}, &d.LeftTrigger, 0.0f, 1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 10,  offSetY - 40)); ImGui::Checkbox("Left shoulder", &d.LeftShoulder);

            ImGui::SetCursorPos(ImVec2(offSetX + 470, offSetY - 40));
            ImGui::VSliderFloat("        ", {30, 150}, &d.RightTrigger, 0.0f, 1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 430, offSetY - 40)); ImGui::Checkbox("    ", &d.RightShoulder);
            ImGui::SetCursorPos(ImVec2(offSetX + 325, offSetY - 30)); ImGui::Text("Right shoulder");

            ImGui::SetCursorPos(ImVec2(offSetX + 370, offSetY + 20));  ImGui::Checkbox(buttonNames[SDL_GAMEPAD_BUTTON_NORTH].c_str(), &d.North);
            ImGui::SetCursorPos(ImVec2(offSetX + 420, offSetY + 70));  ImGui::Checkbox(buttonNames[SDL_GAMEPAD_BUTTON_EAST].c_str(),  &d.East);
            ImGui::SetCursorPos(ImVec2(offSetX + 320, offSetY + 70));  ImGui::Checkbox(buttonNames[SDL_GAMEPAD_BUTTON_WEST].c_str(),  &d.West);
            ImGui::SetCursorPos(ImVec2(offSetX + 370, offSetY + 120)); ImGui::Checkbox(buttonNames[SDL_GAMEPAD_BUTTON_SOUTH].c_str(), &d.South);
            ImGui::SetCursorPos(ImVec2(offSetX + 150, offSetY + 150)); ImGui::Checkbox("Back",  &d.Back);
            ImGui::SetCursorPos(ImVec2(offSetX + 300, offSetY + 150)); ImGui::Checkbox("Start", &d.Start);
            ImGui::SetCursorPos(ImVec2(offSetX + 225, offSetY + 120)); ImGui::Checkbox("Guide", &d.Guide);

            if (SDL_GetGamepadType(gamepad) != SDL_GAMEPAD_TYPE_PS5) {
                SDL_PropertiesID props = SDL_GetGamepadProperties(gamepad);
                if (SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)) {
                    ImGui::SetCursorPos(ImVec2(offSetX + 550, offSetY - 40));
                    ImGui::VSliderFloat("      ", {30, 150}, &d.rumbleLeft,  0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SetCursorPos(ImVec2(offSetX + 600, offSetY - 40));
                    ImGui::VSliderFloat("        ", {30, 150}, &d.rumbleRight, 0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);
                    float rl = std::clamp(d.rumbleLeft  * 65535.f, 0.f, 65535.f);
                    float rr = std::clamp(d.rumbleRight * 65535.f, 0.f, 65535.f);
                    SDL_RumbleGamepad(gamepad, (Uint16)rl, (Uint16)rr, 100);
                }
                if (SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false)) {
                    ImGui::SetCursorPos(ImVec2(offSetX + 550, offSetY + 200));
                    ImGui::VSliderFloat("          ", {30, 150}, &d.rumbleTriggerLeft,  0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::SetCursorPos(ImVec2(offSetX + 600, offSetY + 200));
                    ImGui::VSliderFloat("             ", {30, 150}, &d.rumbleTriggerRight, 0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);
                    float rtl = std::clamp(d.rumbleTriggerLeft  * 65535.f, 0.f, 65535.f);
                    float rtr = std::clamp(d.rumbleTriggerRight * 65535.f, 0.f, 65535.f);
                    SDL_RumbleGamepadTriggers(gamepad, (Uint16)rtl, (Uint16)rtr, 100);
                }
            } else {
                DS5EffectsState_t state;
                Uint8 effects[4][11] = {
                    {0x05, 0,   0,   0,   0, 0, 0, 0, 0, 0, 0},
                    {0x21, 255, 110, 0,   0, 0, 0, 0, 0, 0, 0},
                    {0x26, 15,  63,  128, 0, 0, 0, 0, 0, 0, 0},
                    {0x25, 15,  63,  128, 0, 0, 0, 0, 0, 0, 0},
                };
                SDL_zero(state);
                state.ucEnableBits1 |= (0x04 | 0x08);

                ImGui::SetCursorPos(ImVec2(offSetX + 550, offSetY - 40));
                ImGui::VSliderFloat("      ", {30, 150}, &d.rumbleLeft,  0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SetCursorPos(ImVec2(offSetX + 600, offSetY - 40));
                ImGui::VSliderFloat("        ", {30, 150}, &d.rumbleRight, 0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);
                float rl = std::clamp(d.rumbleLeft  * 65535.f, 0.f, 65535.f);
                float rr = std::clamp(d.rumbleRight * 65535.f, 0.f, 65535.f);
                state.ucRumbleLeft  = (int)rl / 256;
                state.ucRumbleRight = (int)rr / 256;
                SDL_RumbleGamepad(gamepad, (Uint16)rl, (Uint16)rr, 100);
                SDL_memcpy(state.rgucRightTriggerEffect, effects[0], sizeof(effects[0]));
                SDL_memcpy(state.rgucLeftTriggerEffect,  effects[0], sizeof(effects[0]));
            }

            ImGui::EndChild();
        }
        ImGui::End();
    }

    if (demoVisible) {
        ImGui::ShowDemoWindow(&demoVisible);
    }
}

} // namespace ImGuiIntegration

#endif
