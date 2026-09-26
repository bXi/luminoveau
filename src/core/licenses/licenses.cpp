#include "core/licenses/licenses.h"

#include "assets/assethandler.h"
#include "core/enginestate/enginestate.h"
#include "core/licenses/noticerecord.h"
#include "draw/draw.h"
#include "draw/text.h"
#include "gpu/presets.h"
#include "platform/input/input.h"
#include "platform/window/window.h"
#include "renderer/passes/spriterenderpass.h"
#include "renderer/renderer.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

static const char *kLicensesPass = "__licensesOverlay__";
static const char *kLicensesFB   = "__licensesOverlayFB__";

namespace {

constexpr float REPEAT_DELAY     = 0.38f;
constexpr float REPEAT_INTERVAL  = 0.075f;
constexpr float SCROLL_SMOOTHING = 16.0f;
constexpr float FLING_FRICTION   = 5.0f;

constexpr const char *MIDDLE_DOT = "\xC2\xB7";

Color withAlpha(Color c, float alphaScale) {
    c.a = static_cast<unsigned int>(std::clamp(static_cast<float>(c.a) * alphaScale, 0.0f, 255.0f));
    return c;
}

bool contains(const rectf &r, vf2d p) {
    return p.x >= r.x && p.x < r.x + r.width && p.y >= r.y && p.y < r.y + r.height;
}

// Draw::SetScissorMode takes framebuffer pixels; everything here is laid out in window coordinates.
void setClip(const rectf &r) {
    float sx = Window::GetWidth() > 0 ? static_cast<float>(Window::GetPhysicalWidth()) / static_cast<float>(Window::GetWidth()) : 1.0f;
    float sy = Window::GetHeight() > 0 ? static_cast<float>(Window::GetPhysicalHeight()) / static_cast<float>(Window::GetHeight()) : 1.0f;
    Draw::SetScissorMode(rectf(std::floor(r.x * sx), std::floor(r.y * sy), std::ceil(r.width * sx), std::ceil(r.height * sy)));
}

void clearClip() {
    setClip(rectf(0.0f, 0.0f, static_cast<float>(Window::GetWidth()), static_cast<float>(Window::GetHeight())));
}

float textWidth(FontAsset &font, const std::string &s, float size) {
    return s.empty() ? 0.0f : Text::GetRenderedTextSize(font, s, size).x;
}

// Text is placed by its top-left corner; this puts a line of the given size in the middle of a box.
float centeredTop(float boxTop, float boxHeight, float size) {
    return boxTop + (boxHeight - size) * 0.5f;
}

std::string fitText(FontAsset &font, const std::string &s, float size, float maxWidth) {
    if (textWidth(font, s, size) <= maxWidth)
        return s;
    std::string cut = s;
    while (!cut.empty()) {
        while (!cut.empty()) {
            unsigned char last = static_cast<unsigned char>(cut.back());
            cut.pop_back();
            if ((last & 0xC0) != 0x80)
                break;
        }
        while (!cut.empty() && cut.back() == ' ')
            cut.pop_back();
        if (textWidth(font, cut + "...", size) <= maxWidth)
            return cut + "...";
    }
    return "...";
}

std::string withoutScheme(const std::string &url) {
    for (const char *scheme : { "https://", "http://" }) {
        size_t length = std::strlen(scheme);
        if (url.compare(0, length, scheme) == 0)
            return url.substr(length);
    }
    return url;
}

// Stepped strips standing in for a gradient: text scrolling under an edge fades instead of being cut.
void drawFade(const rectf &area, Color color, bool fromTop) {
    constexpr int STEPS = 10;
    const float   step  = area.height / STEPS;
    for (int i = 0; i < STEPS; ++i) {
        const float alpha = 1.0f - (static_cast<float>(i) + 0.5f) / STEPS;
        const float y     = fromTop ? area.y + static_cast<float>(i) * step : area.y + area.height - static_cast<float>(i + 1) * step;
        Draw::RectangleFilled({ area.x, y }, { area.width, step + 0.5f }, withAlpha(color, alpha));
    }
}

// Face buttons are named by what is printed on them, which differs between Xbox, PlayStation and
// Nintendo layouts even though SDL reports the same position.
std::string buttonLabel(int gamepadIndex, SDL_GamepadButton button, const char *fallback) {
    SDL_Gamepad *pad = SDL_GetGamepadFromID(Input::GetGamepadInstanceId(gamepadIndex));
    if (!pad)
        return fallback;
    switch (SDL_GetGamepadButtonLabel(pad, button)) {
    case SDL_GAMEPAD_BUTTON_LABEL_A:
        return "A";
    case SDL_GAMEPAD_BUTTON_LABEL_B:
        return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_X:
        return "X";
    case SDL_GAMEPAD_BUTTON_LABEL_Y:
        return "Y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
        return "Cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
        return "Circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
        return "Square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
        return "Triangle";
    default:
        return fallback;
    }
}

} // namespace

