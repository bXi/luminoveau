#pragma once

#include "SDL3/SDL.h"
#include "utils/vectors.h"

/**
 * @brief Represents a texture asset for rendering images using SDL_GPU.
 */
struct TextureAsset {
    int width = -1; /**< Width of the texture. */
    int height = -1; /**< Height of the texture. */
    const char* filename = nullptr; /**< Filename of the texture image file. */

    SDL_GPUTexture *gpuTexture = nullptr; /**< Pointer to the SDL_GPU texture. */
    SDL_GPUSampler *gpuSampler = nullptr; /**< Pointer to the sampler bound to this texture. */

    /**
     * @brief Retrieves the size of the texture.
     * @return A vi2d struct containing the width and height of the texture.
     */
    vi2d getSize() const {
        return {width, height};
    }

    /**
     * @brief Releases the GPU texture resources.
     * @param device Pointer to the SDL_GPUDevice.
     * @note This is now handled automatically by AssetHandler. You rarely need to call this manually.
     */
    void release(SDL_GPUDevice *device) {
        if (this->gpuTexture != nullptr) {
            SDL_ReleaseGPUTexture(device, this->gpuTexture);
            this->gpuTexture = nullptr;
        }
    }
};

using Texture = TextureAsset&;
