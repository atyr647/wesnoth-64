#pragma once

/**
 * n64_audio.hpp
 *
 * N64 audio subsystem for Wesnoth.
 *
 * Replaces src/sound.cpp / SDL_mixer.
 *
 * Architecture:
 *   - Music: ADPCM streams read directly from ROM, decoded by RSP in chunks
 *   - SFX:   Raw PCM (signed 16-bit, mono, 22050 Hz) preloaded into RAM cache
 *   - Mixer: libdragon audio + mixer for up to 8 simultaneous channels
 *
 * Channel assignment (mirrors src/sound.hpp SoundChannel enum):
 *   0   CHAN_MUSIC      background music (streams from ROM)
 *   1   CHAN_BELL       turn bell / alert sound
 *   2   CHAN_TIMER      clock sound for MP timer
 *   3   CHAN_UI0        UI feedback sounds
 *   4   CHAN_UI1
 *   5-7 CHAN_FX0-2      unit combat / ambient sounds
 */

#include "n64_platform.hpp"

// -----------------------------------------------------------------------
// Channel identifiers (kept compatible with src/sound.hpp SoundChannel)
// -----------------------------------------------------------------------
enum N64AudioChannel : u8 {
    CHAN_MUSIC  = 0,
    CHAN_BELL   = 1,
    CHAN_TIMER  = 2,
    CHAN_UI0    = 3,
    CHAN_UI1    = 4,
    CHAN_FX0    = 5,
    CHAN_FX1    = 6,
    CHAN_FX2    = 7,
    CHAN_COUNT  = 8,
};

// -----------------------------------------------------------------------
// SFX handle — index into the in-RAM SFX cache (max 64 unique SFX)
// -----------------------------------------------------------------------
using N64SfxHandle = u16;
static constexpr N64SfxHandle N64_SFX_INVALID = 0xFFFF;

// -----------------------------------------------------------------------
// Audio subsystem API
// -----------------------------------------------------------------------

// Initialise audio hardware and RSP mixer. Call once at startup.
void n64_audio_init();

// Update: pump ADPCM music decoder and RSP mixer. Call every frame.
void n64_audio_update();

// Shutdown: stop all channels, release resources.
void n64_audio_shutdown();

// --- Music control (ADPCM streaming from ROM) --------------------------

// Start playing a music track. 'rom_offset' points to the ADPCM data in ROM.
// 'loop' controls whether the track loops.
// 'fade_in_ms' fades in over that many milliseconds (0 = instant).
void n64_music_play(u32 rom_offset, u32 byte_length, bool loop, u32 fade_in_ms);

// Stop music with optional fade-out duration (mirrors sound::stop_music).
void n64_music_stop(u32 fade_out_ms);

// Set music volume (0-255, mirrors MusicController).
void n64_music_set_volume(u8 vol);

// Returns true if music is currently playing.
bool n64_music_is_playing();

// --- SFX control (PCM from RAM cache) -----------------------------------

// Load a SFX from ROM into the RAM cache. Returns a handle.
// 'rom_offset' and 'byte_length' are from the ROM filesystem.
// Cache holds up to 64 SFX; evicts LRU entry if full.
N64SfxHandle n64_sfx_load(u32 rom_offset, u32 byte_length);

// Play a SFX on the given channel.
// 'volume' is 0-255. 'loops' is how many times to repeat (0 = play once).
void n64_sfx_play(N64AudioChannel channel, N64SfxHandle sfx,
                  u8 volume, int loops);

// Stop a specific channel.
void n64_sfx_stop(N64AudioChannel channel);

// Stop all SFX channels (does not stop music).
void n64_sfx_stop_all();

// Set global SFX volume (0-255).
void n64_sfx_set_volume(u8 vol);
