#include "rendererhandler.h"

#include <stdexcept>
#include "audio/audiohandler.h"

#include "assethandler/assethandler.h"
#include "draw/drawhandler.h"

#include "utils/helpers.h"

#include "renderpass.h"
#include "spriterenderpass.h"
#include "shaderrenderpass.h"

void Renderer::_initRendering() {

    m_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    if (!m_device) {
        SDL_Log("%s: failed to create gpu device: %s", CURRENT_METHOD(), SDL_GetError());
        SDL_DestroyWindow(Window::GetWindow());
        return;
    }

    SDL_Log("%s: using graphics backend: %s", CURRENT_METHOD(), SDL_GetGPUDeviceDriver(m_device));

    if (!SDL_ClaimWindowForGPUDevice(m_device, Window::GetWindow())) {
        SDL_Log("%s: failed to claim window for gpu device: %s", CURRENT_METHOD(), SDL_GetError());
        return;
    }
    SDL_Log("%s: claimed window for gpu device", CURRENT_METHOD());

    renderpasses.emplace_back("2dsprites", new SpriteRenderPass(m_device));

    renderpasses.begin()->second->color_target_info_loadop      = SDL_GPU_LOADOP_CLEAR;
    renderpasses.begin()->second->color_target_info_clear_color = SDL_FColor(BLACK);

    for (auto &[passname, renderpass]: renderpasses) {
        if (!renderpass->init(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow()), Window::GetWidth(), Window::GetHeight(), passname)) {
            SDL_Log("%s: renderpass (%s) failed to init()", CURRENT_METHOD(), passname.c_str());
        }
    }

    #ifdef ADD_IMGUI
    ImGui_ImplSDL3_InitForOther(Window::GetWindow());
    ImGui_ImplSDLGPU3_Init(m_device, SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow()));
    #endif

}

void Renderer::_close() {



}
void Renderer::_clearBackground(Color color) {
//    m_cmdbuf = SDL_AcquireGPUCommandBuffer(m_device);
//
//    SDL_GPUTexture *swapchain_texture = nullptr;
//
//    if (!SDL_AcquireGPUSwapchainTexture(m_cmdbuf, Window::GetWindow(), &swapchain_texture, nullptr, nullptr)) {
//        SDL_Log("Renderer::render: failed to acquire gpu swapchain texture: %s", SDL_GetError());
//        return;
//    }
//
//    SDL_GPUColorTargetInfo color_target_info{
//        .texture = swapchain_texture,
//        .mip_level = 0,
//        .layer_or_depth_plane = 0,
//        .clear_color = {.r = color.getRFloat(), .g = color.getGFloat(), .b = color.getBFloat(), .a = color.getAFloat()},
//        .load_op = SDL_GPU_LOADOP_CLEAR,
//        .store_op = SDL_GPU_STOREOP_STORE,
//    };
//
//    SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(m_cmdbuf, &color_target_info, 1, nullptr);
//
//    SDL_EndGPURenderPass(render_pass);
}

void Renderer::_startFrame() const {
    Lerp::updateLerps();

    Window::HandleInput();

#ifdef ADD_IMGUI
    ImGui_ImplSDL3_NewFrame();
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui::NewFrame();
#endif
}

void Renderer::_endFrame() {

    auto m_camera = glm::ortho(0.0f, (float) Window::GetWidth(), float(Window::GetHeight()), 0.0f);

    m_cmdbuf = SDL_AcquireGPUCommandBuffer(m_device);
    if (!m_cmdbuf) {
        SDL_Log("Renderer::StartFrame: failed to acquire gpu command buffer: %s", SDL_GetError());
        return;
    }
    SDL_GPUTexture *swapchain_texture;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(m_cmdbuf, Window::GetWindow(), &swapchain_texture, nullptr, nullptr)) {
        SDL_Log("Renderer::render: failed to acquire gpu swapchain texture: %s", SDL_GetError());
        return;
    }

    for (auto &[passname, renderpass]: renderpasses) {
        renderpass->render(m_cmdbuf, swapchain_texture, m_camera);
    }

#ifdef ADD_IMGUI
    ImGui::Render();
    {
        auto copy_pass = SDL_BeginGPUCopyPass(m_cmdbuf);
        ImGui_ImplSDLGPU3_UploadDrawData(ImGui::GetDrawData(), copy_pass);
        SDL_EndGPUCopyPass(copy_pass);
    }
    {
        SDL_GPUColorTargetInfo color_target_info = {};
        color_target_info.texture     = swapchain_texture;
        color_target_info.mip_level = 0;
        color_target_info.layer_or_depth_plane = 0;
        color_target_info.clear_color = {0.25f, 0.25f, 0.25f, 0.0f};
        color_target_info.load_op     = SDL_GPU_LOADOP_LOAD;
        color_target_info.store_op    = SDL_GPU_STOREOP_STORE;

        auto render_pass = SDL_BeginGPURenderPass(m_cmdbuf, &color_target_info, 1, nullptr);

        ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), m_cmdbuf, render_pass);

        SDL_EndGPURenderPass(render_pass);
    }

#endif
    SDL_SubmitGPUCommandBuffer(m_cmdbuf);
    for (auto &[passname, renderpass]: renderpasses) {
        renderpass->resetRenderQueue();
    }
    m_cmdbuf = nullptr;



}

void Renderer::_reset() {
        for (auto &[passname, renderpass]: renderpasses) {

            renderpass->release();

            SDL_WaitForGPUIdle(m_device);

            if (!renderpass->init(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow()), Window::GetWidth(), Window::GetHeight(), passname)) {
                SDL_Log("%s: renderpass (%s) failed to init()", CURRENT_METHOD(), passname.c_str());
            }
        }

}

SDL_GPUDevice *Renderer::_getDevice() {
    return m_device;
}

void Renderer::_addToRenderQueue(const std::string &passname, const Renderable &renderable) {

    auto it = std::find_if(renderpasses.begin(), renderpasses.end(),
                           [&passname](const std::pair<std::string, RenderPass *> &entry) {
                               return entry.first == passname;
                           });

    if (it != renderpasses.end()) {
        it->second->addToRenderQueue(renderable);
    }
}

void Renderer::_addShaderPass(const std::string& passname, const ShaderAsset& vertShader, const ShaderAsset& fragShader) {


    auto shaderPass = new ShaderRenderPass(m_device);

    shaderPass->vertShader = vertShader;
    shaderPass->fragShader = fragShader;

    bool succes = shaderPass->init(SDL_GetGPUSwapchainTextureFormat(m_device, Window::GetWindow()), Window::GetWidth(), Window::GetHeight(), passname);
    renderpasses.emplace_back(passname, shaderPass);
}

UniformBuffer &Renderer::_getUniformBuffer(const std::string &passname) {
    auto it = std::find_if(renderpasses.begin(), renderpasses.end(),
                           [&passname](const std::pair<std::string, RenderPass *> &entry) {
                               return entry.first == passname;
                           });

    if (it != renderpasses.end()) {
        return it->second->getUniformBuffer();
    }
}