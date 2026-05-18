// Persistence-of-Vision: samples the accumulation texture and scales by vScale.
// Decay pass:      vScale = decay factor (e.g. 0.92) — fades old trails
// Composite pass:  vScale = 1.0 — blits accumulation to swapchain
// vScale is forwarded from the vertex shader to avoid fragment uniform binding issues.

Texture2D    gPovTex  : register(t0, space2);
SamplerState gSampler : register(s0, space2);

struct Input {
    float2                vUV    : TEXCOORD0;
    nointerpolation float vScale : TEXCOORD1;
};

float4 main(Input i) : SV_Target
{
    return gPovTex.Sample(gSampler, i.vUV) * i.vScale;
}
