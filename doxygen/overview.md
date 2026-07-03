# Luminoveau

Luminoveau is a small, opinionated C++ game engine. It wraps SDL3's GPU API (and a WebGPU backend
for the browser) behind a clean, static, module-style API so you can go from an empty file to a
running window in a dozen lines — in 2D or 3D, on desktop or the web.

You don't subclass anything or run your own loop. You implement four free functions; the engine owns
`main`, the window, and the frame loop, and calls you at the right moments.

## The application lifecycle

Every Luminoveau program is these four callbacks:

```cpp
#include "luminoveau.h"

Lumi::Result AppInit(void** appstate, int argc, char* argv[]) {
    Window::InitWindow("My Game", 1280, 720, 1);
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void* appstate) {
    Window::StartFrame();
    Text::DrawText(AssetHandler::GetDefaultFont(), {10, 10}, "Hello, world!", WHITE);
    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void* appstate, SDL_Event* event) { return Lumi::Result::Continue; }
void AppQuit(void* appstate, Lumi::Result result) {}
```

- **`AppInit`** runs once at startup. Open the window and load assets here.
- **`AppIterate`** runs once per frame, between `Window::StartFrame()` and `Window::EndFrame()`.
  Return `Lumi::Result::Continue` to keep running, or `Lumi::Result::Success` to exit.
- **`AppEvent`** runs once per incoming SDL event (input, window, quit).
- **`AppQuit`** runs once on shutdown. Save and free here.

`appstate` is an optional pointer you own: set `*appstate` in `AppInit` and it's handed back to the
other callbacks, so you can avoid globals if you prefer.

## How it fits together

The engine is a set of **static modules** — you call them by name, there's no context object to thread
around:

- **Window** — the window, the frame loop (`StartFrame`/`EndFrame`), timing (`GetFrameTime`).
- **Draw** — immediate-mode 2D: shapes, sprites, and tagging draws with post-process effects.
- **Text** — MSDF text rendering at any scale.
- **Input** — keyboard, mouse, and gamepad state.
- **Audio** — music and sound playback.
- **AssetHandler** — loads and caches textures, fonts, shaders, sounds, and models by path.
- **Renderer** — the render passes, framebuffers, MSAA, and sprite render targets underneath Draw.
- **Scene** / **Camera3D** — 3D scenes, models, lighting, and shadows.
- **Particles** — GPU-simulated particle systems.
- **Effects** — full-screen fragment-shader effects stacked over the framebuffer.

Assets are handles you copy freely; the `AssetHandler` owns the real data and hands back the same
cached instance for the same path.

## Conventions worth knowing

- **2D coordinates**: origin top-left, `+x` right, `+y` **down**, in pixels.
- **3D is left-handed**: `+x` east, `+y` up, `+z` north; the camera looks down `+z`.
- **`GetFrameTime()`** is delta time in seconds. It's `0` on the very first frame and clamped
  thereafter, so a slow startup or a hitch can't dump a huge `dt` into your simulation.

## Where to go next

Browse the modules in the sidebar — **Window**, **Draw**, **Input**, **Audio**, and **AssetHandler**
are the ones you'll reach for first. The **Examples** page has runnable demos with full source for
each of these.
