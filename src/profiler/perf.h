#pragma once
#include <chrono>
#include <cstdint>

/**
 * Lightweight performance HUD. Custom-drawn (no ImGui) with Draw::/Text:: into the current
 * frame: a translucent panel, a frame-time line graph with scale labels, and FPS / CPU ms /
 * GPU ms / RAM / VRAM readouts. Driven by the engine frame loop:
 *   Window::_startFrame() -> Perf::FrameStart()   (marks the CPU-work start)
 *   Window::_endFrame()   -> Perf::FrameEnd()     (computes CPU ms, samples, draws if visible)
 * GPU ms and VRAM are pushed in by the renderer/backend via ReportGPUms / ReportVRAM.
 */
class Perf {
public:
    static void FrameStart()              { get()._frameStart(); }
    static void FrameEnd()                { get()._frameEnd(); }
    // Real per-present frame time (ms), pushed by the renderer so the graph matches the FPS.
    static void ReportFrameMs(double ms)  { get()._pushFrame((float)ms); }
    // Draw the HUD (no-op unless visible). Call from the app where its 2D layer composites to
    // the final target — the engine frame loop can leave a custom target-render-pass set.
    static void Render()                  { get()._render(); }
    static void ReportGPUms(double ms)    { get()._gpuMs = ms; }
    static void ReportVRAM(int64_t bytes) { get()._vramBytes = bytes; }
    static void ReportDraws(uint32_t calls, uint64_t verts) { get()._drawCalls = calls; get()._drawVerts = verts; }

    static void SetVisible(bool v)        { get()._visible = v; }
    static void Toggle()                  { get()._visible = !get()._visible; }
    static bool Visible()                 { return get()._visible; }

private:
    Perf() = default;
    static Perf &get() { static Perf instance; return instance; }

    void   _frameStart();
    void   _frameEnd();
    void   _pushFrame(float ms);
    void   _render();
    void   _initOverlay();      // lazily create the render-to-screen overlay framebuffer
    double _queryRamMB();

    bool   _fbReady = false;

    static constexpr int kHist = 128;     // frame-time history samples

    bool   _visible = false;   // toggled by the app (lumiquake: F8)
    std::chrono::high_resolution_clock::time_point _cpuStart;
    double _cpuMs = 0.0;
    double _gpuMs = 0.0;                   // set by the renderer (fence timing)
    int64_t _vramBytes = 0;                // set by the GPU backend (tracked allocations)
    double _ramMB = 0.0;

    float    _frameMs[kHist] = {0.0f};
    float    _cpuHist[kHist] = {0.0f};
    float    _gpuHist[kHist] = {0.0f};
    int      _head = 0;
    int      _count = 0;
    int      _ramThrottle = 0;             // RAM query is mildly costly; sample periodically
    uint32_t _drawCalls = 0;
    uint64_t _drawVerts = 0;
};
