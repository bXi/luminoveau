// dear imgui: Renderer Backend for SDL_Renderer for SDL3
// (Requires: SDL 3.0.0+)

// Note how SDL_Renderer is an _optional_ component of SDL3.
// For a multi-platform app consider using e.g. SDL+DirectX on Windows and SDL+OpenGL on Linux/OSX.
// If your application will want to render any non trivial amount of graphics other than UI,
// please be aware that SDL_Renderer currently offers a limited graphic API to the end-user and
// it might be difficult to step out of those boundaries.

// Implemented features:
//  [X] Renderer: User texture binding. Use 'SDL_Texture*' as ImTextureID. Read the FAQ about ImTextureID!
//  [X] Renderer: Large meshes support (64k+ vertices) with 16-bit indices.
// Missing features:
//  [ ] Renderer: Multi-viewport support (multiple windows).

// You can copy and use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
//

#include "imgui.h"

#ifndef IMGUI_DISABLE


#include "imgui_impl_sdlgpu3.h"
#include "imgui_impl_sdlgpu3_shadercross.h"

#include <stdint.h>

// Clang warnings with -Weverything
#if defined(__clang__)
#pragma clang diagonstic push
#pragma clang diagonstic ignored "-Wsign-conversion"
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#if !SDL_VERSION_ATLEAST(3,0,0)
#error This backend requires SDL 3.0.0+
#endif

#endif

#define MEMALIGN(_SIZE,_ALIGN)        (((_SIZE) + ((_ALIGN) - 1)) & ~((_ALIGN) - 1))    // Memory align (copied from IM_ALIGN() macro).

//-----------------------------------------------------------------------------
// SHADERS
//-----------------------------------------------------------------------------

// backends/sdl3_gpu/glsl_shader.vert, compiled with:
// # glslangValidator -V -x -o glsl_shader.vert.u32 glsl_shader.vert
/*
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

layout (set = 1, binding = 0) uniform vs_params {
    mat4 Trans;
} uniforms;

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out struct {
    vec4 Color;
    vec2 UV;
} Out;

void main()
{
    Out.Color = aColor;
    Out.UV = aUV;
    gl_Position = uniforms.Trans * vec4(aPos.x, aPos.y, 0, 1);
}
*/