// ── Opening and closing ───────────────────────────────────────────────────────

void Licenses::_open(const LicensesStyle &style) {
    _style        = style;
    _isOpen       = true;
    _openedFrame  = EngineState::frameCount;
    _selected     = 0;
    _view         = View::List;
    _listScroll   = 0;
    _listTarget   = 0;
    _textScroll   = 0;
    _textTarget   = 0;
    _textVelocity = 0;
    _drag         = Drag::None;
    _repeatDir    = 0;

    // The style may bring other fonts or sizes, so nothing measured with the previous one holds.
    _rowsWidth      = -1;
    _linesComponent = -1;
}

void Licenses::_close() {
    if (!_isOpen)
        return;
    _isOpen = false;
    if (_quitOnClose)
        Window::Close();
}

void Licenses::_back(const Layout &layout) {
    if (layout.stacked && _view == View::Detail) {
        _view = View::List;
        return;
    }
    _close();
}

bool Licenses::_handleCommandLine(int argc, char *argv[]) {
    bool requested = false;
    for (int i = 1; argv && i < argc; ++i) {
        if (argv[i] && std::strcmp(argv[i], "--lumi-licenses") == 0)
            requested = true;
    }
#ifdef __EMSCRIPTEN__
    if (emscripten_run_script_int("new URLSearchParams(window.location.search).has('licenses') ? 1 : 0") == 1)
        requested = true;
#endif
    if (!requested)
        return false;

#ifndef __EMSCRIPTEN__
    // Launched only to show the licenses, so backing out of them ends the program. A page cannot
    // close its own tab, so on the web the game simply carries on underneath.
    _quitOnClose = true;
#endif
    _open(LicensesStyle {});
    return true;
}

// ── Notice data ───────────────────────────────────────────────────────────────

const std::vector<ThirdPartyNotice> &Licenses::_getNotices() {
    if (_notices.empty()) {
        _notices.reserve(LUMI_NOTICE_RECORD_COUNT);
        for (size_t i = 0; i < LUMI_NOTICE_RECORD_COUNT; ++i) {
            const NoticeRecord &r = LUMI_NOTICE_RECORDS[i];
            _notices.push_back({ r.name, r.spdx, r.version, r.url, std::string(r.text, r.textLength) });
        }
    }
    return _notices;
}

std::string Licenses::_getNoticesText() {
    // Kept identical to the THIRD_PARTY_NOTICES.txt that cmake/ThirdPartyNotices.cmake writes.
    std::string out = "THIRD-PARTY SOFTWARE NOTICES\n\nThis software includes the following components, each listed with the license it is\ndistributed under.\n";
    for (const ThirdPartyNotice &notice : _getNotices()) {
        std::string heading = notice.version.empty() ? notice.name : notice.name + " " + notice.version;
        out += "\n\n" + heading + "\n" + std::string(heading.size(), '=') + "\n";
        if (!notice.spdx.empty())
            out += "License: " + notice.spdx + "\n";
        if (!notice.url.empty())
            out += "Source:  " + notice.url + "\n";
        out += "\n" + notice.text + "\n";
    }
    return out;
}

void Licenses::_loadCatalog() {
    if (_catalogReady)
        return;
    std::vector<LicenseText::Source> sources;
    sources.reserve(LUMI_NOTICE_RECORD_COUNT);
    for (size_t i = 0; i < LUMI_NOTICE_RECORD_COUNT; ++i) {
        const NoticeRecord &r = LUMI_NOTICE_RECORDS[i];
        sources.push_back({ r.name, r.spdx, r.version, r.url, std::string_view(r.text, r.textLength) });
    }
    _catalog      = LicenseText::Build(sources);
    _catalogReady = true;
}

// ── Frame ─────────────────────────────────────────────────────────────────────

