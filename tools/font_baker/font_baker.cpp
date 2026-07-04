// font_baker — offline tool that bakes the default-font MSDF atlas into a compiled blob so the
// engine loads it at startup instead of generating it (the big web-load spike; MEMFS is wiped each
// reload so the runtime font cache never persists on the web).
//
// Output: src/assets/font_atlas_generated.cpp/.h — a zstd-compressed RGBA atlas + a glyph layout
// table (metrics + atlas rects), byte-array style like the shader blobs.
//
// Dependencies: msdf-atlas-gen / msdfgen / freetype (atlas generation) + zstd (compression). It
// never links the engine — the font bytes come from a *copy* of the engine's DroidSansMono.cpp
// compiled against a stub assethandler.h (see CMakeLists.txt), so there's no circular dependency.
//
// IMPORTANT: the atlas parameters below must stay in lock-step with AssetHandler's default-font path
// (charset 0x20..0x17F, size/minScale 64, pxRange 4, miter 1, SQUARE, ink-trap coloring). If you
// change them here, change them there, and vice-versa — then re-run this baker.

#include <msdf-atlas-gen/msdf-atlas-gen.h>
#include <msdfgen/msdfgen.h>
#include <zstd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Must match AssetHandler::FONT_CACHE_VERSION and the .fontmeta layout the runtime loader parses.
static constexpr uint32_t FONT_CACHE_VERSION = 1;

// The font bytes, defined in the engine's DroidSansMono.cpp (compiled in via a stub header — see
// CMakeLists.txt). Length is the known embedded size (AssetHandler::DroidSansMono_ttf_len).
class AssetHandler { public: static const unsigned char DroidSansMono_ttf[]; };
static constexpr unsigned int DROID_SANS_MONO_TTF_LEN = 119380;

// ---- little-endian POD serialization into a byte vector ---------------------------------------
template <class T>
static void putv(std::vector<unsigned char>& out, const T& v) {
    const auto* p = reinterpret_cast<const unsigned char*>(&v);
    out.insert(out.end(), p, p + sizeof(T));
}

// ---- emit a C byte array ----------------------------------------------------------------------
static void emitArray(FILE* f, const char* name, const unsigned char* data, size_t len) {
    std::fprintf(f, "extern const unsigned char %s[] = {\n", name);
    for (size_t i = 0; i < len; ++i) {
        std::fprintf(f, "0x%02x,", data[i]);
        if ((i & 31) == 31) std::fputc('\n', f);
    }
    std::fprintf(f, "\n};\nextern const unsigned int %s_len = %zuu;\n\n", name, len);
}

