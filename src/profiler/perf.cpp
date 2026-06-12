#include "profiler/perf.h"

#include "platform/window/window.h"
#include "assets/assethandler.h"
#include "draw/draw.h"
#include "draw/text.h"
#include "types/color.h"
#include "renderer/renderer.h"
#include "renderer/passes/spriterenderpass.h"
#include "gpu/presets.h"

static const char *kPerfPass = "__perfOverlay__";
static const char *kPerfFB   = "__perfOverlayFB__";

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
  #include <psapi.h>
  #include <dxgi.h>
  #undef DrawText            // windows.h defines DrawText -> DrawTextA, clobbering Text::DrawText
#elif defined(__APPLE__)
  #include <sys/sysctl.h>
  extern "C" const char *lumi_metal_gpu_name();   // implemented in perf_metal.mm
#elif defined(__linux__)
  #include <unistd.h>
  #include <dlfcn.h>
  #include <cstdint>
#endif

// ── Hardware names (CPU/GPU), queried once + cached ───────────────────────────
namespace {

std::string queryCpuName() {
#if defined(_WIN32)
    char name[256] = {0}; DWORD sz = sizeof(name);
    if (RegGetValueA(HKEY_LOCAL_MACHINE,
                     "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     "ProcessorNameString", RRF_RT_REG_SZ, nullptr, name, &sz) == ERROR_SUCCESS)
        return name;
    return "Unknown CPU";
#elif defined(__APPLE__)
    char buf[256] = {0}; size_t sz = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &sz, nullptr, 0) == 0 && buf[0]) return buf;
    return "Unknown CPU";
#elif defined(__linux__)
    if (FILE *f = std::fopen("/proc/cpuinfo", "r")) {
        char line[512]; std::string fallback;
        while (std::fgets(line, sizeof(line), f)) {
            bool model = std::strncmp(line, "model name", 10) == 0;
            bool alt   = std::strncmp(line, "Model", 5) == 0 || std::strncmp(line, "Hardware", 8) == 0;
            if (model || (alt && fallback.empty())) {
                const char *c = std::strchr(line, ':');
                if (c) {
                    std::string v = c + 1;
                    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
                    while (!v.empty() && (v.back() == '\n' || v.back() == ' ')) v.pop_back();
                    if (model) { std::fclose(f); return v; }
                    if (fallback.empty()) fallback = v;
                }
            }
        }
        std::fclose(f);
        if (!fallback.empty()) return fallback;
    }
    return "Unknown CPU";
#else
    return "Unknown CPU";
#endif
}

std::string queryGpuName() {
#if defined(_WIN32)
    IDXGIFactory1 *factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory)) && factory) {
        IDXGIAdapter1 *adapter = nullptr; std::string best;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(adapter->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                char utf8[256] = {0};
                WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, utf8, sizeof(utf8), nullptr, nullptr);
                best = utf8;
            }
            adapter->Release(); adapter = nullptr;
            if (!best.empty()) break;
        }
        factory->Release();
        if (!best.empty()) return best;
    }
    return "Unknown GPU";
#elif defined(__APPLE__)
    const char *n = lumi_metal_gpu_name();
    return (n && n[0]) ? std::string(n) : std::string("Unknown GPU");
