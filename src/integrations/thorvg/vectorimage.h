#pragma once

#ifdef LUMINOVEAU_WITH_THORVG

#include "gpu/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tvg {
class Picture;
}

/// An SVG rasterised on the CPU by ThorVG into a GPU texture (RGBA8, premultiplied alpha).
///
/// The texture is repainted in place by every `Render` at the same size, so anything holding the
/// handle keeps seeing the current picture. `Render` uploads through its own command buffer and
/// must not be called while a render pass is recording.
class VectorImage {
public:
    VectorImage() = default;
    ~VectorImage();

    VectorImage(const VectorImage &)            = delete;
    VectorImage &operator=(const VectorImage &) = delete;

    /// Registers a TrueType font for SVG text. `name` is what the SVG's `font-family` must say.
    static bool LoadFont(const std::string &name, const std::string &path);

    bool Load(const std::string &path);

    /// Rasterises at `width` x `height`, stretching the SVG's own size to fit.
    bool Render(uint32_t width, uint32_t height);

    /// The SVG's own width over height, or zero before `Load`.
    float AspectRatio() const;

    GpuTextureHandle Texture() const { return _texture; }
    uint32_t         Width() const { return _width; }
    uint32_t         Height() const { return _height; }

    /// Frees the GPU side. Needs the renderer alive, so call it before shutdown rather than
    /// leaving it to the destructor.
    void Release();

private:
    static bool ensureInit();

    tvg::Picture         *_picture = nullptr;
    std::vector<uint32_t> _pixels;

    GpuTextureHandle        _texture = 0;
    GpuTransferBufferHandle _staging = 0;
    uint32_t                _width   = 0;
    uint32_t                _height  = 0;

    float _intrinsicWidth  = 0.0f;
    float _intrinsicHeight = 0.0f;
};

#endif // LUMINOVEAU_WITH_THORVG
