#include "assethandler.h"
#include "window/windowhandler.h"
#include "spirv_cross.hpp"

#include <filesystem>
#include <utility>

#include <iostream>
#include <fstream>
#include <vector>

Texture AssetHandler::_getTexture(const std::string &fileName) {
    if (_textures.find(fileName) == _textures.end()) {
        _loadTexture(fileName);

        return _textures[fileName];
    } else {
        return _textures[fileName];
    }
}

TextureAsset AssetHandler::_loadTexture(const std::string &fileName) {

    TextureAsset texture;

    auto surface = STBIMG_Load(fileName.c_str());

    if (!surface) {
        std::string error = Helpers::TextFormat("IMG_Load failed: %s", SDL_GetError());

        SDL_Log("%s", error.c_str());
        throw std::runtime_error(error.c_str());
    }

    if (surface->format != SDL_PIXELFORMAT_RGBA32) {
        SDL_Surface* convertedSurface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface); // Free the original surface

        surface = convertedSurface;
    }

    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);

    texture.filename = fileName;
    texture.surface = surface;
    texture.width  = surface->w;
    texture.height = surface->h;

    SDL_GPUTextureCreateInfo texture_create_info{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = static_cast<Uint32>(texture.width),
        .height = static_cast<Uint32>(texture.height),
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    SDL_GPUTexture *gpuTexture = SDL_CreateGPUTexture(Window::GetDevice(), &texture_create_info);
    if (!gpuTexture)
    {
        SDL_Log("GPUTexture::from_file: failed to create texture: %s (%s)", fileName.c_str(), SDL_GetError());
        throw std::runtime_error("failed to create texture");
    }

    if (!_copy_to_texture(
            Window::GetDevice(),
            surface->pixels,
            texture.width * texture.height * 4,
            gpuTexture,
            texture.width,
            texture.height
        ))
    {
        SDL_ReleaseGPUTexture(Window::GetDevice(), gpuTexture);
        throw std::runtime_error("failed to copy image data to texture");
    }

    SDL_GPUSamplerCreateInfo sampler_create_info{
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .mip_lod_bias = 0,
        .max_anisotropy = 0,
        .compare_op = SDL_GPU_COMPAREOP_NEVER,
        .min_lod = 0,
        .max_lod = 0,
        .enable_anisotropy = false,
        .enable_compare = false,
        .padding1 = 0,
        .padding2 = 0,
        .props = 0,
    };

    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(Window::GetDevice(), &sampler_create_info);
    if (!sampler)
    {
        SDL_ReleaseGPUTexture(Window::GetDevice(), gpuTexture);
        throw std::runtime_error("failed to create sampler");
    }

    texture.gpuSampler = sampler;
    texture.gpuTexture = gpuTexture;

    if (!texture.filename.empty()) {
        SDL_SetGPUTextureName(Window::GetDevice(), gpuTexture, texture.filename.c_str());
    }


    SDL_Log("%s: loaded texture %s (%i x %i)", CURRENT_METHOD(), fileName.c_str(), texture.width, texture.height);

    _textures[std::string(fileName)] = texture;

    return texture;
}

TextureAsset AssetHandler::_createEmptyTexture(const vf2d &size) {
    TextureAsset texture;

    texture.width  = size.x;
    texture.height = size.y;

    texture.surface = nullptr;

    texture.texture = SDL_CreateTexture(Window::GetRenderer(), SDL_PixelFormat::SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_TARGET, (int) size.x, (int) size.y);

    //TODO: fix for other renderers

    get()._createTextureId++;
    texture.id = get()._createTextureId;

    SDL_SetTextureScaleMode(texture.texture, (SDL_ScaleMode) defaultMode);

    return texture;
}

