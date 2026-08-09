#pragma once

// 3DS-only Ogg/Vorbis → PCM loading. Replaces miniaudio's decode path in AssetHandler
// (miniaudio is not compiled on 3DS). Decodes fully to interleaved float PCM stored in
// the asset's 3DS PCM fields; the SDL audio backend plays that buffer directly.

#include <string>

#include "assets/audio/sound.h"
#include "assets/audio/music.h"

namespace LumiN3dsAudio {
bool LoadSound(const std::string &fileName, SoundAsset &out);
bool LoadMusic(const std::string &fileName, MusicAsset &out);
void FreeSound(SoundAsset &out);
void FreeMusic(MusicAsset &out);

// Streaming Ogg playback for music (songs are too long to fully decode). The handle keeps a
// stb_vorbis decoder over the caller-owned compressed bytes; the audio backend pulls chunks.
void *OpenOggStream(const unsigned char *data, unsigned long size, int &channels, int &rate);
long  ReadOggStream(void *handle, float *out, long maxFrames); ///< frames written; 0 = EOF
void  RewindOggStream(void *handle);
void  CloseOggStream(void *handle);
} // namespace LumiN3dsAudio