int main(int argc, char** argv) {
    const char* outPath = (argc > 1) ? argv[1] : "font_atlas_generated.cpp";

    // ---- generate the MSDF atlas (mirrors AssetHandler's default-font path) --------------------
    msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
    if (!ft) { std::fprintf(stderr, "font_baker: FreeType init failed\n"); return 1; }

    msdfgen::FontHandle* font = msdfgen::loadFontData(
        ft, AssetHandler::DroidSansMono_ttf, (int)DROID_SANS_MONO_TTF_LEN);
    if (!font) { std::fprintf(stderr, "font_baker: font load failed\n"); return 1; }

    std::vector<msdf_atlas::GlyphGeometry> glyphs;
    msdf_atlas::FontGeometry fontGeometry(&glyphs);
    msdf_atlas::Charset charset;
    for (uint32_t cp = 0x20; cp <= 0x17F; ++cp) charset.add(cp);
    fontGeometry.loadCharset(font, 1.0, charset);

    const double ascender  = fontGeometry.getMetrics().ascenderY;
    const double descender = fontGeometry.getMetrics().descenderY;
    const double lineHeight = fontGeometry.getMetrics().lineHeight;

    const double maxCornerAngle = 3.0;
    for (auto& g : glyphs) g.edgeColoring(&msdfgen::edgeColoringInkTrap, maxCornerAngle, 0);

    msdf_atlas::TightAtlasPacker packer;
    packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::SQUARE);
    packer.setMinimumScale(64.0);
    packer.setPixelRange(4.0);
    packer.setMiterLimit(1.0);
    packer.pack(glyphs.data(), glyphs.size());

    int atlasW = 0, atlasH = 0;
    packer.getDimensions(atlasW, atlasH);

    msdf_atlas::ImmediateAtlasGenerator<
        float, 3, msdf_atlas::msdfGenerator,
        msdf_atlas::BitmapAtlasStorage<unsigned char, 3>> generator(atlasW, atlasH);
    generator.generate(glyphs.data(), glyphs.size());
    msdfgen::BitmapConstRef<unsigned char, 3> bitmap = generator.atlasStorage();

    // 3ch MSDF -> RGBA, flipped vertically (same as the runtime upload path).
    std::vector<unsigned char> rgba((size_t)atlasW * atlasH * 4);
    for (int y = 0; y < atlasH; ++y) {
        for (int x = 0; x < atlasW; ++x) {
            int srcY = atlasH - 1 - y;
            size_t idx = (size_t)y * atlasW + x;
            const unsigned char* px = bitmap(x, srcY);
            rgba[idx * 4 + 0] = px[0];
            rgba[idx * 4 + 1] = px[1];
            rgba[idx * 4 + 2] = px[2];
            rgba[idx * 4 + 3] = 255;
        }
    }

    // ---- layout/metrics blob (byte-identical to AssetHandler's .fontmeta) ----------------------
    std::vector<unsigned char> meta;
    putv(meta, FONT_CACHE_VERSION);
    putv(meta, (uint32_t)atlasW);
    putv(meta, (uint32_t)atlasH);
    putv(meta, (uint32_t)64);                 // generatedSize
    putv(meta, (uint32_t)glyphs.size());
    putv(meta, ascender);
    putv(meta, descender);
    putv(meta, lineHeight);
    for (auto& g : glyphs) {
        double pl, pb, pr, pt, al, ab, ar, at;
        g.getQuadPlaneBounds(pl, pb, pr, pt);
        g.getQuadAtlasBounds(al, ab, ar, at);
        putv(meta, (uint32_t)g.getCodepoint());
        putv(meta, (double)g.getAdvance());
        putv(meta, pl); putv(meta, pb); putv(meta, pr); putv(meta, pt);
        putv(meta, al); putv(meta, ab); putv(meta, ar); putv(meta, at);
    }

    // ---- zstd-compress the (large) RGBA; meta stays raw (tiny) ---------------------------------
    size_t bound = ZSTD_compressBound(rgba.size());
    std::vector<unsigned char> comp(bound);
    size_t compLen = ZSTD_compress(comp.data(), bound, rgba.data(), rgba.size(), 19);
    if (ZSTD_isError(compLen)) {
        std::fprintf(stderr, "font_baker: zstd compress failed: %s\n", ZSTD_getErrorName(compLen));
        return 1;
    }
    comp.resize(compLen);

    // ---- write the generated .cpp + .h ---------------------------------------------------------
    FILE* f = std::fopen(outPath, "wb");
    if (!f) { std::fprintf(stderr, "font_baker: cannot open %s\n", outPath); return 1; }
    std::fprintf(f,
        "// Generated by tools/font_baker — do not edit. Re-run the font_baker target to regenerate.\n"
        "// Default-font MSDF atlas: %dx%d, %zu glyphs, RGBA %zu bytes -> zstd %zu bytes.\n"
        "#include \"font_atlas_generated.h\"\n\n"
        "extern const unsigned int lumi_font_atlas_rgba_len = %uu;  // uncompressed RGBA size\n\n",
        atlasW, atlasH, glyphs.size(), rgba.size(), comp.size(),
        (unsigned)rgba.size());
    emitArray(f, "lumi_font_atlas_meta", meta.data(), meta.size());
    emitArray(f, "lumi_font_atlas_rgba_zstd", comp.data(), comp.size());
    std::fclose(f);

    // header (write next to the .cpp)
    std::string hdr = outPath;
    auto dot = hdr.find_last_of('.');
    if (dot != std::string::npos) hdr = hdr.substr(0, dot);
    hdr += ".h";
    if (FILE* h = std::fopen(hdr.c_str(), "wb")) {
        std::fprintf(h,
            "// Generated by tools/font_baker — do not edit.\n#pragma once\n\n"
            "extern const unsigned char lumi_font_atlas_meta[];\n"
            "extern const unsigned int  lumi_font_atlas_meta_len;\n"
            "extern const unsigned char lumi_font_atlas_rgba_zstd[];\n"
            "extern const unsigned int  lumi_font_atlas_rgba_zstd_len;\n"
            "extern const unsigned int  lumi_font_atlas_rgba_len;\n");
        std::fclose(h);
    }

    std::printf("font_baker: %s (%dx%d, %zu glyphs, %zu -> %zu B zstd)\n",
                outPath, atlasW, atlasH, glyphs.size(), rgba.size(), comp.size());
    return 0;
}