void AssetHandler::_saveTextureAsPNG(Texture texture, const char *fileName) {

    SDL_Surface *surface = texture.surface;

    if (!surface) {
        float texWidth, texHeight;

        SDL_PropertiesID props = SDL_GetTextureProperties(texture.texture);
        SDL_GetTextureSize(texture.texture, &texWidth, &texHeight);
        surface = SDL_CreateSurface((int) texWidth, (int) texHeight, SDL_PixelFormat::SDL_PIXELFORMAT_ARGB8888);

        if (!surface) {
            SDL_Log("%s", SDL_GetError());
        }

        SDL_RenderReadPixels(Window::GetRenderer(), nullptr);
    }

    SDL_Surface *rgbaSurface = surface;

    if (surface->format != SDL_PIXELFORMAT_ARGB8888) {
        rgbaSurface = SDL_ConvertSurface(surface, SDL_PixelFormat::SDL_PIXELFORMAT_ARGB8888);
    }

    unsigned char *pixels = new unsigned char[rgbaSurface->w * rgbaSurface->h * 4];

    SDL_LockSurface(rgbaSurface);
    memcpy(pixels, rgbaSurface->pixels, rgbaSurface->w * rgbaSurface->h * 4);
    SDL_UnlockSurface(rgbaSurface);

    stbi_write_png(fileName, rgbaSurface->w, rgbaSurface->h, 4, pixels, rgbaSurface->w * 4);

    delete[] pixels;

    if (surface->format != SDL_PixelFormat::SDL_PIXELFORMAT_ARGB8888) {
        SDL_DestroySurface(rgbaSurface);
    }
}

Sound AssetHandler::_getSound(const std::string &fileName) {
    if (_sounds.find(fileName) == _sounds.end()) {
        SoundAsset _sound;
        _sound.sound    = new ma_sound();
        _sound.fileName = fileName;
        ma_result result = ma_sound_init_from_file(Audio::GetAudioEngine(), fileName.c_str(),
                                                   MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr,
                                                   _sound.sound);

        if (result != MA_SUCCESS) {
            std::string error = Helpers::TextFormat("GetSound failed: %s", fileName.c_str());

            SDL_Log("%s", error.c_str());
            throw std::runtime_error(error.c_str());
        }

        _sounds[fileName] = _sound;

        return _sounds[fileName];
    } else {
        return _sounds[fileName];
    }
}

Music AssetHandler::_getMusic(const std::string &fileName) {
    if (_musics.find(fileName) == _musics.end()) {
        MusicAsset _music;

        _music.music = new ma_sound();
        ma_result result = ma_sound_init_from_file(Audio::GetAudioEngine(), fileName.c_str(),
                                                   MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, nullptr, nullptr,
                                                   _music.music);

        if (result != MA_SUCCESS) {
            std::string error = Helpers::TextFormat("GetMusic failed: %s", fileName.c_str());

            SDL_Log("%s", error.c_str());
            throw std::runtime_error(error.c_str());
        }

        _musics[fileName] = _music;

        return _musics[fileName];
    } else {
        return _musics[fileName];
    }
}

Font AssetHandler::_getFont(const std::string &fileName, const int fontSize) {
    std::string index = std::string(Helpers::TextFormat("%s%d", fileName.c_str(), fontSize));

    auto it = _fonts.find(index);

    if (it == _fonts.end()) {

        FontAsset _font;

        _font.textEngine = TTF_CreateGPUTextEngine(Window::GetDevice());
        _font.ttfFont = TTF_OpenFont(fileName.c_str(), fontSize);

        if (!_font.ttfFont) {
            throw std::runtime_error("Can't load font.");
        }

        _fonts[index] = _font;

        return _fonts[index];
    } else {
        return _fonts[index];
    }
}

void AssetHandler::_setDefaultTextureScaleMode(ScaleMode mode) {
    defaultMode = mode;
}

ScaleMode AssetHandler::_getDefaultTextureScaleMode() {
    return defaultMode;
}