#elif defined(__linux__)
    void *vk = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!vk) vk = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!vk) return "Unknown GPU";
    auto vkCreateInstance  = (int (*)(const void *, const void *, void **))dlsym(vk, "vkCreateInstance");
    auto vkDestroyInstance = (void (*)(void *, const void *))dlsym(vk, "vkDestroyInstance");
    auto vkEnumerate       = (int (*)(void *, uint32_t *, void **))dlsym(vk, "vkEnumeratePhysicalDevices");
    auto vkGetInstanceProc = (void *(*)(void *, const char *))dlsym(vk, "vkGetInstanceProcAddr");
    std::string result = "Unknown GPU";
    if (vkCreateInstance && vkEnumerate && vkGetInstanceProc) {
        struct AppInfo { int sType; const void *pNext; const char *an; uint32_t av;
                         const char *en; uint32_t ev; uint32_t api; } app{0, nullptr, nullptr, 0, nullptr, 0, (1u<<22)};
        struct CI { int sType; const void *pNext; uint32_t flags; const void *app;
                    uint32_t lc; const char *const *l; uint32_t ec; const char *const *e; }
            ci{1, nullptr, 0, &app, 0, nullptr, 0, nullptr};
        void *inst = nullptr;
        if (vkCreateInstance(&ci, nullptr, &inst) == 0 && inst) {
            auto vkGetProps = (void (*)(void *, void *))vkGetInstanceProc(inst, "vkGetPhysicalDeviceProperties");
            uint32_t count = 0; vkEnumerate(inst, &count, nullptr);
            if (count && vkGetProps) {
                if (count > 8) count = 8;
                void *devs[8] = {nullptr}; vkEnumerate(inst, &count, devs);
                for (uint32_t i = 0; i < count && devs[i]; i++) {
                    unsigned char props[1024] = {0}; vkGetProps(devs[i], props);
                    uint32_t type = *reinterpret_cast<uint32_t *>(props + 16);   // deviceType
                    const char *name = reinterpret_cast<const char *>(props + 20);  // deviceName[256]
                    if (name[0]) { result = name; if (type == 2) break; }        // 2 = discrete GPU
                }
            }
            if (vkDestroyInstance) vkDestroyInstance(inst, nullptr);
        }
    }
    dlclose(vk);
    return result;
#else
    return "Unknown GPU";
#endif
}

const std::string &cpuName() { static std::string s = queryCpuName(); return s; }
const std::string &gpuName() { static std::string s = queryGpuName(); return s; }

} // namespace

static std::chrono::high_resolution_clock::time_point s_lastEnd{};
static bool s_haveLast = false;

// Snap a ceiling UP to a "nice" step (~1/20th of the decade) so the axis quantizes into stable
// bands instead of wobbling every frame. e.g. 120-125 -> 125, 126-150 -> 150 (25-fps steps);
// 8.x ms -> 10 ms. Keeps the line near the top of a steady, readable range.
static float steppedCeil(float v) {
    if (v <= 0.0f) return 1.0f;
    float e    = std::floor(std::log10(v));
    float base = std::pow(10.0f, e);
    float n    = v / base;                                  // 1 .. 10
    float step = (n < 1.5f) ? 0.25f : (n < 3.0f) ? 0.5f : (n < 6.0f) ? 1.0f : 2.0f;
    step *= base;
    return std::ceil(v / step) * step;
}

void Perf::_frameStart() {
    _cpuStart = std::chrono::high_resolution_clock::now();
}

void Perf::_frameEnd() {
    auto now = std::chrono::high_resolution_clock::now();
    auto ms  = [](auto d) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count() / 1.0e6;
    };
    _cpuMs = ms(now - _cpuStart);
    (void)s_lastEnd; (void)s_haveLast;
    // Frame-time samples come from the renderer (per present) via ReportFrameMs, so the graph
    // matches the present-based FPS. CPU ms + RAM are sampled here.
    if (--_ramThrottle <= 0) { _ramMB = _queryRamMB(); _ramThrottle = 30; }
}

void Perf::_pushFrame(float ms) {
    _frameMs[_head] = ms;
    _cpuHist[_head] = (float)_cpuMs;
    _gpuHist[_head] = (float)_gpuMs;
    _head = (_head + 1) % kHist;
    if (_count < kHist) _count++;
}

double Perf::_queryRamMB() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
    return 0.0;
#elif defined(__linux__)
    long pages = 0, rss = 0;
    if (FILE *f = std::fopen("/proc/self/statm", "r")) {
        if (std::fscanf(f, "%ld %ld", &pages, &rss) != 2) rss = 0;
        std::fclose(f);
    }
    return (double)rss * (double)sysconf(_SC_PAGESIZE) / (1024.0 * 1024.0);
#else
    return _ramMB;
#endif
}

