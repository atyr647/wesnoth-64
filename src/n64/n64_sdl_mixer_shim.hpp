/**
 * n64_sdl_mixer_shim.hpp
 *
 * Replaces SDL_mixer (SDL2/SDL_mixer.h) for N64 builds.
 *
 * Wesnoth uses SDL_mixer for audio playback. On N64, audio goes through
 * n64_audio.hpp (libdragon mixer + ADPCM streaming from ROM).
 *
 * Force-included via -include in CMakeLists.txt.
 */

#ifndef N64_SDL_MIXER_SHIM_HPP
#define N64_SDL_MIXER_SHIM_HPP

#ifdef N64_BUILD

// Block real SDL_mixer headers
#define SDL_MIXER_H_
#define _SDL_MIXER_H
#define SDL_mixer_h_

#include <cstdint>

// -----------------------------------------------------------------------
// SDL_mixer type stubs
// -----------------------------------------------------------------------
#define MIX_DEFAULT_FREQUENCY 22050
#define MIX_DEFAULT_FORMAT    0x8010  // AUDIO_S16LSB
#define MIX_DEFAULT_CHANNELS  2
#define MIX_MAX_VOLUME        128

#define MIX_CHANNEL_POST (-2)

typedef struct Mix_Chunk {
    int allocated;
    uint8_t* abuf;
    uint32_t alen;
    uint8_t volume;
} Mix_Chunk;

typedef struct _Mix_Music Mix_Music;
struct _Mix_Music {
    int type;
};

typedef enum {
    MUS_NONE = 0,
    MUS_CMD,
    MUS_WAV,
    MUS_MOD,
    MUS_MID,
    MUS_OGG,
    MUS_MP3,
    MUS_FLAC,
    MUS_OPUS
} Mix_MusicType;

typedef enum {
    MIX_INIT_FLAC   = 0x00000001,
    MIX_INIT_MOD    = 0x00000002,
    MIX_INIT_MP3    = 0x00000008,
    MIX_INIT_OGG    = 0x00000010,
    MIX_INIT_MID    = 0x00000020,
    MIX_INIT_OPUS   = 0x00000040
} MIX_InitFlags;

// Fading status
typedef enum {
    MIX_NO_FADING = 0,
    MIX_FADING_OUT,
    MIX_FADING_IN
} Mix_Fading;

// -----------------------------------------------------------------------
// SDL_mixer function stubs — route to n64_audio where applicable
// -----------------------------------------------------------------------

// Initialization
inline int Mix_Init(int) { return 0; }
inline void Mix_Quit() {}
inline int Mix_OpenAudio(int, uint16_t, int, int) { return 0; }
inline int Mix_OpenAudioDevice(int, uint16_t, int, int, const char*, int) { return 0; }
inline void Mix_CloseAudio() {}
inline int Mix_QuerySpec(int* freq, uint16_t* fmt, int* ch) {
    if (freq) *freq = 22050;
    if (fmt) *fmt = MIX_DEFAULT_FORMAT;
    if (ch) *ch = 2;
    return 1;
}

// Channels
inline int Mix_AllocateChannels(int numchans) { return numchans; }
inline int Mix_Volume(int, int vol) { return vol; }
inline int Mix_VolumeChunk(Mix_Chunk*, int vol) { return vol; }

// Chunks (sound effects)
inline Mix_Chunk* Mix_LoadWAV_RW(void*, int) { return nullptr; }
inline Mix_Chunk* Mix_LoadWAV(const char*) { return nullptr; }
inline void Mix_FreeChunk(Mix_Chunk*) {}
inline int Mix_PlayChannel(int, Mix_Chunk*, int) { return 0; }
inline int Mix_PlayChannelTimed(int, Mix_Chunk*, int, int) { return 0; }
inline int Mix_FadeInChannel(int, Mix_Chunk*, int, int) { return 0; }
inline int Mix_FadeInChannelTimed(int, Mix_Chunk*, int, int, int) { return 0; }
inline int Mix_HaltChannel(int) { return 0; }
inline int Mix_ExpireChannel(int, int) { return 0; }
inline int Mix_FadeOutChannel(int, int) { return 0; }
inline int Mix_Playing(int) { return 0; }
inline int Mix_Paused(int) { return 0; }
inline void Mix_Pause(int) {}
inline void Mix_Resume(int) {}
inline Mix_Fading Mix_FadingChannel(int) { return MIX_NO_FADING; }
inline Mix_Chunk* Mix_GetChunk(int) { return nullptr; }

// Music
inline Mix_Music* Mix_LoadMUS(const char*) { return nullptr; }
inline Mix_Music* Mix_LoadMUS_RW(void*, int) { return nullptr; }
inline void Mix_FreeMusic(Mix_Music*) {}
inline int Mix_PlayMusic(Mix_Music*, int) { return 0; }
inline int Mix_FadeInMusic(Mix_Music*, int, int) { return 0; }
inline int Mix_FadeInMusicPos(Mix_Music*, int, int, double) { return 0; }
inline int Mix_VolumeMusic(int vol) { return vol; }
inline int Mix_HaltMusic() { return 0; }
inline int Mix_FadeOutMusic(int) { return 0; }
inline void Mix_PauseMusic() {}
inline void Mix_ResumeMusic() {}
inline void Mix_RewindMusic() {}
inline int Mix_PausedMusic() { return 0; }
inline int Mix_PlayingMusic() { return 0; }
inline int Mix_SetMusicPosition(double) { return 0; }
inline Mix_Fading Mix_FadingMusic() { return MIX_NO_FADING; }
inline Mix_MusicType Mix_GetMusicType(const Mix_Music*) { return MUS_NONE; }
inline const char* Mix_GetMusicTitle(const Mix_Music*) { return ""; }
inline const char* Mix_GetMusicArtistTag(const Mix_Music*) { return ""; }

// Hooks
typedef void (*Mix_EffectFunc_t)(int, void*, int, void*);
typedef void (*Mix_EffectDone_t)(int, void*);

inline void Mix_ChannelFinished(void (*)(int)) {}
inline void Mix_HookMusic(void (*)(void*, uint8_t*, int), void*) {}
inline void Mix_HookMusicFinished(void (*)()) {}
inline int Mix_RegisterEffect(int, Mix_EffectFunc_t, Mix_EffectDone_t, void*) { return 0; }
inline int Mix_UnregisterEffect(int, Mix_EffectFunc_t) { return 0; }
inline int Mix_UnregisterAllEffects(int) { return 0; }

// Panning and positional
inline int Mix_SetPanning(int, uint8_t, uint8_t) { return 1; }
inline int Mix_SetPosition(int, int16_t, uint8_t) { return 1; }
inline int Mix_SetDistance(int, uint8_t) { return 1; }
inline int Mix_SetReverseStereo(int, int) { return 1; }

// Groups
inline int Mix_GroupChannel(int, int) { return 0; }
inline int Mix_GroupChannels(int, int, int) { return 0; }
inline int Mix_GroupAvailable(int) { return -1; }
inline int Mix_GroupCount(int) { return 0; }
inline int Mix_GroupOldest(int) { return -1; }
inline int Mix_GroupNewer(int) { return -1; }
inline int Mix_HaltGroup(int) { return 0; }
inline int Mix_FadeOutGroup(int, int) { return 0; }

// Error
inline const char* Mix_GetError() { return "N64: SDL_mixer not available"; }

#endif // N64_BUILD
#endif // N64_SDL_MIXER_SHIM_HPP
