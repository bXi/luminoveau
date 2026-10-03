#ifdef LUMINOVEAU_WITH_THORVG

#include "integrations/thorvg/vectorimage.h"

#include "core/log/log.h"
#include "file/filehandler.h"
#include "gpu/IGpu.h"
#include "renderer/renderer.h"

#include <thorvg.h>

#include <algorithm>
#include <cstring>
#include <memory>

bool VectorImage::ensureInit() {
    static bool initialised = false;
    if (!initialised) {
        // No worker threads: pictures here are small and rendered rarely.
        initialised = tvg::Initializer::init(0) == tvg::Result::Success;
        if (!initialised) LOG_ERROR("VectorImage: ThorVG failed to initialise");
    }
    return initialised;
}

bool VectorImage::LoadFont(const std::string &name, const std::string &path) {
    if (!ensureInit()) return false;

    std::vector<uint8_t> data = FileHandler::ReadBinaryFile(path);
    if (data.empty()) {
        LOG_ERROR("VectorImage: could not read font {}", path);
        return false;
    }

    // Copied, since `data` goes out of scope.
    if (tvg::Text::load(name.c_str(), reinterpret_cast<const char *>(data.data()),
                        static_cast<uint32_t>(data.size()), "ttf", true) != tvg::Result::Success) {
        LOG_ERROR("VectorImage: ThorVG rejected font {}", path);
        return false;
    }
    return true;
}

VectorImage::~VectorImage() {
    delete _canvas;
    if (_picture != nullptr) _picture->unref();
}

bool VectorImage::Load(const std::string &path) {
    if (!ensureInit()) return false;

    std::vector<uint8_t> data = FileHandler::ReadBinaryFile(path);
    if (data.empty()) {
        LOG_ERROR("VectorImage: could not read {}", path);
        return false;
    }

    delete _canvas;
    _canvas = nullptr;
    if (_picture != nullptr) _picture->unref();

    // Held by reference so a canvas can take it and give it back on every render.
    _picture = tvg::Picture::gen();
    _picture->ref();

    if (_picture->load(reinterpret_cast<const char *>(data.data()), static_cast<uint32_t>(data.size()),
                       "svg", nullptr, true) != tvg::Result::Success) {
        LOG_ERROR("VectorImage: ThorVG could not load {}", path);
        _picture->unref();
        _picture = nullptr;
        return false;
    }

    _picture->size(&_intrinsicWidth, &_intrinsicHeight);
    return true;
}

float VectorImage::AspectRatio() const {
    return _intrinsicHeight > 0.0f ? _intrinsicWidth / _intrinsicHeight : 0.0f;
}

bool VectorImage::Render(uint32_t width, uint32_t height) {
    if (_picture == nullptr || width == 0 || height == 0) return false;

    IGpu &gpu = Renderer::GetGpu();

    if (width != _width || height != _height) {
        Release();

        GpuTextureCreateInfo info{};
        info.width  = width;
        info.height = height;
        info.format = GpuTextureFormat::R8G8B8A8_Unorm;
        info.usage  = GpuTextureUsage::Sampler | GpuTextureUsage::Transfer;
        _texture    = gpu.CreateTexture(info);

        GpuTransferBufferCreateInfo staging{};
        staging.size  = width * height * 4;
        staging.usage = GpuTransferUsage::Upload;
        _staging      = gpu.CreateTransferBuffer(staging);

        if (_texture == 0 || _staging == 0) {
            LOG_ERROR("VectorImage: could not create a {}x{} texture", width, height);
            Release();
            return false;
        }
        _width  = width;
        _height = height;
        _pixels.assign((size_t) width * height, 0u);
    }

    // ABGR8888 is R, G, B, A in memory on a little-endian machine, premultiplied: RGBA8 as the UI wants it.
    if (_canvas == nullptr) {
        _canvas = tvg::SwCanvas::gen();
        if (_canvas == nullptr || _canvas->add(_picture) != tvg::Result::Success) {
            LOG_ERROR("VectorImage: could not create the canvas");
            delete _canvas;
            _canvas = nullptr;
            return false;
        }
    }
    if (_canvas->target(_pixels.data(), width, width, height, tvg::ColorSpace::ABGR8888) !=
        tvg::Result::Success) {
        LOG_ERROR("VectorImage: could not target the canvas");
        return false;
    }

    std::fill(_pixels.begin(), _pixels.end(), 0u);
    _picture->size(static_cast<float>(width), static_cast<float>(height));
    _canvas->update();
    _canvas->draw(true);
    _canvas->sync();

    std::memcpy(gpu.MapTransferBuffer(_staging, true), _pixels.data(), _pixels.size() * 4);
    gpu.UnmapTransferBuffer(_staging);

    GpuTransferBufferRegion src{};
    src.transferBuffer = _staging;
    src.pixelsPerRow   = width;
    src.rowsPerLayer   = height;

    GpuTextureRegion dst{};
    dst.texture = _texture;
    dst.width   = width;
    dst.height  = height;
    dst.depth   = 1;

    GpuCmdBufferHandle cmd = gpu.AcquireCommandBuffer();
    gpu.UploadToTexture(cmd, src, dst);
    gpu.SubmitCommandBuffer(cmd);
    return true;
}

void VectorImage::Release() {
    IGpu &gpu = Renderer::GetGpu();
    if (_texture != 0) gpu.ReleaseTexture(_texture);
    if (_staging != 0) gpu.ReleaseTransferBuffer(_staging);
    _texture = 0;
    _staging = 0;
    _width   = 0;
    _height  = 0;
}

GpuTextureHandle VectorImage::Detach() {
    GpuTextureHandle texture = _texture;
    _texture = 0;
    _width   = 0;
    _height  = 0;
    return texture;
}

#endif // LUMINOVEAU_WITH_THORVG
