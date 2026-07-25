// WebGPU-backend mouse-position query.
// - Emscripten path: hooks document-level POINTER events (mouse + touch + pen) via JS so a finger
//   drag tracks like a mouse — browsers only synthesise a mouse *click* from a tap, never mousemove
//   during a drag, so plain mouse listeners left touch drags dead. Pointer events unify all three.
//   The coordinate space tracks #canvas's bounding rect (what the WebGPU swapchain renders to).
//   Position, button mask and multi-touch disambiguation are tracked here; pinch-zoom is handled from
//   SDL finger events in Window::_processEvent (so it shares the wheel timing).
// - Native (Dawn) path: SDL_GetMouseState scaled from window-logical to canvas-pixel coords.
// Both paths finish with Renderer::CanvasToLogical so callers see engine logical units.

#include "platform/input/mouseinput_backend.h"

#include "platform/window/window.h"
#include "renderer/renderer.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include "SDL3/SDL.h"
#endif

#ifdef __EMSCRIPTEN__
// Install the pointer-event listeners once. They maintain, on window:
//   _lumiPos  {x,y}  cursor/finger position in canvas pixels
//   _lumiBtns  int   SDL-style button mask (bit0 left, bit1 middle, bit2 right)
// One finger = left button held (pan/paint); two fingers = pinch, so we drop the button (no pan) and
// report the midpoint as the position (zoom anchors at the pinch centre).
static void lumiEnsurePointerHooks() {
    // EM_ASM body is JavaScript passed as C++ tokens, so clang-format splits JS
    // strict-equality into two operators and silently corrupts it. NOTE: the
    // directive below must be exactly "clang-format off" — trailing text disables it.
    // clang-format off
    EM_ASM({
        if (window._lumiPtrInit)
            return;
        window._lumiPtrInit = true;
        window._lumiPos     = ({ x : 0, y : 0 });
        window._lumiBtns    = 0;
        var pressed         = new Map(); // pointerId -> {x,y,type,buttons}
        var c0              = document.querySelector('#canvas');
        if (c0)
            c0.style.touchAction = 'none'; // don't let the browser eat drags for scroll/zoom

        function canvasXY(e) {
            var c = document.querySelector('#canvas');
            if (!c)
                return null;
            var r  = c.getBoundingClientRect();
            var sx = r.width > 0 ? c.width / r.width : 1.0;
            var sy = r.height > 0 ? c.height / r.height : 1.0;
            return ({ x : (e.clientX - r.left) * sx, y : (e.clientY - r.top) * sy });
        }
        function recompute() {
            var touches   = [];
            var hasMouse  = false;
            var mouseBtns = 0;
            pressed.forEach(function(p) {
                if (p.type === 'mouse') {
                    hasMouse  = true;
                    mouseBtns = p.buttons;
                } else
                    touches.push(p);
            });
            var mask = 0;
            // Pointer e.buttons: 1=left,2=right,4=middle → SDL mask: 1=left,2=middle,4=right.
            if (hasMouse) {
                if (mouseBtns & 1)
                    mask |= 1;
                if (mouseBtns & 2)
                    mask |= 4;
                if (mouseBtns & 4)
                    mask |= 2;
            }
            if (touches.length === 1)
                mask |= 1; // single finger acts as the left button
            if (touches.length >= 2)
                window._lumiPos = ({ x : (touches[0].x + touches[1].x) / 2,
                    y : (touches[0].y + touches[1].y) / 2 });
            window._lumiBtns = mask;
        }
        window.addEventListener(
            'pointerdown', function(e) {
                var xy = canvasXY(e);
                if (!xy)
                    return;
                pressed.set(e.pointerId, { x : xy.x, y : xy.y, type : e.pointerType, buttons : e.buttons });
                window._lumiPos = xy;
                recompute();
            },
            true);
        window.addEventListener(
            'pointermove', function(e) {
                var xy = canvasXY(e);
                if (!xy)
                    return;
                if (pressed.has(e.pointerId)) {
                    var p     = pressed.get(e.pointerId);
                    p.x       = xy.x;
                    p.y       = xy.y;
                    p.buttons = e.buttons;
                }
                var touchCount = 0;
                pressed.forEach(function(p) { if (p.type !== 'mouse') touchCount++; });
                if (touchCount < 2)
                    window._lumiPos = xy; // pinch midpoint is set in recompute()
                recompute();
            },
            true);
        function up(e) {
            pressed.delete(e.pointerId);
            recompute();
        }
        window.addEventListener('pointerup', up, true);
        window.addEventListener('pointercancel', up, true);
    });
    // clang-format on
}
#endif

vf2d PlatformInputBackend::_getMousePosition() {
#ifdef __EMSCRIPTEN__
    lumiEnsurePointerHooks();
    static float cx = 0.0f, cy = 0.0f;
    // clang-format off
    EM_ASM({
        setValue($0, window._lumiPos ? window._lumiPos.x : 0, 'float');
        setValue($1, window._lumiPos ? window._lumiPos.y : 0, 'float');
    },
        &cx, &cy);
    // clang-format on
    return Renderer::CanvasToLogical(cx, cy);
#else
    float cx = 0.0f, cy = 0.0f;
    SDL_GetMouseState(&cx, &cy);
    int sdlW = 0, sdlH = 0;
    SDL_GetWindowSize(Window::GetWindow(), &sdlW, &sdlH);
    const uint32_t canvasW = Renderer::GetCanvasWidth();
    const uint32_t canvasH = Renderer::GetCanvasHeight();
    if (sdlW > 0 && sdlH > 0 && canvasW > 0 && canvasH > 0) {
        cx = cx * (float)canvasW / (float)sdlW;
        cy = cy * (float)canvasH / (float)sdlH;
    }
    return Renderer::CanvasToLogical(cx, cy);
#endif
}

uint32_t PlatformInputBackend::_getMouseButtons() {
#ifdef __EMSCRIPTEN__
    lumiEnsurePointerHooks();
    // clang-format off
        return (uint32_t)EM_ASM_INT({ return window._lumiBtns || 0; });
    // clang-format on
#else
    return (uint32_t)SDL_GetMouseState(nullptr, nullptr);
#endif
}