static uint32_t __glsl_shader_vert_spv[] =
{
    0x07230203,0x00010000,0x0008000b,0x00000031,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x000a000f,0x00000000,0x00000004,0x6e69616d,0x00000000,0x0000000b,0x0000000f,0x00000015,
    0x0000001b,0x00000023,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
    0x00000000,0x00030005,0x00000009,0x00000000,0x00050006,0x00000009,0x00000000,0x6f6c6f43,
    0x00000072,0x00040006,0x00000009,0x00000001,0x00005655,0x00030005,0x0000000b,0x0074754f,
    0x00040005,0x0000000f,0x6c6f4361,0x0000726f,0x00030005,0x00000015,0x00565561,0x00060005,
    0x00000019,0x505f6c67,0x65567265,0x78657472,0x00000000,0x00060006,0x00000019,0x00000000,
    0x505f6c67,0x7469736f,0x006e6f69,0x00030005,0x0000001b,0x00000000,0x00050005,0x0000001d,
    0x705f7376,0x6d617261,0x00000073,0x00050006,0x0000001d,0x00000000,0x6e617254,0x00000073,
    0x00050005,0x0000001f,0x66696e75,0x736d726f,0x00000000,0x00040005,0x00000023,0x736f5061,
    0x00000000,0x00040047,0x0000000b,0x0000001e,0x00000000,0x00040047,0x0000000f,0x0000001e,
    0x00000002,0x00040047,0x00000015,0x0000001e,0x00000001,0x00050048,0x00000019,0x00000000,
    0x0000000b,0x00000000,0x00030047,0x00000019,0x00000002,0x00040048,0x0000001d,0x00000000,
    0x00000005,0x00050048,0x0000001d,0x00000000,0x00000023,0x00000000,0x00050048,0x0000001d,
    0x00000000,0x00000007,0x00000010,0x00030047,0x0000001d,0x00000002,0x00040047,0x0000001f,
    0x00000022,0x00000001,0x00040047,0x0000001f,0x00000021,0x00000000,0x00040047,0x00000023,
    0x0000001e,0x00000000,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,
    0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040017,0x00000008,
    0x00000006,0x00000002,0x0004001e,0x00000009,0x00000007,0x00000008,0x00040020,0x0000000a,
    0x00000003,0x00000009,0x0004003b,0x0000000a,0x0000000b,0x00000003,0x00040015,0x0000000c,
    0x00000020,0x00000001,0x0004002b,0x0000000c,0x0000000d,0x00000000,0x00040020,0x0000000e,
    0x00000001,0x00000007,0x0004003b,0x0000000e,0x0000000f,0x00000001,0x00040020,0x00000011,
    0x00000003,0x00000007,0x0004002b,0x0000000c,0x00000013,0x00000001,0x00040020,0x00000014,
    0x00000001,0x00000008,0x0004003b,0x00000014,0x00000015,0x00000001,0x00040020,0x00000017,
    0x00000003,0x00000008,0x0003001e,0x00000019,0x00000007,0x00040020,0x0000001a,0x00000003,
    0x00000019,0x0004003b,0x0000001a,0x0000001b,0x00000003,0x00040018,0x0000001c,0x00000007,
    0x00000004,0x0003001e,0x0000001d,0x0000001c,0x00040020,0x0000001e,0x00000002,0x0000001d,
    0x0004003b,0x0000001e,0x0000001f,0x00000002,0x00040020,0x00000020,0x00000002,0x0000001c,
    0x0004003b,0x00000014,0x00000023,0x00000001,0x00040015,0x00000024,0x00000020,0x00000000,
    0x0004002b,0x00000024,0x00000025,0x00000000,0x00040020,0x00000026,0x00000001,0x00000006,
    0x0004002b,0x00000024,0x00000029,0x00000001,0x0004002b,0x00000006,0x0000002c,0x00000000,
    0x0004002b,0x00000006,0x0000002d,0x3f800000,0x00050036,0x00000002,0x00000004,0x00000000,
    0x00000003,0x000200f8,0x00000005,0x0004003d,0x00000007,0x00000010,0x0000000f,0x00050041,
    0x00000011,0x00000012,0x0000000b,0x0000000d,0x0003003e,0x00000012,0x00000010,0x0004003d,
    0x00000008,0x00000016,0x00000015,0x00050041,0x00000017,0x00000018,0x0000000b,0x00000013,
    0x0003003e,0x00000018,0x00000016,0x00050041,0x00000020,0x00000021,0x0000001f,0x0000000d,
    0x0004003d,0x0000001c,0x00000022,0x00000021,0x00050041,0x00000026,0x00000027,0x00000023,
    0x00000025,0x0004003d,0x00000006,0x00000028,0x00000027,0x00050041,0x00000026,0x0000002a,
    0x00000023,0x00000029,0x0004003d,0x00000006,0x0000002b,0x0000002a,0x00070050,0x00000007,
    0x0000002e,0x00000028,0x0000002b,0x0000002c,0x0000002d,0x00050091,0x00000007,0x0000002f,
    0x00000022,0x0000002e,0x00050041,0x00000011,0x00000030,0x0000001b,0x0000000d,0x0003003e,
    0x00000030,0x0000002f,0x000100fd,0x00010038
};