void Licenses::_render() {
    if (!_isOpen)
        return;

    _loadCatalog();
    if (_catalog.components.empty()) {
        _close();
        return;
    }
    if (!_fbReady)
        _initOverlay();

    const float  dt     = std::clamp(static_cast<float>(Window::GetFrameTime()), 0.0f, 0.1f);
    const Layout layout = _computeLayout();

    _layoutText(layout);
    _handleInput(layout, dt);
    if (!_isOpen)
        return;
    _layoutRows(layout);
    _layoutText(layout);
    _updateScroll(layout, dt);

    Draw::SetTargetRenderPass(kLicensesPass);
    _draw(layout);
    Draw::ResetTargetRenderPass();
}

void Licenses::_initOverlay() {
    // Created on first open, after every framebuffer the game made at startup, so it composites
    // on top of all of them.
    Renderer::CreateFrameBuffer(kLicensesFB);
    Renderer::SetFramebufferRenderToScreen(kLicensesFB, true);
    auto *pass = new SpriteRenderPass();
    pass->UpdateRenderPassBlendState(GpuPresets::AlphaBlendPreserveAlpha);
    pass->Init(Renderer::GetGpu().GetSwapchainFormat(), Window::GetWidth(), Window::GetHeight(), kLicensesPass);
    pass->colorTargetInfoLoadOp = GpuLoadOp::Clear;
    pass->colorTargetClearR = pass->colorTargetClearG = pass->colorTargetClearB = pass->colorTargetClearA = 0.0f;
    Renderer::AttachRenderPassToFrameBuffer(pass, kLicensesPass, kLicensesFB);
    _fbReady = true;
}

FontAsset &Licenses::_titleFont() const {
    return _style.titleFont ? *_style.titleFont : AssetHandler::GetDefaultFont();
}

FontAsset &Licenses::_bodyFont() const {
    return _style.bodyFont ? *_style.bodyFont : AssetHandler::GetDefaultFont();
}

// ── Layout ────────────────────────────────────────────────────────────────────

Licenses::Layout Licenses::_computeLayout() const {
    Layout l;
    l.width  = static_cast<float>(Window::GetWidth());
    l.height = static_cast<float>(Window::GetHeight());
    l.unit   = std::clamp(l.height / 1080.0f, 0.55f, 3.0f) * std::max(_style.scale, 0.1f);

    const float u  = l.unit;
    l.titleSize    = 46.0f * u;
    l.subtitleSize = 21.0f * u;
    l.rowSize      = 23.0f * u;
    l.rowMetaSize  = 17.0f * u;
    l.nameSize     = 34.0f * u;
    l.metaSize     = 18.0f * u;
    l.bodySize     = std::max(20.0f * u, 13.0f);
    l.lineHeight   = l.bodySize * 1.5f;
    l.rowHeight    = 50.0f * u;
    l.hintSize     = 18.0f * u;
    l.pad          = std::max(34.0f * u, 12.0f);

    const float margin = std::min(l.width, l.height) * std::clamp(_style.safeArea, 0.0f, 0.25f);
    const float panelW = std::min(l.width - margin * 2.0f, 1760.0f * u);
    l.panel            = rectf((l.width - panelW) * 0.5f, margin, panelW, l.height - margin * 2.0f);
    l.stacked          = panelW < 860.0f * u;

    const float headerH = l.pad + l.titleSize * 1.1f + 8.0f * u + l.subtitleSize * 1.2f + l.pad * 0.75f;
    const float footerH = 64.0f * u;
    l.contentTop        = l.panel.y + headerH;
    l.contentBottom     = std::max(l.panel.y + l.panel.height - footerH, l.contentTop);
    const float contentH = l.contentBottom - l.contentTop;

    const float closeW = 110.0f * u;
    const float closeH = 44.0f * u;
    l.closeButton      = rectf(l.panel.x + l.panel.width - l.pad - closeW, l.panel.y + l.pad, closeW, closeH);

    const float listInset = 12.0f * u;
    if (l.stacked) {
        l.list   = rectf(l.panel.x + l.pad * 0.5f, l.contentTop + listInset, l.panel.width - l.pad, std::max(contentH - listInset * 2.0f, 0.0f));
        l.detail = rectf(l.panel.x + l.pad, l.contentTop, l.panel.width - l.pad * 2.0f, contentH);
    } else {
        const float listW   = std::clamp(l.panel.width * 0.3f, 300.0f * u, 460.0f * u);
        l.list              = rectf(l.panel.x + l.pad * 0.5f, l.contentTop + listInset, listW, std::max(contentH - listInset * 2.0f, 0.0f));
        const float detailX = l.list.x + listW + l.pad;
        l.detail            = rectf(detailX, l.contentTop, l.panel.x + l.panel.width - l.pad - detailX, contentH);
    }

    const float detailHeaderH = 20.0f * u + l.nameSize * 1.15f + l.metaSize * 1.3f + 18.0f * u;
    const float scrollbarRoom = 22.0f * u;
    l.text                    = rectf(l.detail.x, l.detail.y + detailHeaderH,
                           std::max(l.detail.width - scrollbarRoom, 50.0f),
                           std::max(l.detail.height - detailHeaderH - 10.0f * u, 0.0f));
    return l;
}

