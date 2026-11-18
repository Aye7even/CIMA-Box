// miniaudio with the parts this app doesn't use switched off.
// Exactly one .cpp defines MINIAUDIO_IMPLEMENTATION before including this.
#pragma once
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX  // miniaudio includes <windows.h>; keep its min/max macros away from std::min/std::max
#endif
#define MA_NO_WAV   // only MP3 is decoded (ElevenLabs' "High" quality)
#define MA_NO_FLAC
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"