void Perf::_initOverlay() {
    // Own render-to-screen framebuffer, created after the app's framebuffers so it composites
    // last (on top of everything). Mirrors a typical HUD-layer setup.
    Renderer::CreateFrameBuffer(kPerfFB);
    Renderer::SetFramebufferRenderToScreen(kPerfFB, true);
    auto *pass = new SpriteRenderPass();
    pass->UpdateRenderPassBlendState(GpuPresets::AlphaBlendPreserveAlpha);
    pass->init(Renderer::GetGpu().getSwapchainFormat(), Window::GetWidth(), Window::GetHeight(), kPerfPass);
    pass->color_target_info_loadop = GpuLoadOp::Clear;   // transparent each frame
    pass->color_target_clear_r = pass->color_target_clear_g =
    pass->color_target_clear_b = pass->color_target_clear_a = 0.0f;
    Renderer::AttachRenderPassToFrameBuffer(pass, kPerfPass, kPerfFB);
    _fbReady = true;
}

void Perf::_render() {
    if (!_visible || _count == 0) return;
    if (!_fbReady) _initOverlay();
    Draw::SetTargetRenderPass(kPerfPass);

    // Stats over the history window.
    double sum = 0.0, mn = 1e9, mx = 0.0, maxCpu = 0.0, maxGpu = 0.0;
    float sorted[kHist];
    for (int i = 0; i < _count; i++) {
        float v = _frameMs[i];
        sum += v; mn = std::min(mn, (double)v); mx = std::max(mx, (double)v);
        maxCpu = std::max(maxCpu, (double)_cpuHist[i]);
        maxGpu = std::max(maxGpu, (double)_gpuHist[i]);
        sorted[i] = v;
    }
    std::sort(sorted, sorted + _count);   // ascending frame ms
    // 1% / 0.1% low FPS: average the slowest N% of frames and express as FPS — the standard
    // "how bad the hitches get" metric, not a single percentile sample.
    auto lowFps = [&](double frac) {
        int n = std::max(1, (int)std::ceil(_count * frac));
        double s = 0.0;
        for (int k = _count - n; k < _count; k++) s += sorted[k];
        s /= n;
        return s > 0.01 ? 1000.0 / s : 0.0;
    };
    double low1Fps  = lowFps(0.01);
    double low01Fps = lowFps(0.001);
    (void)sum; (void)mx;
    // FPS from the engine's present counter (authoritative — the SDL callback loop can spin /
    // skip presents, which poisons a per-iteration frame-time average).
    double fps = (double)Window::GetFPS(250.0f);
    double avg = fps > 0.0001 ? 1000.0 / fps : 0.0;             // present-based frame ms

    // Panel layout (top-left, logical coords).
    const float pad = 8.0f, ts = 17.0f, line = 20.0f;
    const float panelW = 340.0f, graphH = 46.0f;
    const float headerH = line * 5.0f + pad;                  // 5 text rows
    const float namesH  = line * 2.0f;                        // CPU + GPU name rows (bottom)
    const float panelH = headerH + (graphH + pad) * 3.0f + namesH + pad * 2.0f;  // fps + cpu + gpu graphs
    const vf2d  org{ 8.0f, 8.0f };

    auto F = AssetHandler::GetDefaultFont();
    const Color bg     {  1,  17,  28, 210 };
    const Color frameC {  90, 200, 255, 255 };   // frame-time / fps (cyan)
    const Color cpuC   { 120, 230, 140, 255 };   // cpu              (green)
    const Color gpuC   { 255, 170,  70, 255 };   // gpu              (orange)
    const Color memC   { 200, 170, 255, 255 };   // ram/vram         (violet)
    const Color drawC  { 230, 220, 140, 255 };   // draws/tris       (yellow)
    const Color gridC  {  40,  70,  90, 255 };   // guide lines
    const Color labelC { 150, 180, 200, 255 };

    Draw::RectangleFilled(org, { panelW, panelH }, bg);

    char buf[96], lbl[16];
    float tx = org.x + pad, ty = org.y + pad;

    std::snprintf(buf, sizeof(buf), "FPS %.0f   %.2f ms", fps, avg);
    Text::DrawText(F, { tx, ty }, buf, frameC, ts); ty += line;
    std::snprintf(buf, sizeof(buf), "1%% low %.0f   0.1%% low %.0f", low1Fps, low01Fps);
    Text::DrawText(F, { tx, ty }, buf, labelC, ts); ty += line;
    std::snprintf(buf, sizeof(buf), "CPU %.2f ms", _cpuMs);
    Text::DrawText(F, { tx, ty }, buf, cpuC, ts);
    std::snprintf(buf, sizeof(buf), "GPU %.2f ms", _gpuMs);
    Text::DrawText(F, { tx + 165.0f, ty }, buf, gpuC, ts); ty += line;
    std::snprintf(buf, sizeof(buf), "RAM %.0f MB   VRAM %.0f MB", _ramMB, _vramBytes / (1024.0 * 1024.0));
    Text::DrawText(F, { tx, ty }, buf, memC, ts); ty += line;
    std::snprintf(buf, sizeof(buf), "draws %u   tris %.0fk", _drawCalls, (_drawVerts / 3) / 1000.0);
    Text::DrawText(F, { tx, ty }, buf, drawC, ts); ty += line;

    const float gx = org.x + pad;
    const float gw = panelW - pad * 2.0f - 40.0f;   // room for scale labels on the right
    float dx = gw / (float)(kHist - 1);

    // One labelled graph per metric. fpsMode plots 1000/ms (else the value directly). top is a
    // stepped ceiling so the axis is stable. Right-side labels: top, mid, and the unit.
    auto drawGraph = [&](float gy, const float *hist, Color c, float top, bool fpsMode, const char *unit) {
        Draw::Line({ gx, gy }, { gx + gw, gy }, gridC);
        Draw::Line({ gx, gy + graphH * 0.5f }, { gx + gw, gy + graphH * 0.5f }, gridC);
        auto mapv = [&](float ms) { return fpsMode ? (ms > 0.01f ? 1000.0f / ms : 0.0f) : ms; };
        auto yOf  = [&](float ms) { return gy + graphH * (1.0f - std::min(mapv(ms), top) / top); };
        for (int i = 1; i < _count; i++) {
            int i0 = (_head + (kHist - _count) + (i - 1)) % kHist;
            int i1 = (_head + (kHist - _count) + i) % kHist;
            Draw::Line({ gx + dx * (i - 1), yOf(hist[i0]) }, { gx + dx * i, yOf(hist[i1]) }, c);
        }
        const char *fmt = (top >= 10.0f) ? "%.0f" : "%.1f";
        std::snprintf(lbl, sizeof(lbl), fmt, top);        Text::DrawText(F, { gx + gw + 4.0f, gy - 7.0f }, lbl, c, 12.0f);
        std::snprintf(lbl, sizeof(lbl), fmt, top * 0.5f); Text::DrawText(F, { gx + gw + 4.0f, gy + graphH * 0.5f - 7.0f }, lbl, labelC, 12.0f);
        Text::DrawText(F, { gx + gw + 4.0f, gy + graphH - 11.0f }, unit, c, 10.0f);
    };

    float gy = ty + pad;
    float peakFps = (mn > 0.01) ? 1000.0f / (float)mn : 60.0f;
    drawGraph(gy, _frameMs, frameC, std::max(steppedCeil(peakFps * 1.05f), 30.0f), true,  "fps"); gy += graphH + pad;
    drawGraph(gy, _cpuHist, cpuC,   std::max(steppedCeil((float)maxCpu * 1.1f), 1.0f), false, "cpu"); gy += graphH + pad;
    drawGraph(gy, _gpuHist, gpuC,   std::max(steppedCeil((float)maxGpu * 1.1f), 1.0f), false, "gpu"); gy += graphH + pad;

    // Hardware names (bottom); GPU line shows the graphics API too.
    float ny = gy;
    Text::DrawText(F, { org.x + pad, ny }, ("CPU: " + cpuName()).c_str(), labelC, 14.0f); ny += line;
    std::snprintf(buf, sizeof(buf), "GPU: %s [%s]", gpuName().c_str(), Renderer::GetGpu().backendName());
    Text::DrawText(F, { org.x + pad, ny }, buf, labelC, 14.0f);

    Draw::ResetTargetRenderPass();   // back to the app default
}