void Licenses::_layoutRows(const Layout &layout) {
    if (_rowsWidth == layout.list.width && _rowsUnit == layout.unit && _rowNames.size() == _catalog.components.size())
        return;
    _rowsWidth = layout.list.width;
    _rowsUnit  = layout.unit;
    _rowNames.clear();
    _rowLicenses.clear();

    FontAsset  &font = _bodyFont();
    const float u    = layout.unit;
    for (const LicenseText::Component &component : _catalog.components) {
        std::string license = fitText(font, component.spdx, layout.rowMetaSize, layout.list.width * 0.45f);
        const float room    = layout.list.width - 36.0f * u - textWidth(font, license, layout.rowMetaSize) - 20.0f * u;
        _rowNames.push_back(fitText(font, component.name, layout.rowSize, room));
        _rowLicenses.push_back(std::move(license));
    }
}

void Licenses::_layoutText(const Layout &layout) {
    if (_linesComponent == _selected && _linesWidth == layout.text.width && _linesUnit == layout.unit)
        return;
    _linesComponent = _selected;
    _linesWidth     = layout.text.width;
    _linesUnit      = layout.unit;
    _lines.clear();

    FontAsset                             &font      = _bodyFont();
    const LicenseText::Component          &component = _catalog.components[_selected];
    std::unordered_map<std::string, float> widths;

    auto measure = [&](std::string_view s) {
        std::string key(s);
        auto        it = widths.find(key);
        if (it != widths.end())
            return it->second;
        const float width = textWidth(font, key, layout.bodySize);
        widths.emplace(std::move(key), width);
        return width;
    };

    const float paragraphGap = layout.lineHeight * 0.6f;
    float       y            = 0.0f;
    auto        addParagraph = [&](const std::string &paragraph, bool dimmed) {
        for (std::string &line : LicenseText::Wrap(paragraph, layout.text.width, measure)) {
            _lines.push_back({ std::move(line), y, layout.bodySize, dimmed });
            y += layout.lineHeight;
        }
        y += paragraphGap;
    };

    // Holders first: for a shared text they are the only part that belongs to this component.
    for (const std::string &copyright : component.copyrights)
        addParagraph(copyright, false);
    for (const std::string &paragraph : _catalog.texts[component.textIndex])
        addParagraph(paragraph, true);

    _contentHeight = std::max(0.0f, y - paragraphGap);
}

void Licenses::_select(int index, const Layout &layout) {
    index = std::clamp(index, 0, static_cast<int>(_catalog.components.size()) - 1);
    if (index != _selected) {
        _selected     = index;
        _textScroll   = 0;
        _textTarget   = 0;
        _textVelocity = 0;
    }

    const float top    = static_cast<float>(index) * layout.rowHeight;
    const float bottom = top + layout.rowHeight;
    if (top < _listTarget)
        _listTarget = top;
    if (bottom > _listTarget + layout.list.height)
        _listTarget = bottom - layout.list.height;
}

// ── Input ─────────────────────────────────────────────────────────────────────

