// Example 15 — Mandelbrot (GPU, perturbation deep zoom)
// ---------------------------------------------------------------------------
// A GPU Mandelbrot that zooms far past f32 precision using perturbation theory:
//   * the HOST iterates one reference orbit (the view centre) in double precision each frame and
//     uploads it as a storage buffer
//   * a compute shader renders every pixel as a tiny f32 DELTA from that reference
//     (dz_{n+1} = 2*Z_n*dz + dz^2 + dc) into an HDR texture, shown with one Draw::Texture
// So per-pixel math stays cheap f32, but zoom depth is bounded only by the host's doubles.
//
// Controls:  drag = pan   scroll = zoom (toward the cursor)
//
// Assets: assets/shaders/mandel_perturb.comp (GLSL compute).

#include "luminoveau.h"
#include "app/lumi.h"
#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <vector>

const int MAXREF = 8192;           // reference-orbit length (deeper zoom wants more)
int       TEXW = 1600, TEXH = 900; // render resolution — tracks the window size (1:1, no upscale blur)

// ── Double-double (two-double) arithmetic — ~32 digits, so the view centre can be placed far past
// double precision. CPU IEEE + real FMA make this exact/reliable (unlike GPU float-float). ──
struct DD {
    double hi, lo;
};
static inline DD ddSet(double a) { return { a, 0.0 }; }
static inline DD ddAdd(DD a, DD b) {
    double s = a.hi + b.hi, v = s - a.hi, e = (a.hi - (s - v)) + (b.hi - v);
    e += a.lo + b.lo;
    double hi = s + e, lo = e - (hi - s);
    return { hi, lo };
}
static inline DD ddSub(DD a, DD b) { return ddAdd(a, { -b.hi, -b.lo }); }
static inline DD ddMul(DD a, DD b) {
    double p = a.hi * b.hi, e = std::fma(a.hi, b.hi, -p);
    e += a.hi * b.lo + a.lo * b.hi;
    double hi = p + e, lo = e - (hi - p);
    return { hi, lo };
}

ComputePipelineAsset mandelPipe;
TextureAsset         outTex;
GpuBufferHandle      refBuf = 0;
std::vector<float>   refData(MAXREF * 2);

double mbScale = 1.0, mbTargetScale = 1.0; // texture-pixels per complex unit (double is fine)
DD     mbCenterX = { -0.5, 0.0 }, mbCenterY = { 0.0, 0.0 };
DD     mbTargetCenterX = { -0.5, 0.0 }, mbTargetCenterY = { 0.0, 0.0 };
bool   panning = false;
double lastMtx = 0.0, lastMty = 0.0;

struct U {
    uint32_t outW, outH, refLen, _pad;
    float    invScale, _p0, _p1, _p2;
};

double dmix(double a, double b, double t) { return a + (b - a) * t; }
// Lerp a dd toward a target by t (t small double): a + (b - a)*t.
DD ddLerp(DD a, DD b, double t) { return ddAdd(a, ddMul(ddSub(b, a), ddSet(t))); }

// Iterate the reference orbit c=(cx,cy) in double-double; store Z_n as float pairs. Returns count.
int ComputeReference(DD cx, DD cy) {
    DD  zx = { 0.0, 0.0 }, zy = { 0.0, 0.0 };
    int n = 0;
    for (n = 0; n < MAXREF; n++) {
        refData[2 * n]     = (float)zx.hi;
        refData[2 * n + 1] = (float)zy.hi;
        DD zx2 = ddMul(zx, zx), zy2 = ddMul(zy, zy);
        DD nx = ddAdd(ddSub(zx2, zy2), cx);                  // zx^2 - zy^2 + cx
        DD ny = ddAdd(ddMul(ddSet(2.0), ddMul(zx, zy)), cy); // 2*zx*zy + cy
        zx    = nx;
        zy    = ny;
        if (zx.hi * zx.hi + zy.hi * zy.hi > 1e12) {
            n++;
            break;
        } // reference escaped
    }
    return n;
}

// (Re)allocate the render texture to match a target size, releasing the old one.
void EnsureTexture(int w, int h) {
    if (outTex.gpuTexture && TEXW == w && TEXH == h)
        return;
    if (outTex.gpuTexture)
        Renderer::GetGpu().ReleaseTexture(outTex.gpuTexture);
    TEXW              = std::max(1, w);
    TEXH              = std::max(1, h);
    outTex            = AssetHandler::CreateEmptyTexture({ (float)TEXW, (float)TEXH }, GpuTextureFormat::R16G16B16A16_Float);
    outTex.gpuSampler = Renderer::GetSampler(ScaleMode::Linear);
}

