#include "helpers.h"

#include "state/state.h"

#include "assethandler/assethandler.h"

#include "SDL3/SDL.h"

bool Helpers::imguiTexturesVisible = false;
bool Helpers::imguiAudioVisible = false;
bool Helpers::imguiInputVisible = false;
bool Helpers::imguiDemoVisible = false;

struct GamepadTest {
    bool North = false;
    bool South = false;
    bool East = false;
    bool West = false;

    bool DPadUp = false;
    bool DPadDown = false;
    bool DPadLeft = false;
    bool DPadRight = false;

    bool Back = false;
    bool Start = false;
    bool Guide = false;


    bool LeftStickClick = false; //L3
    bool RightStickClick = false; //R3
    bool LeftShoulder = false; //L1
    bool RightShoulder = false; //R1

    // Axis values
    float LeftStickX = 0.0f;
    float LeftStickY = 0.0f;
    float RightStickX = 0.0f;
    float RightStickY = 0.0f;
    float LeftTrigger = 0.0f;
    float RightTrigger = 0.0f;

    float rumbleLeft = 0.0f;
    float rumbleRight = 0.0f;

    float rumbleTriggerLeft = 0.0f;
    float rumbleTriggerRight = 0.0f;


};

typedef struct {
    Uint8 ucEnableBits1;              /* 0 */
    Uint8 ucEnableBits2;              /* 1 */
    Uint8 ucRumbleRight;              /* 2 */
    Uint8 ucRumbleLeft;               /* 3 */
    Uint8 ucHeadphoneVolume;          /* 4 */
    Uint8 ucSpeakerVolume;            /* 5 */
    Uint8 ucMicrophoneVolume;         /* 6 */
    Uint8 ucAudioEnableBits;          /* 7 */
    Uint8 ucMicLightMode;             /* 8 */
    Uint8 ucAudioMuteBits;            /* 9 */
    Uint8 rgucRightTriggerEffect[11]; /* 10 */
    Uint8 rgucLeftTriggerEffect[11];  /* 21 */
    Uint8 rgucUnknown1[6];            /* 32 */
    Uint8 ucLedFlags;                 /* 38 */
    Uint8 rgucUnknown2[2];            /* 39 */
    Uint8 ucLedAnim;                  /* 41 */
    Uint8 ucLedBrightness;            /* 42 */
    Uint8 ucPadLights;                /* 43 */
    Uint8 ucLedRed;                   /* 44 */
    Uint8 ucLedGreen;                 /* 45 */
    Uint8 ucLedBlue;                  /* 46 */
} DS5EffectsState_t;

int Helpers::clamp(const int input, const int min, const int max) {
    const int a = (input < min) ? min : input;
    return (a > max ? max : a);
}