// backends/sdl3_gpu/glsl_shader.frag, compiled with:
// # glslangValidator -V -x -o glsl_shader.frag.u32 glsl_shader.frag
/*
#version 450 core
layout(location = 0) out vec4 fColor;

layout(set=2, binding=0) uniform sampler2D sTexture;

layout(location = 0) in struct {
    vec4 Color;
    vec2 UV;
} In;

void main()
{
    fColor = In.Color * texture(sTexture, In.UV.st);
}
*/
static uint32_t __glsl_shader_frag_spv[] =
{
    0x07230203,0x00010000,0x0008000b,0x0000001e,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0007000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000009,0x0000000d,0x00030010,
    0x00000004,0x00000007,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
    0x00000000,0x00040005,0x00000009,0x6c6f4366,0x0000726f,0x00030005,0x0000000b,0x00000000,
    0x00050006,0x0000000b,0x00000000,0x6f6c6f43,0x00000072,0x00040006,0x0000000b,0x00000001,
    0x00005655,0x00030005,0x0000000d,0x00006e49,0x00050005,0x00000016,0x78655473,0x65727574,
    0x00000000,0x00040047,0x00000009,0x0000001e,0x00000000,0x00040047,0x0000000d,0x0000001e,
    0x00000000,0x00040047,0x00000016,0x00000022,0x00000002,0x00040047,0x00000016,0x00000021,
    0x00000000,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,0x00000006,
    0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040020,0x00000008,0x00000003,
    0x00000007,0x0004003b,0x00000008,0x00000009,0x00000003,0x00040017,0x0000000a,0x00000006,
    0x00000002,0x0004001e,0x0000000b,0x00000007,0x0000000a,0x00040020,0x0000000c,0x00000001,
    0x0000000b,0x0004003b,0x0000000c,0x0000000d,0x00000001,0x00040015,0x0000000e,0x00000020,
    0x00000001,0x0004002b,0x0000000e,0x0000000f,0x00000000,0x00040020,0x00000010,0x00000001,
    0x00000007,0x00090019,0x00000013,0x00000006,0x00000001,0x00000000,0x00000000,0x00000000,
    0x00000001,0x00000000,0x0003001b,0x00000014,0x00000013,0x00040020,0x00000015,0x00000000,
    0x00000014,0x0004003b,0x00000015,0x00000016,0x00000000,0x0004002b,0x0000000e,0x00000018,
    0x00000001,0x00040020,0x00000019,0x00000001,0x0000000a,0x00050036,0x00000002,0x00000004,
    0x00000000,0x00000003,0x000200f8,0x00000005,0x00050041,0x00000010,0x00000011,0x0000000d,
    0x0000000f,0x0004003d,0x00000007,0x00000012,0x00000011,0x0004003d,0x00000014,0x00000017,
    0x00000016,0x00050041,0x00000019,0x0000001a,0x0000000d,0x00000018,0x0004003d,0x0000000a,
    0x0000001b,0x0000001a,0x00050057,0x00000007,0x0000001c,0x00000017,0x0000001b,0x00050085,
    0x00000007,0x0000001d,0x00000012,0x0000001c,0x0003003e,0x00000009,0x0000001d,0x000100fd,
    0x00010038
};

struct ImGui_ImplSDLGPU3_Data {
    SDL_GPUDevice*                  Device;
    SDL_GPUTextureSamplerBinding    FontTexture;
    SDL_GPUBuffer*                  IndexBuffer;
    SDL_GPUBuffer*                  VertexBuffer;
    SDL_GPUTransferBuffer*          IndexTransferBuffer;
    SDL_GPUTransferBuffer*          VertexTransferBuffer;
    SDL_GPUGraphicsPipeline*        Pipeline;

    int                     IndexBufferSize;
    int                     VertexBufferSize;

    SDL_GPUTextureFormat    RenderTextureFormat;

    ImGui_ImplSDLGPU3_Data() { memset((void*)this, 0, sizeof(*this)); }
};

