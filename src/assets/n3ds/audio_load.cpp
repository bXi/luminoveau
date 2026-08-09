// 3DS audio decoding for AssetHandler. Decodes to interleaved float PCM held in the asset's
// 3DS PCM fields; the SDL audio backend (src/platform/audio/n3ds/audio.cpp) plays that
// buffer. miniaudio (which normally decodes) is not compiled on 3DS, so this TU also
// compiles stb_vorbis (previously pulled in via src/extern/miniaudio.cpp). Ogg is decoded
// with stb_vorbis; WAV via SDL (the format DeltaLight2 and many games actually ship).

#include "assets/n3ds/audio_load.h"

#include "file/filehandler.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_iostream.h>

#include <cctype>
#include <cstdlib>
#include <cstring>

// Full stb_vorbis implementation lives here now (no miniaudio.cpp on 3DS to provide it).
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

namespace {
// Decode a whole Ogg file to interleaved float PCM (owned; free with free()).
bool decodeOgg(const std::string &fileName, float *&pcm, uint64_t &frameCount,
    int &channels, int &sampleRate) {
    pcm        = nullptr;
    frameCount = 0;
    channels   = 0;
    sampleRate = 0;

    PhysFSFileData fd = FileHandler::ReadFile(fileName);
    if (!fd.data || fd.fileSize <= 0)
        return false;

    short *s16    = nullptr;
    int    ch     = 0;
    int    sr     = 0;
    int    frames = stb_vorbis_decode_memory(static_cast<const unsigned char *>(fd.data),
        fd.fileSize, &ch, &sr, &s16);

    free(fd.data); // encoded bytes no longer needed after a full decode
    if (frames < 0 || s16 == nullptr || ch <= 0) {
        free(s16);
        return false;
    }

    const size_t total = static_cast<size_t>(frames) * static_cast<size_t>(ch);
    pcm                = static_cast<float *>(malloc(total * sizeof(float)));
    if (pcm == nullptr) {
        free(s16);
        return false;
    }
    for (size_t i = 0; i < total; ++i)
        pcm[i] = static_cast<float>(s16[i]) / 32768.0f;
    free(s16);

    frameCount = static_cast<uint64_t>(frames);
    channels   = ch;
    sampleRate = sr;
    return true;
}

// Decode a WAV file to interleaved float PCM (owned; free with free()). SDL parses the WAV
// and SDL_ConvertAudioSamples normalises whatever sample format it holds to F32.
bool decodeWav(const std::string &fileName, float *&pcm, uint64_t &frameCount,
    int &channels, int &sampleRate) {
    pcm        = nullptr;
    frameCount = 0;
    channels   = 0;
    sampleRate = 0;

    PhysFSFileData fd = FileHandler::ReadFile(fileName);
    if (!fd.data || fd.fileSize <= 0)
        return false;

    SDL_IOStream *io = SDL_IOFromConstMem(fd.data, static_cast<size_t>(fd.fileSize));
    if (!io) {
        free(fd.data);
        return false;
    }

    SDL_AudioSpec spec {};
    Uint8        *wav    = nullptr;
    Uint32        wavLen = 0;
    bool          ok     = SDL_LoadWAV_IO(io, true /*closeio*/, &spec, &wav, &wavLen);
    free(fd.data);
    if (!ok || !wav)
        return false;

    SDL_AudioSpec dst = spec;
    dst.format        = SDL_AUDIO_F32;
    Uint8 *f32        = nullptr;
    int    f32Len     = 0;
    bool   cok        = SDL_ConvertAudioSamples(&spec, wav, static_cast<int>(wavLen),
          &dst, &f32, &f32Len);
    SDL_free(wav);
    if (!cok || !f32 || f32Len <= 0) {
        SDL_free(f32);
        return false;
    }

    pcm = static_cast<float *>(malloc(static_cast<size_t>(f32Len)));
    if (!pcm) {
        SDL_free(f32);
        return false;
    }
    std::memcpy(pcm, f32, static_cast<size_t>(f32Len));
    SDL_free(f32);

    channels   = dst.channels;
    sampleRate = dst.freq;
    frameCount = static_cast<uint64_t>(f32Len)
        / (static_cast<uint64_t>(dst.channels) * sizeof(float));
    return true;
}

bool hasExtension(const std::string &name, const char *ext) {
    const size_t n = std::strlen(ext);
    if (name.size() < n)
        return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower((unsigned char)name[name.size() - n + i]) != (unsigned char)ext[i])
            return false;
    return true;
}