float Helpers::mapValues(const float x, const float in_min, const float in_max, const float out_min, const float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float Helpers::getDifficultyModifier(float mod) {
    return 1.0f + ((mod / 10.0f) * (mod / 10.0f) / 1.9f);
}

bool Helpers::lineIntersectsRectangle(vf2d lineStart, vf2d lineEnd, rectf rect) {

    auto doIntersect = [](vf2d p1, vf2d q1, vf2d p2, vf2d q2) -> bool {
        auto orientation = [](vf2d p, vf2d q, vf2d r) -> int {
            float val = (q.y - p.y) * (r.x - q.x) - (q.x - p.x) * (r.y - q.y);
            if (val == 0) return 0; // Collinear
            return (val > 0) ? 1 : 2; // Clockwise or Counterclockwise
        };

        auto onSegment = [](vf2d p, vf2d q, vf2d r) -> bool {
            return q.x <= std::max(p.x, r.x) && q.x >= std::min(p.x, r.x) &&
                   q.y <= std::max(p.y, r.y) && q.y >= std::min(p.y, r.y);
        };

        // Find the 4 orientations required for general and
        // special cases
        int o1 = orientation(p1, q1, p2);
        int o2 = orientation(p1, q1, q2);
        int o3 = orientation(p2, q2, p1);
        int o4 = orientation(p2, q2, q1);

        // General case
        if (o1 != o2 && o3 != o4)
            return true;

        // Special Cases

        // p1, q1, and p2 are collinear, and p2 lies on segment p1q1
        if (o1 == 0 && onSegment(p1, p2, q1)) return true;

        // p1, q1, and q2 are collinear, and q2 lies on segment p1q1
        if (o2 == 0 && onSegment(p1, q2, q1)) return true;

        // p2, q2, and p1 are collinear, and p1 lies on segment p2q2
        if (o3 == 0 && onSegment(p2, p1, q2)) return true;

        // p2, q2, and q1 are collinear, and q1 lies on segment p2q2
        if (o4 == 0 && onSegment(p2, q1, q2)) return true;

        return false; // Doesn't fall in any of the above cases
    };


    auto lines = Helpers::getLinesFromRectangle(rect);

    for (auto &line: lines) {
        if (doIntersect(line.first, line.second, lineStart, lineEnd)) {
            return true;
        }
    }


    return false;
}

std::vector<std::pair<vf2d, vf2d>> Helpers::getLinesFromRectangle(rectf rect) {
    float x = rect.x;
    float y = rect.y;
    vf2d topLeft = {x, y};
    vf2d topRight = {x + rect.width, y};
    vf2d bottomLeft = {x, y + rect.height};
    vf2d bottomRight = {x + rect.width, y + rect.height};

    const std::pair topLine = {topLeft, topRight};
    const std::pair rightLine = {topRight, bottomRight};
    const std::pair bottomLine = {bottomRight, bottomLeft};
    const std::pair leftLine = {bottomLeft, topLeft};

    return std::vector{
            topLine,
            rightLine,
            bottomLine,
            leftLine
    };
}


void Helpers::DrawMainMenu() {
#ifdef ADD_IMGUI
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) {
                State::SetState("quit");
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug")) {
            if (ImGui::MenuItem("Textures")) {
                imguiTexturesVisible = !imguiTexturesVisible;
            }

            if (ImGui::MenuItem("Audio chunks")) {
                imguiAudioVisible = !imguiAudioVisible;
            }

            if (ImGui::MenuItem("Input devices")) {
                imguiInputVisible = !imguiInputVisible;
            }

            if (ImGui::MenuItem("ImGui Demo")) {
                imguiDemoVisible = !imguiDemoVisible;
            }

            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (imguiTexturesVisible) {
        ImGui::SetNextWindowSizeConstraints({200, 200}, {FLT_MAX, FLT_MAX});
        ImGui::Begin("Textures", &imguiTexturesVisible);

        ImGui::BeginChild("Loaded textures");

        for (auto &texture: AssetHandler::GetTextures()) {

            ImGui::Text("%s", texture.first.c_str());

            SDL_Texture *my_texture = texture.second.texture;
            int my_image_width = texture.second.width;
            int my_image_height = texture.second.height;

            ImGui::Text("size = %d x %d", my_image_width, my_image_height);

            SDL_PixelFormat *pixelFormat = texture.second.surface->format;
            SDL_PixelFormatEnum pixelFormatEnum = pixelFormat->format;
            const char *surfacePixelFormatName = SDL_GetPixelFormatName(pixelFormatEnum);

            ImGui::Text("pixel format = %s", surfacePixelFormatName);

            ImGui::Image((void *) my_texture, ImVec2(my_image_width, my_image_height));


        }
        ImGui::EndChild();
        ImGui::End();

    }

    if (imguiAudioVisible) {

    }

    if (imguiInputVisible) {
        ImGui::SetNextWindowSizeConstraints({200, 200}, {FLT_MAX, FLT_MAX});
        ImGui::Begin("Gamepads", &imguiInputVisible);

        int numJoysticks;
        SDL_JoystickID *joysticks = SDL_GetGamepads(&numJoysticks);

        for (int i = 0; i < numJoysticks; ++i) {

            int offSetX = 40;
            int offSetY = 70;

            SDL_Gamepad *gamepad = SDL_OpenGamepad(joysticks[i]);

            ImGui::BeginChild(SDL_GetGamepadName(gamepad));

            ImGui::Text("Gamepad %d: %s", i + 1, SDL_GetGamepadName(gamepad));

            GamepadTest gamepaddata;

            gamepaddata.DPadUp = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
            gamepaddata.DPadDown = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            gamepaddata.DPadLeft = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            gamepaddata.DPadRight = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

            gamepaddata.North = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH);
            gamepaddata.South = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
            gamepaddata.East = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST);
            gamepaddata.West = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST);

            gamepaddata.LeftShoulder = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
            gamepaddata.RightShoulder = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);

            gamepaddata.Start = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START);
            gamepaddata.Back = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK);
            gamepaddata.Guide = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_GUIDE);

            gamepaddata.LeftStickX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32768.f;
            gamepaddata.LeftStickY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32768.f;

            gamepaddata.RightStickX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32768.f;
            gamepaddata.RightStickY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32768.f;

            gamepaddata.LeftTrigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32768.f;
            gamepaddata.RightTrigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32768.f;

            std::map<SDL_GamepadButton, std::string> buttonNames;

            switch (SDL_GetGamepadType(gamepad)) {

                case SDL_GAMEPAD_TYPE_XBOXONE:
                    buttonNames[SDL_GAMEPAD_BUTTON_NORTH] = "Y";
                    buttonNames[SDL_GAMEPAD_BUTTON_SOUTH] = "A";
                    buttonNames[SDL_GAMEPAD_BUTTON_EAST] = "B";
                    buttonNames[SDL_GAMEPAD_BUTTON_WEST] = "X";
                    break;
                case SDL_GAMEPAD_TYPE_PS3:
                case SDL_GAMEPAD_TYPE_PS4:
                case SDL_GAMEPAD_TYPE_PS5:
                    buttonNames[SDL_GAMEPAD_BUTTON_NORTH] = "Triangle";
                    buttonNames[SDL_GAMEPAD_BUTTON_SOUTH] = "X";
                    buttonNames[SDL_GAMEPAD_BUTTON_EAST] = "Circle";
                    buttonNames[SDL_GAMEPAD_BUTTON_WEST] = "Square";
                    break;
                case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
                case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
                    buttonNames[SDL_GAMEPAD_BUTTON_NORTH] = "X";
                    buttonNames[SDL_GAMEPAD_BUTTON_SOUTH] = "B";
                    buttonNames[SDL_GAMEPAD_BUTTON_EAST] = "A";
                    buttonNames[SDL_GAMEPAD_BUTTON_WEST] = "Y";
                    break;
                default:
                case SDL_GAMEPAD_TYPE_UNKNOWN:
                case SDL_GAMEPAD_TYPE_STANDARD:
                case SDL_GAMEPAD_TYPE_XBOX360:
                case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
                case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
                case SDL_GAMEPAD_TYPE_MAX:
                    buttonNames[SDL_GAMEPAD_BUTTON_NORTH] = "2";
                    buttonNames[SDL_GAMEPAD_BUTTON_SOUTH] = "0";
                    buttonNames[SDL_GAMEPAD_BUTTON_EAST] = "1";
                    buttonNames[SDL_GAMEPAD_BUTTON_WEST] = "3";
                    break;
            }


            ImGui::SetCursorPos(ImVec2(offSetX + 20, offSetY + 70));
            ImGui::Checkbox("Left", &gamepaddata.DPadLeft);

            ImGui::SetCursorPos(ImVec2(offSetX + 70, offSetY + 120));
            ImGui::Checkbox("Down", &gamepaddata.DPadDown);

            ImGui::SetCursorPos(ImVec2(offSetX + 70, offSetY + 20));
            ImGui::Checkbox("Up", &gamepaddata.DPadUp);

            ImGui::SetCursorPos(ImVec2(offSetX + 120, offSetY + 70));
            ImGui::Checkbox("Right", &gamepaddata.DPadRight);


            ImGui::SetCursorPos(ImVec2(offSetX + 60, offSetY + 200));
            ImGui::SetNextItemWidth(150.0f);
            ImGui::SliderFloat(" ", &gamepaddata.LeftStickX, -1.0f, 1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 30, offSetY + 230));
            ImGui::VSliderFloat("  ", {30, 150}, &gamepaddata.LeftStickY, 1.0f, -1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 70, offSetY + 240));
            ImGui::Text("Left joystick");
            ImGui::SetCursorPos(ImVec2(offSetX + 70, offSetY + 260));
            ImGui::Text("X: %.3f", gamepaddata.LeftStickX);
            ImGui::SetCursorPos(ImVec2(offSetX + 70, offSetY + 280));
            ImGui::Text("Y: %.3f", gamepaddata.LeftStickY);


            ImGui::SetCursorPos(ImVec2(offSetX + 330, offSetY + 200));
            ImGui::SetNextItemWidth(150.0f);
            ImGui::SliderFloat(" ", &gamepaddata.RightStickX, -1.0f, 1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 300, offSetY + 230));
            ImGui::VSliderFloat("  ", {30, 150}, &gamepaddata.RightStickY, 1.0f, -1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 340, offSetY + 240));
            ImGui::Text("Right joystick");
            ImGui::SetCursorPos(ImVec2(offSetX + 340, offSetY + 260));
            ImGui::Text("X: %.3f", gamepaddata.RightStickX);
            ImGui::SetCursorPos(ImVec2(offSetX + 340, offSetY + 280));
            ImGui::Text("Y: %.3f", gamepaddata.RightStickY);


            ImGui::SetCursorPos(ImVec2(offSetX - 30, offSetY - 40));
            ImGui::VSliderFloat("  ", {30, 150}, &gamepaddata.LeftTrigger, 0.0f, 1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 10, offSetY - 40));
            ImGui::Checkbox("Left shoulder", &gamepaddata.LeftShoulder);

            ImGui::SetCursorPos(ImVec2(offSetX + 470, offSetY - 40));
            ImGui::VSliderFloat("  ", {30, 150}, &gamepaddata.RightTrigger, 0.0f, 1.0f, "", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetCursorPos(ImVec2(offSetX + 430, offSetY - 40));
            ImGui::Checkbox("    ", &gamepaddata.RightShoulder);
            ImGui::SetCursorPos(ImVec2(offSetX + 325, offSetY - 30));
            ImGui::Text("Right shoulder");


            ImGui::SetCursorPos(ImVec2(offSetX + 370, offSetY + 20));
            ImGui::Checkbox(buttonNames[SDL_GAMEPAD_BUTTON_NORTH].c_str(), &gamepaddata.North);

            ImGui::SetCursorPos(ImVec2(offSetX + 420, offSetY + 70));
            ImGui::Checkbox(buttonNames[SDL_GAMEPAD_BUTTON_EAST].c_str(), &gamepaddata.East);

            ImGui::SetCursorPos(ImVec2(offSetX + 320, offSetY + 70));
            ImGui::Checkbox(buttonNames[SDL_GAMEPAD_BUTTON_WEST].c_str(), &gamepaddata.West);

            ImGui::SetCursorPos(ImVec2(offSetX + 370, offSetY + 120));
            ImGui::Checkbox(buttonNames[SDL_GAMEPAD_BUTTON_SOUTH].c_str(), &gamepaddata.South);

            ImGui::SetCursorPos(ImVec2(offSetX + 150, offSetY + 150));
            ImGui::Checkbox("Back", &gamepaddata.Back);

            ImGui::SetCursorPos(ImVec2(offSetX + 300, offSetY + 150));
            ImGui::Checkbox("Start", &gamepaddata.Start);

            ImGui::SetCursorPos(ImVec2(offSetX + 225, offSetY + 120));
            ImGui::Checkbox("Guide", &gamepaddata.Guide);

            if (SDL_GetGamepadType(gamepad) != SDL_GAMEPAD_TYPE_PS5) {
                SDL_PropertiesID props = SDL_GetGamepadProperties(gamepad);

                if (SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, SDL_FALSE)) {

                    ImGui::SetCursorPos(ImVec2(offSetX + 550, offSetY - 40));
                    ImGui::VSliderFloat("      ", {30, 150}, &gamepaddata.rumbleLeft, 0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);

                    ImGui::SetCursorPos(ImVec2(offSetX + 600, offSetY - 40));
                    ImGui::VSliderFloat("        ", {30, 150}, &gamepaddata.rumbleRight, 0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);

                    float rumbleLeft = std::clamp(gamepaddata.rumbleLeft * 65535.f, 0.f, 65535.f);
                    float rumbleRight = std::clamp(gamepaddata.rumbleRight * 65535.f, 0.f, 65535.f);


                    SDL_RumbleGamepad(gamepad, (Uint16) rumbleLeft, (Uint16) rumbleRight, 100);
                }

                if (SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, SDL_FALSE)) {
                    ImGui::SetCursorPos(ImVec2(offSetX + 550, offSetY + 200));
                    ImGui::VSliderFloat("          ", {30, 150}, &gamepaddata.rumbleTriggerLeft, 0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);

                    ImGui::SetCursorPos(ImVec2(offSetX + 600, offSetY + 200));
                    ImGui::VSliderFloat("             ", {30, 150}, &gamepaddata.rumbleTriggerRight, 0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);

                    float rumbleTriggerLeft = std::clamp(gamepaddata.rumbleTriggerLeft * 65535.f, 0.f, 65535.f);
                    float rumbleTriggerRight = std::clamp(gamepaddata.rumbleTriggerRight * 65535.f, 0.f, 65535.f);

                    SDL_RumbleGamepadTriggers(gamepad, (Uint16) rumbleTriggerLeft, (Uint16) rumbleTriggerRight, 100);
                }
            } else {

                DS5EffectsState_t state;

                Uint8 effects[4][11] = {
                        /* Clear trigger effect */
                        {0x05, 0,   0,   0,   0, 0, 0, 0, 0, 0, 0},
                        /* Constant resistance across entire trigger pull */
                        {0x21, 255, 110, 0,   0, 0, 0, 0, 0, 0, 0},
                        /* Resistance and vibration when trigger is pulled */
                        {0x26, 15,  63,  128, 0, 0, 0, 0, 0, 0, 0},

                        {0x25, 15,  63,  128, 0, 0, 0, 0, 0, 0, 0},
                };

                SDL_zero(state);

                state.ucEnableBits1 |= (0x04 | 0x08); /* Modify right and left trigger effect respectively */


                ImGui::SetCursorPos(ImVec2(offSetX + 550, offSetY - 40));
                ImGui::VSliderFloat("      ", {30, 150}, &gamepaddata.rumbleLeft, 0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);

                ImGui::SetCursorPos(ImVec2(offSetX + 600, offSetY - 40));
                ImGui::VSliderFloat("        ", {30, 150}, &gamepaddata.rumbleRight, 0.0f, 1.0f, "", ImGuiSliderFlags_AlwaysClamp);

                float rumbleLeft = std::clamp(gamepaddata.rumbleLeft * 65535.f, 0.f, 65535.f);
                float rumbleRight = std::clamp(gamepaddata.rumbleRight * 65535.f, 0.f, 65535.f);


                state.ucRumbleLeft = (int) rumbleLeft / 256;
                state.ucRumbleRight = (int) rumbleRight / 256;
                SDL_RumbleGamepad(gamepad, (Uint16) rumbleLeft, (Uint16) rumbleRight, 100);


                SDL_memcpy(state.rgucRightTriggerEffect, effects[0], sizeof(effects[0]));
                SDL_memcpy(state.rgucLeftTriggerEffect, effects[0], sizeof(effects[0]));
//                SDL_SendGamepadEffect(gamepad, &state, sizeof(state));


            }


            ImGui::EndChild();

        }


        ImGui::End();
    }

    if (imguiDemoVisible) {
        ImGui::ShowDemoWindow(&imguiDemoVisible);

    }