static ImGui_ImplSDLGPU3_Data* ImGui_ImplSDLGPU3_GetBackendData() {
    return ImGui::GetCurrentContext() ? (ImGui_ImplSDLGPU3_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

static void ImGui_ImplSDLGPU3_SetupRenderState(ImDrawData* draw_data, SDL_GPUCommandBuffer* cmd_buf, SDL_GPURenderPass* render_pass) {
    ImGui_ImplSDLGPU3_Data* bd = ImGui_ImplSDLGPU3_GetBackendData();

    // Bind pipeline
    SDL_BindGPUGraphicsPipeline(render_pass, bd->Pipeline);

    // Bind vertex and index buffers
    SDL_GPUBufferBinding vertex_buffer_binding;
    vertex_buffer_binding.buffer = bd->VertexBuffer;
    vertex_buffer_binding.offset = 0;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_buffer_binding, 1);

    SDL_GPUBufferBinding index_buffer_binding;
    index_buffer_binding.buffer = bd->IndexBuffer;
    index_buffer_binding.offset = 0;
    SDL_BindGPUIndexBuffer(render_pass, &index_buffer_binding,
                           sizeof(ImDrawIdx) == 2? SDL_GPU_INDEXELEMENTSIZE_16BIT : SDL_GPU_INDEXELEMENTSIZE_32BIT);

    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    float mvp[4][4] =
        {
            { 2.0f/(R-L),   0.0f,           0.0f,       0.0f },
            { 0.0f,         2.0f/(T-B),     0.0f,       0.0f },
            { 0.0f,         0.0f,           0.5f,       0.0f },
            { (R+L)/(L-R),  (T+B)/(B-T),    0.5f,       1.0f },
        };

    SDL_PushGPUVertexUniformData(cmd_buf, 0, mvp, 16 * sizeof(float));
}

bool ImGui_ImplSDLGPU3_Init(SDL_GPUDevice* device, int render_texture_format) {
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");
    IM_ASSERT(device != nullptr && "SDL_GPUDevice not initialized!");

    ImGui_ImplSDLGPU3_Data* bd = IM_NEW(ImGui_ImplSDLGPU3_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_sdlgpu";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    bd->Device = device;
    bd->FontTexture = {nullptr, nullptr};
    bd->RenderTextureFormat = (SDL_GPUTextureFormat)render_texture_format;

    return true;
}

void ImGui_ImplSDLGPU3_Shutdown() {
    ImGui_ImplSDLGPU3_Data* bd = ImGui_ImplSDLGPU3_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplSDLGPU3_DestroyDeviceObjects();

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    IM_DELETE(bd);
}

void ImGui_ImplSDLGPU3_NewFrame() {
    ImGui_ImplSDLGPU3_Data* bd = ImGui_ImplSDLGPU3_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplSDL3_Init()?");

    if (bd->Pipeline == nullptr)
        ImGui_ImplSDLGPU3_CreateDeviceObjects();
}

void ImGui_ImplSDLGPU3_UploadDrawData(ImDrawData* draw_data, SDL_GPUCopyPass* copy_pass) {
    ImGui_ImplSDLGPU3_Data* bd = ImGui_ImplSDLGPU3_GetBackendData();

    // Create and grow vertex/index buffers if needed
    if (bd->VertexBuffer == nullptr || bd->VertexBufferSize < draw_data->TotalVtxCount)
    {
        if (bd->VertexBuffer) SDL_ReleaseGPUBuffer(bd->Device, bd->VertexBuffer);
        if (bd->VertexTransferBuffer) SDL_ReleaseGPUTransferBuffer(bd->Device, bd->VertexTransferBuffer);

        bd->VertexBufferSize = draw_data->TotalVtxCount + 5000;

        SDL_GPUBufferCreateInfo buffer_create_info = {};
        buffer_create_info.size = (uint32_t)MEMALIGN(bd->VertexBufferSize * sizeof(ImDrawVert), 4);
        buffer_create_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;

        bd->VertexBuffer = SDL_CreateGPUBuffer(bd->Device, &buffer_create_info);
        if (!bd->VertexBuffer)
            return;

        SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info = {};
        transfer_buffer_create_info.size = (uint32_t)MEMALIGN(bd->VertexBufferSize * sizeof(ImDrawVert), 4);
        transfer_buffer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

        bd->VertexTransferBuffer = SDL_CreateGPUTransferBuffer(bd->Device, &transfer_buffer_create_info);
        if (!bd->VertexTransferBuffer)
            return;
    }
    if (bd->IndexBuffer == nullptr || bd->IndexBufferSize < draw_data->TotalIdxCount)
    {
        if (bd->IndexBuffer) SDL_ReleaseGPUBuffer(bd->Device, bd->IndexBuffer);
        if (bd->IndexTransferBuffer) SDL_ReleaseGPUTransferBuffer(bd->Device, bd->IndexTransferBuffer);

        bd->IndexBufferSize = draw_data->TotalIdxCount + 10000;

        SDL_GPUBufferCreateInfo buffer_create_info = {};
        buffer_create_info.size = (uint32_t)MEMALIGN(bd->IndexBufferSize * sizeof(ImDrawVert), 4);
        buffer_create_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;

        bd->IndexBuffer = SDL_CreateGPUBuffer(bd->Device, &buffer_create_info);
        if (!bd->IndexBuffer)
            return;

        SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info = {};
        transfer_buffer_create_info.size = (uint32_t)MEMALIGN(bd->IndexBufferSize * sizeof(ImDrawVert), 4);
        transfer_buffer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

        bd->IndexTransferBuffer = SDL_CreateGPUTransferBuffer(bd->Device, &transfer_buffer_create_info);
        if (!bd->IndexTransferBuffer)
            return;
    }

    // Upload vertex/index data into GPU transfer buffers
    ImDrawVert* vtx_dst = (ImDrawVert*) SDL_MapGPUTransferBuffer(bd->Device, bd->VertexTransferBuffer, true);
    ImDrawIdx* idx_dst = (ImDrawIdx*) SDL_MapGPUTransferBuffer(bd->Device, bd->IndexTransferBuffer, true);
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += cmd_list->VtxBuffer.Size;
        idx_dst += cmd_list->IdxBuffer.Size;
    }
    SDL_UnmapGPUTransferBuffer(bd->Device, bd->VertexTransferBuffer);
    SDL_UnmapGPUTransferBuffer(bd->Device, bd->IndexTransferBuffer);

    // Copy transfer buffer to device-only buffer
    {
        SDL_GPUTransferBufferLocation transfer_buffer_loc = {};
        transfer_buffer_loc.transfer_buffer = bd->VertexTransferBuffer;
        transfer_buffer_loc.offset = 0;

        SDL_GPUBufferRegion buffer_region = {};
        buffer_region.buffer = bd->VertexBuffer,
        buffer_region.offset = 0,
        buffer_region.size = (uint32_t)MEMALIGN(bd->VertexBufferSize * sizeof(ImDrawVert), 4);
        SDL_UploadToGPUBuffer(copy_pass, &transfer_buffer_loc, &buffer_region, true);
    }
    {
        SDL_GPUTransferBufferLocation transfer_buffer_loc = {};
        transfer_buffer_loc.transfer_buffer = bd->IndexTransferBuffer;
        transfer_buffer_loc.offset = 0;

        SDL_GPUBufferRegion buffer_region = {};
        buffer_region.buffer = bd->IndexBuffer,
        buffer_region.offset = 0,
        buffer_region.size = (uint32_t)MEMALIGN(bd->IndexBufferSize * sizeof(ImDrawIdx), 4);
        SDL_UploadToGPUBuffer(copy_pass, &transfer_buffer_loc, &buffer_region, true);
    }
}

void ImGui_ImplSDLGPU3_RenderDrawData(ImDrawData* draw_data, SDL_GPUCommandBuffer* cmd_buf, SDL_GPURenderPass* render_pass) {
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0)
        return;

    // Setup desired render state
    ImGui_ImplSDLGPU3_SetupRenderState(draw_data, cmd_buf, render_pass);

    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    ImVec2 clip_scale = draw_data->FramebufferScale;
    ImVec2 clip_off = draw_data->DisplayPos;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplSDLGPU3_SetupRenderState(draw_data, cmd_buf, render_pass);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                // Bind custom texture
                ImTextureID tex_id = pcmd->GetTexID();
                SDL_GPUTextureSamplerBinding* binding = (SDL_GPUTextureSamplerBinding*)tex_id;
                SDL_BindGPUFragmentSamplers(render_pass, 0, binding, 1);

                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                // Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
                if (clip_min.x < 0.0f) { clip_min.x = 0.0f; }
                if (clip_min.y < 0.0f) { clip_min.y = 0.0f; }
                if (clip_max.x > fb_width) { clip_max.x = (float)fb_width; }
                if (clip_max.y > fb_height) { clip_max.y = (float)fb_height; }
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle
                SDL_Rect clip = {(int)clip_min.x, (int)clip_min.y, (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y)};
                SDL_SetGPUScissor(render_pass, &clip);

                // Draw!
                SDL_DrawGPUIndexedPrimitives(render_pass, pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
            }
        }
        global_idx_offset += cmd_list->IdxBuffer.Size;
        global_vtx_offset += cmd_list->VtxBuffer.Size;
    }
}