// Pick the decoder by extension; default to Ogg (covers .ogg and anything stb can sniff).
bool decodeFile(const std::string &fileName, float *&pcm, uint64_t &frameCount,
    int &channels, int &sampleRate) {
    if (hasExtension(fileName, ".wav"))
        return decodeWav(fileName, pcm, frameCount, channels, sampleRate);
    return decodeOgg(fileName, pcm, frameCount, channels, sampleRate);
}

// .mp3 sources are transcoded to .ogg at build time (3DS has no MP3 decoder), so a request
// for a .mp3 resolves to its .ogg sibling.
std::string resolveOgg(const std::string &fileName) {
    if (hasExtension(fileName, ".mp3")) {
        std::string o = fileName;
        o.replace(o.size() - 4, 4, ".ogg");
        return o;
    }
    return fileName;
}
} // namespace

namespace LumiN3dsAudio {

bool LoadSound(const std::string &fileName, SoundAsset &out) {
    return decodeFile(resolveOgg(fileName), out.pcm, out.frameCount, out.channels, out.sampleRate);
}

bool LoadMusic(const std::string &fileName, MusicAsset &out) {
    out.encoded     = nullptr;
    out.encodedSize = 0;
    out.channels    = 0;
    out.sampleRate  = 0;

    PhysFSFileData fd = FileHandler::ReadFile(resolveOgg(fileName));
    if (!fd.data || fd.fileSize <= 0)
        return false;

    // Probe channels/rate up front; keep the compressed bytes for streamed playback.
    int         err   = 0;
    stb_vorbis *probe = stb_vorbis_open_memory(
        static_cast<const unsigned char *>(fd.data), fd.fileSize, &err, nullptr);
    if (!probe) {
        free(fd.data);
        return false;
    }
    stb_vorbis_info info = stb_vorbis_get_info(probe);
    out.channels         = info.channels;
    out.sampleRate       = info.sample_rate;
    stb_vorbis_close(probe);

    out.encoded     = static_cast<unsigned char *>(fd.data);
    out.encodedSize = static_cast<unsigned long>(fd.fileSize);
    return true;
}

void FreeSound(SoundAsset &out) {
    if (out.pcm) {
        free(out.pcm);
        out.pcm = nullptr;
    }
    out.frameCount = 0;
}

void FreeMusic(MusicAsset &out) {
    if (out.encoded) {
        free(out.encoded);
        out.encoded = nullptr;
    }
    out.encodedSize = 0;
}

// ── Streaming Ogg (music) ──
void *OpenOggStream(const unsigned char *data, unsigned long size, int &channels, int &rate) {
    channels = 0;
    rate     = 0;
    if (!data || size == 0)
        return nullptr;
    int         err = 0;
    stb_vorbis *v   = stb_vorbis_open_memory(data, static_cast<int>(size), &err, nullptr);
    if (!v)
        return nullptr;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    channels             = info.channels;
    rate                 = info.sample_rate;
    return v;
}

long ReadOggStream(void *handle, float *out, long maxFrames) {
    if (!handle || !out || maxFrames <= 0)
        return 0;
    auto     *v  = static_cast<stb_vorbis *>(handle);
    const int ch = stb_vorbis_get_info(v).channels;
    int       frames =
        stb_vorbis_get_samples_float_interleaved(v, ch, out, static_cast<int>(maxFrames * ch));
    return frames < 0 ? 0 : frames;
}

void RewindOggStream(void *handle) {
    if (handle)
        stb_vorbis_seek_start(static_cast<stb_vorbis *>(handle));
}

void CloseOggStream(void *handle) {
    if (handle)
        stb_vorbis_close(static_cast<stb_vorbis *>(handle));
}

} // namespace LumiN3dsAudio