Shader AssetHandler::_getShader(const std::string &fileName) {

    if (_shaders.find(fileName) == _shaders.end()) {

        // Auto-detect the shader stage from the file name for convenience
        SDL_GPUShaderStage stage;
        if (SDL_strstr(fileName.c_str(), ".vert")) {
            stage = SDL_GPU_SHADERSTAGE_VERTEX;
        } else if (SDL_strstr(fileName.c_str(), ".frag")) {
            stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        } else {
            throw std::runtime_error("Invalid shader stage!!");
        }


        std::string binFileName = fileName + ".bin";

        if (!std::filesystem::exists(binFileName)) {

            //do compile
        }

        size_t codeSize;
        auto  *code = (Uint8 *) SDL_LoadFile(binFileName.c_str(), &codeSize);
        if (code == nullptr) {
            throw std::runtime_error(Helpers::TextFormat("Failed to load shader from disk! %s", fileName.c_str()));
        }

        spirv_cross::Compiler compiler(reinterpret_cast<const uint32_t*>(code), codeSize / sizeof(uint32_t));
        const auto resources = compiler.get_shader_resources();

        Uint32 samplerCount        = resources.sampled_images.size();
        Uint32 uniformBufferCount  = resources.uniform_buffers.size();
        Uint32 storageBufferCount  = resources.storage_buffers.size();
        Uint32 storageTextureCount = resources.storage_images.size();

        SDL_GPUShaderCreateInfo shaderInfo = {
            .code_size = codeSize,
            .code = code,
            .entrypoint = "main",
            .format = SDL_GPU_SHADERFORMAT_SPIRV,
            .stage = stage,
            .num_samplers = samplerCount,
            .num_storage_textures = storageTextureCount,
            .num_storage_buffers = storageBufferCount,
            .num_uniform_buffers = uniformBufferCount,
        };

        ShaderAsset _shader;

        _shader.shaderFilename = fileName;

        _shader.samplerCount = samplerCount;
        _shader.uniformBufferCount = uniformBufferCount;
        _shader.storageBufferCount = storageBufferCount;
        _shader.storageTextureCount = storageTextureCount;

        _shader.shader = SDL_CreateGPUShader(Window::GetDevice(), &shaderInfo);
        if (_shader.shader == nullptr) {
            SDL_free(code);
            throw std::runtime_error("Failed to create shader!");
        }

        SDL_free(code);

        _shaders[std::string(fileName)] = _shader;

        return _shaders[fileName];
    } else {
        return _shaders[fileName];
    }
}

bool AssetHandler::_copy_to_texture(
    SDL_GPUDevice *device, void *src_data, uint32_t src_data_len, SDL_GPUTexture *dst_texture,
    uint32_t dst_texture_width, uint32_t dst_texture_height
)
{
    SDL_GPUTransferBufferCreateInfo transfer_buf_create_info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = src_data_len,
        .props = 0,
    };
    SDL_GPUTransferBuffer *transfer_buf =
        SDL_CreateGPUTransferBuffer(device, &transfer_buf_create_info);
    if (!transfer_buf)
    {
//        spdlog::error("copy_to_texture: failed to create transfer buffer: {}", SDL_GetError());
        return false;
    }
    void *transfer_buf_ptr = SDL_MapGPUTransferBuffer(device, transfer_buf, false);
    if (!transfer_buf_ptr)
    {
//        spdlog::error("copy_to_texture: failed to map transfer buffer: {}", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer_buf);
        return false;
    }
    std::memcpy(transfer_buf_ptr, src_data, src_data_len);
    SDL_UnmapGPUTransferBuffer(device, transfer_buf);

    SDL_GPUCommandBuffer *copy_cmd_buf = SDL_AcquireGPUCommandBuffer(device);
    if (!copy_cmd_buf)
    {
//        spdlog::error("copy_to_texture: failed to create init command buffer: {}", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(device, transfer_buf);
        return false;
    }
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(copy_cmd_buf);
    {
        SDL_GPUTextureTransferInfo transfer_info{
            .transfer_buffer = transfer_buf,
            .offset = 0,
            .pixels_per_row = 0,
            .rows_per_layer = 0,
        };
        SDL_GPUTextureRegion destination_info{
            .texture = dst_texture,
            .mip_level = 0,
            .layer = 0,
            .x = 0,
            .y = 0,
            .z = 0,
            .w = dst_texture_width,
            .h = dst_texture_height,
            .d = 1,
        };
        SDL_UploadToGPUTexture(copy_pass, &transfer_info, &destination_info, false);
    }
    SDL_EndGPUCopyPass(copy_pass);
    SDL_SubmitGPUCommandBuffer(copy_cmd_buf);

    SDL_ReleaseGPUTransferBuffer(device, transfer_buf);

    return true;
}