bool ImGui_ImplSDLGPU3_CreateFontsTexture() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplSDLGPU3_Data* bd = ImGui_ImplSDLGPU3_GetBackendData();

    // Build texture atlas
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // Create texture and sampler
    {
        SDL_GPUTextureCreateInfo texture_create_info = {};
        texture_create_info.type = SDL_GPU_TEXTURETYPE_2D,
        texture_create_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        texture_create_info.width = (uint32_t)width,
        texture_create_info.height = (uint32_t)height,
        texture_create_info.layer_count_or_depth = 1,
        texture_create_info.num_levels = 1,
        texture_create_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

        bd->FontTexture.texture = SDL_CreateGPUTexture(bd->Device, &texture_create_info);
        SDL_SetGPUTextureName(bd->Device, bd->FontTexture.texture, "Dear ImGui Font Texture");

        SDL_GPUSamplerCreateInfo sampler_create_info = {};
        sampler_create_info.min_filter = SDL_GPU_FILTER_LINEAR;
        sampler_create_info.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampler_create_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler_create_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_create_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_create_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_create_info.max_anisotropy = 1.0f;

        bd->FontTexture.sampler = SDL_CreateGPUSampler(bd->Device, &sampler_create_info);
    }

    // Upload texture data
    {
        SDL_GPUTransferBufferCreateInfo buffer_create_info = {};
        buffer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        buffer_create_info.size = (uint32_t)(width * height * 4);

        SDL_GPUTransferBuffer* texture_transfer_buffer = SDL_CreateGPUTransferBuffer(bd->Device, &buffer_create_info);
        uint8_t* texture_transfer_ptr = (uint8_t*)SDL_MapGPUTransferBuffer(bd->Device, texture_transfer_buffer, false);
        memcpy(texture_transfer_ptr, pixels, width * height * 4);
        SDL_UnmapGPUTransferBuffer(bd->Device, texture_transfer_buffer);

        SDL_GPUCommandBuffer* upload_cmd_buf = SDL_AcquireGPUCommandBuffer(bd->Device);
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(upload_cmd_buf);

        SDL_GPUTextureTransferInfo transfer_texture_info = {};
        transfer_texture_info.transfer_buffer = texture_transfer_buffer;
        transfer_texture_info.offset = 0;

        SDL_GPUTextureRegion transfer_texture_region = {};
        transfer_texture_region.texture = bd->FontTexture.texture;
        transfer_texture_region.w = width;
        transfer_texture_region.h = height;
        transfer_texture_region.d = 1;

        SDL_UploadToGPUTexture(
            copy_pass, &transfer_texture_info, &transfer_texture_region, false);

        SDL_EndGPUCopyPass(copy_pass);

        SDL_SubmitGPUCommandBuffer(upload_cmd_buf);
        SDL_ReleaseGPUTransferBuffer(bd->Device, texture_transfer_buffer);
    }

    // Store texture identifier
    static_assert(sizeof(ImTextureID) == sizeof(void*), "Can't pack descriptor handle into TexID, 32-bit not supported yet.");
    io.Fonts->SetTexID((ImTextureID)&bd->FontTexture);

    return true;
}