void Licenses::_handleInput(const Layout &layout, float dt) {
    const vf2d mouse = Input::GetMousePosition();
    if (_lastMouse.x < 0.0f)
        _lastMouse = mouse;

    // The press that opened the screen belongs to whatever was showing before it.
    if (EngineState::frameCount == _openedFrame) {
        _lastMouse = mouse;
        return;
    }

    const bool listVisible   = _listVisible(layout);
    const bool detailVisible = _detailVisible(layout);
    const int  count         = static_cast<int>(_catalog.components.size());

    bool  back     = false;
    bool  accept   = false;
    bool  holdUp   = false;
    bool  holdDown = false;
    float pages    = 0.0f;
    float analog   = 0.0f;

    // Keyboard
    back     = Input::KeyPressed(SDLK_ESCAPE) || Input::KeyPressed(SDLK_BACKSPACE) || Input::KeyPressed(SDLK_AC_BACK);
    accept   = Input::KeyPressed(SDLK_RETURN) || Input::KeyPressed(SDLK_KP_ENTER);
    holdUp   = Input::KeyDown(SDLK_UP);
    holdDown = Input::KeyDown(SDLK_DOWN);
    if (Input::KeyPressed(SDLK_PAGEDOWN) || Input::KeyPressed(SDLK_SPACE))
        pages += 1.0f;
    if (Input::KeyPressed(SDLK_PAGEUP))
        pages -= 1.0f;
    const bool home = Input::KeyPressed(SDLK_HOME);
    const bool end  = Input::KeyPressed(SDLK_END);
    if (back || accept || holdUp || holdDown || pages != 0.0f || home || end)
        _usingGamepad = false;

    // Every connected gamepad, so whichever controller the player picked up works.
    for (int pad = 0; Input::GetGamepadInstanceId(pad) != 0; ++pad) {
        bool active  = false;
        auto pressed = [&](SDL_GamepadButton button) {
            const bool p = Input::GamepadButtonPressed(pad, button);
            active       = active || p;
            return p;
        };
        auto down = [&](SDL_GamepadButton button) {
            const bool d = Input::GamepadButtonDown(pad, button);
            active       = active || d;
            return d;
        };

        if (pressed(SDL_GAMEPAD_BUTTON_EAST) || pressed(SDL_GAMEPAD_BUTTON_BACK))
            back = true;
        if (pressed(SDL_GAMEPAD_BUTTON_SOUTH))
            accept = true;
        if (pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER))
            pages += 1.0f;
        if (pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))
            pages -= 1.0f;

        const float leftY    = Input::GetGamepadAxisMovement(pad, SDL_GAMEPAD_AXIS_LEFTY);
        const float rightY   = Input::GetGamepadAxisMovement(pad, SDL_GAMEPAD_AXIS_RIGHTY);
        const float triggers = Input::GetGamepadAxisMovement(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
            - Input::GetGamepadAxisMovement(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);

        const bool dpadUp   = down(SDL_GAMEPAD_BUTTON_DPAD_UP);
        const bool dpadDown = down(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        holdUp              = holdUp || dpadUp || leftY < -0.5f;
        holdDown            = holdDown || dpadDown || leftY > 0.5f;
        analog += rightY + triggers;
        if (std::abs(leftY) > 0.5f || std::abs(rightY) > 0.1f || std::abs(triggers) > 0.1f)
            active = true;

        if (active) {
            _usingGamepad = true;
            _lastGamepad  = pad;
        }
    }

    // Mouse and touch
    if (std::abs(mouse.x - _lastMouse.x) + std::abs(mouse.y - _lastMouse.y) > 2.0f)
        _usingGamepad = false;

    const float wheel = static_cast<float>(Input::MouseScrolledDown()) - static_cast<float>(Input::MouseScrolledUp());
    if (wheel != 0.0f) {
        _usingGamepad = false;
        if (listVisible && contains(layout.list, mouse)) {
            _listTarget += wheel * layout.rowHeight * 1.5f;
        } else if (detailVisible) {
            _textTarget += wheel * layout.lineHeight * 3.0f;
            _textVelocity = 0.0f;
        }
    }

    if (Input::MouseButtonPressed(SDL_BUTTON_LEFT)) {
        _usingGamepad = false;
        _dragLastY    = mouse.y;
        _dragDistance = 0.0f;
        if (contains(layout.closeButton, mouse)) {
            back = true;
        } else if (listVisible && contains(layout.list, mouse)) {
            _drag = Drag::List;
        } else if (detailVisible && contains(layout.text, mouse)) {
            _drag         = Drag::Text;
            _textVelocity = 0.0f;
        }
    }

    if (_drag != Drag::None) {
        const float dy = mouse.y - _dragLastY;
        _dragLastY     = mouse.y;
        _dragDistance += std::abs(dy);

        if (_drag == Drag::List) {
            _listTarget -= dy;
            _listScroll = _listTarget;
        } else {
            _textTarget -= dy;
            _textScroll = _textTarget;
            if (dt > 0.0f)
                _textVelocity = _textVelocity * 0.5f - (dy / dt) * 0.5f;
        }

        if (!Input::MouseButtonDown(SDL_BUTTON_LEFT)) {
            // A press that barely moved is a tap on a row, not a drag.
            if (_drag == Drag::List && _dragDistance < 10.0f * layout.unit && contains(layout.list, mouse)) {
                const int row = static_cast<int>(std::floor((mouse.y - layout.list.y + _listScroll) / layout.rowHeight));
                if (row >= 0 && row < count) {
                    _select(row, layout);
                    if (layout.stacked)
                        _view = View::Detail;
                }
            }
            _drag = Drag::None;
        }
    }
    _lastMouse = mouse;

    if (back) {
        _back(layout);
        return;
    }
    if (accept && layout.stacked && _view == View::List)
        _view = View::Detail;

    // Held direction: one step at once, then repeats after a short delay.
    const int direction = holdDown ? 1 : (holdUp ? -1 : 0);
    int       steps     = 0;
    if (direction != _repeatDir) {
        _repeatDir   = direction;
        _repeatTimer = REPEAT_DELAY;
        steps        = direction;
    } else if (direction != 0) {
        _repeatTimer -= dt;
        while (_repeatTimer <= 0.0f) {
            _repeatTimer += REPEAT_INTERVAL;
            steps += direction;
        }
    }
    if (steps != 0) {
        if (listVisible) {
            _select(_selected + steps, layout);
        } else {
            _textTarget += static_cast<float>(steps) * layout.lineHeight * 2.0f;
            _textVelocity = 0.0f;
        }
    }

    if (detailVisible) {
        if (pages != 0.0f) {
            _textTarget += pages * layout.text.height * 0.85f;
            _textVelocity = 0.0f;
        }
        if (home)
            _textTarget = 0.0f;
        if (end)
            _textTarget = _contentHeight;
        if (analog != 0.0f) {
            _textTarget += analog * 1400.0f * layout.unit * dt;
            _textScroll   = _textTarget;
            _textVelocity = 0.0f;
        }
    }
}