TextureAsset AssetHandler::_createDepthTarget(SDL_GPUDevice *device, uint32_t width, uint32_t height) {

    SDL_GPUTextureCreateInfo texture_create_info{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(device, &texture_create_info);
    if (!texture)
    {
        throw std::runtime_error("failed to create depth texture");
    }

    TextureAsset tex;

    tex.gpuSampler = nullptr;
    tex.gpuTexture = texture;


    return tex;
}

TextureAsset AssetHandler::_loadFromPixelData(const vf2d& size, void *pixelData, std::string fileName) {

        TextureAsset texture;

    texture.width  = (int)size.x;
    texture.height = (int)size.y;
    texture.filename = std::move(fileName);

    //SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
    SDL_GPUTextureCreateInfo texture_create_info{
        .type =  SDL_GPU_TEXTURETYPE_2D, //SDL_GPU_TEXTURETYPE_2D_ARRAY
        .format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER, //SDL_GPU_TEXTUREUSAGE_SAMPLER
        .width = (uint32_t)texture.width,
        .height = (uint32_t)texture.height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    SDL_GPUTexture *gpuTexture = SDL_CreateGPUTexture(Window::GetDevice(), &texture_create_info);
    if (!gpuTexture)
    {
        SDL_Log("GPUTexture::from_file: failed to create texture: (%s)", SDL_GetError());
        throw std::runtime_error("failed to create texture");
    }

    if (!_copy_to_texture(
            Window::GetDevice(),
            pixelData,
            (uint32_t)size.x * (uint32_t)size.y * 4,
            gpuTexture,
            (uint32_t)size.x,
            (uint32_t)size.y
        ))
    {
        SDL_ReleaseGPUTexture(Window::GetDevice(), gpuTexture);
        throw std::runtime_error("failed to copy image data to texture");
    }

    //TODO: SDL_GPU_FILTER_NEAREST replace with configured one. (scalemode)
    SDL_GPUSamplerCreateInfo sampler_create_info{
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .mip_lod_bias = 0,
        .max_anisotropy = 0,
        .compare_op = SDL_GPU_COMPAREOP_NEVER,
        .min_lod = 0,
        .max_lod = 0,
        .enable_anisotropy = false,
        .enable_compare = false,
        .padding1 = 0,
        .padding2 = 0,
        .props = 0,
    };

    SDL_GPUSampler *sampler = SDL_CreateGPUSampler(Window::GetDevice(), &sampler_create_info);
    if (!sampler)
    {
//        spdlog::error("GPUTexture::from_file: failed to create sampler: {}", SDL_GetError());
        SDL_ReleaseGPUTexture(Window::GetDevice(), gpuTexture);
        throw std::runtime_error("failed to create sampler");
    }

    texture.gpuSampler = sampler;
    texture.gpuTexture = gpuTexture;

    if (!texture.filename.empty()) {
        SDL_SetGPUTextureName(Window::GetDevice(), gpuTexture, texture.filename.c_str());
    }


    //_textures[std::string(fileName)] = texture;

    return texture;
}