void ImGui_ImplSDLGPU3_DestroyFontsTexture() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplSDLGPU3_Data* bd = ImGui_ImplSDLGPU3_GetBackendData();

    if (bd->FontTexture.texture)
        SDL_ReleaseGPUTexture(bd->Device, bd->FontTexture.texture);

    if (bd->FontTexture.sampler)
        SDL_ReleaseGPUSampler(bd->Device, bd->FontTexture.sampler);
}

bool ImGui_ImplSDLGPU3_CreateDeviceObjects() {
    ImGui_ImplSDLGPU3_Data* bd = ImGui_ImplSDLGPU3_GetBackendData();

    // Compile shaders

    SDL_GPUShaderCreateInfo vert_shader_info = {};
    vert_shader_info.code = (uint8_t*)__glsl_shader_vert_spv;
    vert_shader_info.code_size = sizeof(__glsl_shader_vert_spv);
    vert_shader_info.entrypoint = "main";
    vert_shader_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vert_shader_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    vert_shader_info.num_samplers = 0;
    vert_shader_info.num_uniform_buffers = 1;
    vert_shader_info.num_storage_buffers = 0;
    vert_shader_info.num_storage_textures = 0;

    //TODO: fix shader to use shadercross

    // SDL_GPUShader* vert_shader = static_cast<SDL_GPUShader*>(
    //     SDL_ShaderCross_CompileFromSPIRV(bd->Device, &vert_shader_info, false));

    SDL_GPUShader* vert_shader = SDL_CreateGPUShader(bd->Device, &vert_shader_info);
    if (vert_shader == nullptr) {
        return false;
    }

    SDL_GPUShaderCreateInfo frag_shader_info = {};
    frag_shader_info.code = (uint8_t*)__glsl_shader_frag_spv;
    frag_shader_info.code_size = sizeof(__glsl_shader_frag_spv);
    frag_shader_info.entrypoint = "main";
    frag_shader_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    frag_shader_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    frag_shader_info.num_samplers = 1;
    frag_shader_info.num_uniform_buffers = 0;
    frag_shader_info.num_storage_buffers = 0;
    frag_shader_info.num_storage_textures = 0;

    //TODO: fix shader to use shadercross

    //SDL_GPUShader* frag_shader = static_cast<SDL_GPUShader*>(
    //    SDL_ShaderCross_CompileFromSPIRV(bd->Device, &frag_shader_info, false));

    SDL_GPUShader* frag_shader = SDL_CreateGPUShader(bd->Device, &frag_shader_info);

    if (frag_shader == nullptr) {
        return false;
    }

    // Create render pipeline
    SDL_GPUColorTargetDescription color_target_description = {};
    color_target_description.format = bd->RenderTextureFormat;
    color_target_description.blend_state.enable_blend = true;
    color_target_description.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target_description.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target_description.blend_state.color_write_mask = 0xF;
    color_target_description.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target_description.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target_description.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target_description.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

    SDL_GPUVertexBufferDescription vertex_binding = {};
    vertex_binding.slot = 0;
    vertex_binding.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_binding.instance_step_rate = 0;
    vertex_binding.pitch = sizeof(ImDrawVert);

    SDL_GPUVertexAttribute vertex_attributes[3];
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attributes[0].location = 0;
    vertex_attributes[0].offset = 0;
    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attributes[1].location = 1;
    vertex_attributes[1].offset = 2 * sizeof(float);
    vertex_attributes[2].buffer_slot = 0;
    vertex_attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    vertex_attributes[2].location = 2;
    vertex_attributes[2].offset = 4 * sizeof(float);

    SDL_GPUGraphicsPipelineCreateInfo pipeline_create_info = {};
    pipeline_create_info.target_info.num_color_targets = 1;
    pipeline_create_info.target_info.color_target_descriptions = &color_target_description;
    pipeline_create_info.vertex_input_state.num_vertex_buffers = 1;
    pipeline_create_info.vertex_input_state.vertex_buffer_descriptions = &vertex_binding;
    pipeline_create_info.vertex_input_state.num_vertex_attributes = 3;
    pipeline_create_info.vertex_input_state.vertex_attributes = vertex_attributes;
    pipeline_create_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_create_info.vertex_shader = vert_shader;
    pipeline_create_info.fragment_shader = frag_shader;

    bd->Pipeline = SDL_CreateGPUGraphicsPipeline(bd->Device, &pipeline_create_info);

    if (bd->FontTexture.texture == nullptr || bd->FontTexture.sampler == nullptr)
        ImGui_ImplSDLGPU3_CreateFontsTexture();

    SDL_ReleaseGPUShader(bd->Device, vert_shader);
    SDL_ReleaseGPUShader(bd->Device, frag_shader);

    return true;
}

void ImGui_ImplSDLGPU3_DestroyDeviceObjects() {
    ImGui_ImplSDLGPU3_Data* bd = ImGui_ImplSDLGPU3_GetBackendData();

    ImGui_ImplSDLGPU3_DestroyFontsTexture();

    if (bd->VertexBuffer)
    {
        SDL_ReleaseGPUBuffer(bd->Device, bd->VertexBuffer);
    }
    if (bd->VertexTransferBuffer)
    {
        SDL_ReleaseGPUTransferBuffer(bd->Device, bd->VertexTransferBuffer);
    }
    if (bd->IndexBuffer)
    {
        SDL_ReleaseGPUBuffer(bd->Device, bd->IndexBuffer);
    }
    if (bd->IndexTransferBuffer)
    {
        SDL_ReleaseGPUTransferBuffer(bd->Device, bd->IndexTransferBuffer);
    }

    SDL_ReleaseGPUGraphicsPipeline(bd->Device, bd->Pipeline);
}