void Licenses::_updateScroll(const Layout &layout, float dt) {
    const float listMax = std::max(0.0f, static_cast<float>(_catalog.components.size()) * layout.rowHeight - layout.list.height);
    const float textMax = std::max(0.0f, _contentHeight - layout.text.height);

    if (_drag != Drag::Text && std::abs(_textVelocity) > 1.0f) {
        _textTarget += _textVelocity * dt;
        _textVelocity *= std::exp(-dt * FLING_FRICTION);
    }

    _listTarget = std::clamp(_listTarget, 0.0f, listMax);
    const float clamped = std::clamp(_textTarget, 0.0f, textMax);
    if (clamped != _textTarget)
        _textVelocity = 0.0f;
    _textTarget = clamped;

    const float blend = 1.0f - std::exp(-dt * SCROLL_SMOOTHING);
    _listScroll += (_listTarget - _listScroll) * blend;
    _textScroll += (_textTarget - _textScroll) * blend;
    if (std::abs(_listTarget - _listScroll) < 0.5f)
        _listScroll = _listTarget;
    if (std::abs(_textTarget - _textScroll) < 0.5f)
        _textScroll = _textTarget;
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void Licenses::_draw(const Layout &layout) {
    const float u         = layout.unit;
    FontAsset  &titleFont = _titleFont();
    FontAsset  &bodyFont  = _bodyFont();
    const rectf panel     = layout.panel;

    clearClip();
    Draw::RectangleFilled({ 0.0f, 0.0f }, { layout.width, layout.height }, _style.backdrop);
    Draw::RectangleRoundedFilled({ panel.x, panel.y }, { panel.width, panel.height }, _style.cornerRadius * u, _style.panel);

    // Header
    const float left      = panel.x + layout.pad;
    const float right     = panel.x + panel.width - layout.pad;
    const float subtitleY = panel.y + layout.pad + layout.titleSize * 1.1f + 8.0f * u;
    Text::DrawText(titleFont, { left, panel.y + layout.pad }, fitText(titleFont, _style.title, layout.titleSize, layout.closeButton.x - left - 24.0f * u), _style.text, layout.titleSize);

    const std::string count  = std::to_string(_catalog.components.size()) + " components";
    const float       countW = textWidth(bodyFont, count, layout.subtitleSize);
    Text::DrawText(bodyFont, { left, subtitleY }, fitText(bodyFont, _style.subtitle, layout.subtitleSize, right - countW - 32.0f * u - left), _style.dimText, layout.subtitleSize);
    Text::DrawText(bodyFont, { right - countW, subtitleY }, count, _style.dimText, layout.subtitleSize);

    const rectf       button     = layout.closeButton;
    const std::string closeLabel = (layout.stacked && _view == View::Detail) ? "Back" : "Close";
    const bool        hovered    = !_usingGamepad && contains(button, _lastMouse);
    Draw::RectangleRoundedFilled({ button.x, button.y }, { button.width, button.height }, button.height * 0.5f, withAlpha(_style.text, hovered ? 0.16f : 0.08f));
    const float labelW = textWidth(bodyFont, closeLabel, layout.hintSize);
    Text::DrawText(bodyFont, { button.x + (button.width - labelW) * 0.5f, centeredTop(button.y, button.height, layout.hintSize) }, closeLabel, _style.text, layout.hintSize);

    // Dividers
    const float hairline = std::max(1.0f, u);
    Draw::RectangleFilled({ panel.x, layout.contentTop }, { panel.width, hairline }, _style.divider);
    Draw::RectangleFilled({ panel.x, layout.contentBottom }, { panel.width, hairline }, _style.divider);
    if (!layout.stacked)
        Draw::RectangleFilled({ layout.list.x + layout.list.width + layout.pad * 0.5f, layout.contentTop }, { hairline, layout.contentBottom - layout.contentTop }, _style.divider);

    if (_listVisible(layout))
        _drawList(layout);
    if (_detailVisible(layout))
        _drawDetail(layout);
    _drawFooter(layout);
}

void Licenses::_drawList(const Layout &layout) {
    const float u     = layout.unit;
    FontAsset  &font  = _bodyFont();
    const rectf list  = layout.list;
    const int   count = static_cast<int>(_catalog.components.size());

    setClip(list);
    const int  first        = std::max(0, static_cast<int>(std::floor(_listScroll / layout.rowHeight)));
    const int  last         = std::min(count - 1, static_cast<int>(std::ceil((_listScroll + list.height) / layout.rowHeight)));
    const bool hoverEnabled = !_usingGamepad && _drag == Drag::None && contains(list, _lastMouse);

    for (int i = first; i <= last; ++i) {
        const float y        = list.y + static_cast<float>(i) * layout.rowHeight - _listScroll;
        const bool  selected = i == _selected;
        const bool  hovered  = hoverEnabled && _lastMouse.y >= y && _lastMouse.y < y + layout.rowHeight;

        if (selected) {
            Draw::RectangleRoundedFilled({ list.x, y + 3.0f * u }, { list.width, layout.rowHeight - 6.0f * u }, 9.0f * u, withAlpha(_style.accent, 0.18f));
            Draw::RectangleRoundedFilled({ list.x + 6.0f * u, y + layout.rowHeight * 0.3f }, { 4.0f * u, layout.rowHeight * 0.4f }, 2.0f * u, _style.accent);
        } else if (hovered) {
            Draw::RectangleRoundedFilled({ list.x, y + 3.0f * u }, { list.width, layout.rowHeight - 6.0f * u }, 9.0f * u, withAlpha(_style.text, 0.06f));
        }

        Text::DrawText(font, { list.x + 20.0f * u, centeredTop(y, layout.rowHeight, layout.rowSize) }, _rowNames[i], selected ? _style.text : withAlpha(_style.text, 0.85f), layout.rowSize);
        const float licenseW = textWidth(font, _rowLicenses[i], layout.rowMetaSize);
        Text::DrawText(font, { list.x + list.width - 16.0f * u - licenseW, centeredTop(y, layout.rowHeight, layout.rowMetaSize) }, _rowLicenses[i], _style.dimText, layout.rowMetaSize);
    }
    clearClip();

    const float total = static_cast<float>(count) * layout.rowHeight;
    const float fadeH = std::min(24.0f * u, list.height * 0.25f);
    if (_listScroll > 0.5f)
        drawFade(rectf(list.x, list.y, list.width, fadeH), _style.panel, true);
    if (_listScroll < total - list.height - 0.5f)
        drawFade(rectf(list.x, list.y + list.height - fadeH, list.width, fadeH), _style.panel, false);
}

void Licenses::_drawDetail(const Layout &layout) {
    const float                   u         = layout.unit;
    FontAsset                    &titleFont = _titleFont();
    FontAsset                    &font      = _bodyFont();
    const rectf                   text      = layout.text;
    const LicenseText::Component &component = _catalog.components[_selected];

    float y = layout.detail.y + 20.0f * u;
    Text::DrawText(titleFont, { layout.detail.x, y }, fitText(titleFont, component.name, layout.nameSize, layout.detail.width), _style.text, layout.nameSize);
    y += layout.nameSize * 1.15f;

    std::string meta;
    for (const std::string &part : { component.spdx, component.version, withoutScheme(component.url) }) {
        if (part.empty())
            continue;
        if (!meta.empty())
            meta += std::string("   ") + MIDDLE_DOT + "   ";
        meta += part;
    }
    Text::DrawText(font, { layout.detail.x, y }, fitText(font, meta, layout.metaSize, layout.detail.width), _style.dimText, layout.metaSize);

    // Only the lines inside the viewport are drawn; a long license is hundreds of them.
    setClip(text);
    auto first = std::lower_bound(_lines.begin(), _lines.end(), _textScroll - layout.lineHeight,
        [](const TextLine &line, float value) { return line.y < value; });
    for (auto it = first; it != _lines.end(); ++it) {
        const float lineY = text.y + it->y - _textScroll;
        if (lineY > text.y + text.height)
            break;
        if (!it->text.empty())
            Text::DrawText(font, { text.x, lineY }, it->text, it->dimmed ? withAlpha(_style.text, 0.8f) : _style.text, it->size);
    }
    clearClip();

    const float maxScroll = std::max(0.0f, _contentHeight - text.height);
    const float fadeH     = std::min(28.0f * u, text.height * 0.25f);
    if (_textScroll > 0.5f)
        drawFade(rectf(text.x, text.y, text.width, fadeH), _style.panel, true);
    if (_textScroll < maxScroll - 0.5f)
        drawFade(rectf(text.x, text.y + text.height - fadeH, text.width, fadeH), _style.panel, false);

    if (maxScroll > 0.0f) {
        const float trackX = text.x + text.width + 12.0f * u;
        const float barW   = 5.0f * u;
        const float thumbH = std::max(text.height * (text.height / _contentHeight), 36.0f * u);
        const float thumbY = text.y + (text.height - thumbH) * (_textScroll / maxScroll);
        Draw::RectangleRoundedFilled({ trackX, text.y }, { barW, text.height }, barW * 0.5f, withAlpha(_style.text, 0.06f));
        Draw::RectangleRoundedFilled({ trackX, thumbY }, { barW, thumbH }, barW * 0.5f, withAlpha(_style.accent, 0.8f));
    }
}

void Licenses::_drawFooter(const Layout &layout) {
    struct Hint {
        std::string key;
        std::string action;
    };

    const bool        listOnly   = layout.stacked && _view == View::List;
    const bool        detailOnly = layout.stacked && _view == View::Detail;
    std::vector<Hint> hints;

    if (_usingGamepad && _lastGamepad >= 0) {
        if (!detailOnly)
            hints.push_back({ "D-Pad", "Select" });
        if (listOnly)
            hints.push_back({ buttonLabel(_lastGamepad, SDL_GAMEPAD_BUTTON_SOUTH, "South"), "Open" });
        if (!listOnly)
            hints.push_back({ "R-Stick", "Scroll" });
        hints.push_back({ buttonLabel(_lastGamepad, SDL_GAMEPAD_BUTTON_EAST, "East"), detailOnly ? "Back" : "Close" });
    } else {
        if (!detailOnly)
            hints.push_back({ "Up/Down", "Select" });
        if (listOnly)
            hints.push_back({ "Enter", "Open" });
        if (!listOnly)
            hints.push_back({ "PgUp/PgDn", "Scroll" });
        hints.push_back({ "Esc", detailOnly ? "Back" : "Close" });
    }

    const float u         = layout.unit;
    FontAsset  &font      = _bodyFont();
    const float top       = layout.contentBottom;
    const float height    = layout.panel.y + layout.panel.height - top;
    const float keySize   = layout.hintSize * 0.9f;
    const float capHeight = layout.hintSize * 1.7f;
    const float capPad    = 10.0f * u;
    const float capY      = top + (height - capHeight) * 0.5f;

    float x = layout.panel.x + layout.panel.width - layout.pad;
    for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
        x -= textWidth(font, it->action, layout.hintSize);
        Text::DrawText(font, { x, centeredTop(top, height, layout.hintSize) }, it->action, _style.dimText, layout.hintSize);

        const float capW = textWidth(font, it->key, keySize) + capPad * 2.0f;
        x -= 10.0f * u + capW;
        Draw::RectangleRoundedFilled({ x, capY }, { capW, capHeight }, 6.0f * u, withAlpha(_style.text, 0.1f));
        Text::DrawText(font, { x + capPad, centeredTop(capY, capHeight, keySize) }, it->key, _style.text, keySize);

        x -= 30.0f * u;
    }
}