Lumi::Result AppInit(void **appstate, int argc, char *argv[]) {
    Window::InitWindow("Luminoveau Example — Mandelbrot", 900, 560, 1, SDL_WINDOW_RESIZABLE);
    Renderer::ClearBackground({ 0, 0, 0, 255 });

    EnsureTexture(Window::GetWidth(), Window::GetHeight());
    mbScale = mbTargetScale = std::max((double)TEXW / 3.5, (double)TEXH / 2.5); // fit the full set

    refBuf     = Compute::CreateBuffer((uint32_t)(MAXREF * 2 * sizeof(float)), GpuBufferUsage::StorageRead);
    mandelPipe = AssetHandler::GetComputePipeline("assets/shaders/mandel_perturb.comp");
    return Lumi::Result::Continue;
}

Lumi::Result AppIterate(void *appstate) {
    float dt = std::min((float)Window::GetFrameTime(), 0.1f);

    EnsureTexture(Window::GetWidth(), Window::GetHeight()); // match render res to the window (1:1)

    // Texture is 1:1 with the window, so mouse pixels ARE texture pixels.
    vf2d   mouse = Input::GetMousePosition();
    double mtx = mouse.x, mty = mouse.y;

    if (Input::MouseButtonPressed(SDL_BUTTON_LEFT)) {
        panning = true;
        lastMtx = mtx;
        lastMty = mty;
    }
    if (Input::MouseButtonReleased(SDL_BUTTON_LEFT))
        panning = false;
    if (panning && mbScale > 0.0) {
        double dx = (mtx - lastMtx) / mbScale, dy = (mty - lastMty) / mbScale;
        mbCenterX       = ddSub(mbCenterX, ddSet(dx));
        mbCenterY       = ddSub(mbCenterY, ddSet(dy));
        mbTargetCenterX = ddSub(mbTargetCenterX, ddSet(dx));
        mbTargetCenterY = ddSub(mbTargetCenterY, ddSet(dy));
        lastMtx         = mtx;
        lastMty         = mty;
    }

    float factor = 0.0f;
    if (Input::MouseScrolledUp())
        factor = 1.15f;
    if (Input::MouseScrolledDown())
        factor = 1.0f / 1.15f;
    if (factor > 0.0f && mbScale > 0.0) {
        double areaX = TEXW * 0.5, areaY = TEXH * 0.5;
        // Complex point under the cursor now (from the *live* centre), then re-anchor the target
        // centre so that same point stays under the cursor at the new scale.
        DD cxM          = ddAdd(mbCenterX, ddSet((mtx - areaX) / mbScale));
        DD cyM          = ddAdd(mbCenterY, ddSet((mty - areaY) / mbScale));
        mbTargetScale   = std::max(mbTargetScale * (double)factor, 10.0);
        mbTargetCenterX = ddSub(cxM, ddSet((mtx - areaX) / mbTargetScale));
        mbTargetCenterY = ddSub(cyM, ddSet((mty - areaY) / mbTargetScale));
    }

    double t  = 1.0 - std::exp(-12.0 * (double)dt);
    mbScale   = dmix(mbScale, mbTargetScale, t);
    mbCenterX = ddLerp(mbCenterX, mbTargetCenterX, t);
    mbCenterY = ddLerp(mbCenterY, mbTargetCenterY, t);

    int refLen = ComputeReference(mbCenterX, mbCenterY);
    Compute::UploadBufferData(refBuf, refData.data(), (uint32_t)(refLen * 2 * sizeof(float)));

    U u {};
    u.outW     = TEXW;
    u.outH     = TEXH;
    u.refLen   = (uint32_t)refLen;
    u.invScale = (float)(1.0 / mbScale);

    Window::StartFrame();
    Compute::SetPipeline(mandelPipe);
    Compute::BindReadBuffer(0, refBuf);
    Compute::BindReadWriteTexture(0, outTex); // set1 binding0 (write-only display image)
    Compute::PushUniform(0, &u, sizeof(u));
    Compute::DispatchAuto(TEXW, TEXH, 1);

    Draw::Texture(outTex, { 0.0f, 0.0f }, { (float)Window::GetWidth(), (float)Window::GetHeight() });
    Window::EndFrame();
    return Lumi::Result::Continue;
}

Lumi::Result AppEvent(void *appstate, SDL_Event *event) { return Lumi::Result::Continue; }
void         AppQuit(void *appstate, Lumi::Result result) { Window::Close(); }