#endif
}

bool Helpers::randomChance(const float required) {

    std::default_random_engine generator(time(0));
    std::uniform_real_distribution<double> distribution;

    const float chance = distribution(generator);

    if (chance > (required / 100.0f))
        return true;
    return false;

}

int Helpers::GetRandomValue(int min, int max) {

    std::random_device rd; // obtain a random number from hardware
    std::mt19937 gen(rd()); // seed the generator
    std::uniform_int_distribution<> distr(min, max); // define the range

    return distr(gen);

}


const char *Helpers::TextFormat(const char *text, ...) {
#ifndef MAX_TEXTFORMAT_BUFFERS
#define MAX_TEXTFORMAT_BUFFERS 4        // Maximum number of static buffers for text formatting
#endif

    // We create an array of buffers so strings don't expire until MAX_TEXTFORMAT_BUFFERS invocations
    static char buffers[MAX_TEXTFORMAT_BUFFERS][MAX_TEXT_BUFFER_LENGTH] = {{0}};
    static int index = 0;

    char *currentBuffer = buffers[index];
    memset(currentBuffer, 0, MAX_TEXT_BUFFER_LENGTH);   // Clear buffer before using

    va_list args;
    va_start(args, text);
    vsnprintf(currentBuffer, MAX_TEXT_BUFFER_LENGTH, text, args);
    va_end(args);

    index += 1;     // Move to next buffer for next function call
    if (index >= MAX_TEXTFORMAT_BUFFERS) index = 0;

    return currentBuffer;
}


