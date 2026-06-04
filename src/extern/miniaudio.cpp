// Enable Ogg/Vorbis decoding in miniaudio by compiling stb_vorbis in.
// stb_vorbis must appear header-only BEFORE the miniaudio implementation
// (so MA_HAS_VORBIS is detected) and its full implementation AFTER it.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
