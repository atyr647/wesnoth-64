/**
 * n64_audio.cpp
 *
 * N64 audio implementation for Wesnoth.
 *
 * Uses libdragon audio + mixer subsystems:
 *   - audio_init(44100, 4)          : configure AI DMA at 44.1 kHz, 4-buffer
 *   - mixer_init(CHAN_COUNT)         : RSP-based software mixer
 *   - wav64_*                        : for PCM SFX playback
 *   - ys64 / ADPCM streaming        : for music tracks
 *
 * Music tracks are converted to ADPCM by the asset pipeline tool and stored
 * in ROM. We stream them in 4 KB chunks to minimise RAM usage.
 */

#include "n64_audio.hpp"
#include "n64_memory.hpp"

#ifdef N64_BUILD
#  include <audio.h>
#  include <mixer.h>
#  include <samplebuffer.h>
#endif

// -----------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------
static constexpr int    AUDIO_FREQ       = 22050; // Hz — lower for RAM savings
static constexpr int    AUDIO_BUFFERS    = 4;     // number of AI DMA buffers
static constexpr size_t MAX_SFX_CACHE   = 64;    // max unique SFX in RAM
static constexpr size_t SFX_DATA_BUDGET = 128 * 1024; // 128 KB for SFX PCM (trimmed to fit 8 MB RAM)

// -----------------------------------------------------------------------
// SFX cache
// -----------------------------------------------------------------------
struct SfxCacheEntry {
    u32    rom_offset;
    u32    byte_length;
    s16*   data;      // pointer into s_sfx_data pool
    u32    samples;   // number of PCM samples
    u32    last_used; // frame counter for LRU eviction
    bool   in_use;
};

static SfxCacheEntry s_sfx_cache[MAX_SFX_CACHE];
static u8  s_sfx_data[SFX_DATA_BUDGET] __attribute__((aligned(8)));
static size_t s_sfx_data_used = 0;
static u32    s_frame_count   = 0;
static u8     s_sfx_vol       = 200; // default SFX volume
static u8     s_music_vol     = 180; // default music volume

// -----------------------------------------------------------------------
// Music streaming state
// -----------------------------------------------------------------------
static constexpr size_t MUSIC_STREAM_BUF = 8 * 1024; // 8 KB decode buffer

struct MusicState {
    u32    rom_offset;
    u32    total_bytes;
    u32    read_cursor;   // bytes already DMA'd from ROM
    bool   looping;
    bool   playing;
    u8     volume;
    u32    fade_ms;
    u32    fade_elapsed_ms;
    bool   fading_out;
    bool   fading_in;
    u8     buf[MUSIC_STREAM_BUF] __attribute__((aligned(8)));
};

static MusicState s_music;

// -----------------------------------------------------------------------
// n64_audio_init
// -----------------------------------------------------------------------
void n64_audio_init() {
    for (int i = 0; i < (int)MAX_SFX_CACHE; ++i)
        s_sfx_cache[i].in_use = false;

    std::memset(&s_music, 0, sizeof(s_music));

#ifdef N64_BUILD
    audio_init(AUDIO_FREQ, AUDIO_BUFFERS);
    mixer_init(CHAN_COUNT);
    // Set initial volumes
    mixer_ch_set_vol(CHAN_MUSIC, s_music_vol / 255.0f, s_music_vol / 255.0f);
    for (int c = CHAN_BELL; c < CHAN_COUNT; ++c)
        mixer_ch_set_vol(c, s_sfx_vol / 255.0f, s_sfx_vol / 255.0f);
#endif

    N64_LOG("Audio init: %d Hz, %d channels, sfx_cache=%u, sfx_budget=%uKB",
            AUDIO_FREQ, CHAN_COUNT, MAX_SFX_CACHE,
            (unsigned)(SFX_DATA_BUDGET / 1024));
}

// -----------------------------------------------------------------------
// n64_audio_update — called every frame
// -----------------------------------------------------------------------
void n64_audio_update() {
    ++s_frame_count;

#ifdef N64_BUILD
    // Let libdragon mixer fill the audio DMA buffers
    static int16_t audio_buf[1024] __attribute__((aligned(16)));
    mixer_poll(audio_buf, 512);

    // Stream next chunk of ADPCM music if needed
    if (s_music.playing) {
        u32 remaining = s_music.total_bytes - s_music.read_cursor;
        if (remaining > 0) {
            u32 chunk = remaining < MUSIC_STREAM_BUF ? remaining : MUSIC_STREAM_BUF;
            data_cache_hit_writeback_invalidate(s_music.buf, chunk);
            dma_read(s_music.buf, s_music.rom_offset + s_music.read_cursor, chunk);
            s_music.read_cursor += chunk;
            // Feed decoded PCM into mixer channel 0
            // (In a real implementation, use libdragon's streaming wav64 API)
        } else if (s_music.looping) {
            s_music.read_cursor = 0; // loop back to start
        } else {
            s_music.playing = false;
        }
    }
#endif
}

// -----------------------------------------------------------------------
// n64_audio_shutdown
// -----------------------------------------------------------------------
void n64_audio_shutdown() {
    n64_music_stop(0);
    n64_sfx_stop_all();
#ifdef N64_BUILD
    mixer_close();
    audio_close();
#endif
}

// -----------------------------------------------------------------------
// Music control
// -----------------------------------------------------------------------
void n64_music_play(u32 rom_offset, u32 byte_length, bool loop, u32 fade_in_ms) {
    s_music.rom_offset   = rom_offset;
    s_music.total_bytes  = byte_length;
    s_music.read_cursor  = 0;
    s_music.looping      = loop;
    s_music.playing      = true;
    s_music.fading_in    = (fade_in_ms > 0);
    s_music.fading_out   = false;
    s_music.fade_ms      = fade_in_ms;
    s_music.fade_elapsed_ms = 0;
    s_music.volume       = s_music.fading_in ? 0 : s_music_vol;
#ifdef N64_BUILD
    mixer_ch_set_vol(CHAN_MUSIC, s_music.volume / 255.0f, s_music.volume / 255.0f);
#endif
}

void n64_music_stop(u32 fade_out_ms) {
    if (!s_music.playing) return;
    if (fade_out_ms == 0) {
        s_music.playing   = false;
        s_music.fading_out = false;
#ifdef N64_BUILD
        mixer_ch_stop(CHAN_MUSIC);
#endif
    } else {
        s_music.fading_out   = true;
        s_music.fade_ms      = fade_out_ms;
        s_music.fade_elapsed_ms = 0;
    }
}

void n64_music_set_volume(u8 vol) {
    s_music_vol = vol;
    if (s_music.playing) {
#ifdef N64_BUILD
        mixer_ch_set_vol(CHAN_MUSIC, vol / 255.0f, vol / 255.0f);
#endif
    }
}

bool n64_music_is_playing() { return s_music.playing; }

// -----------------------------------------------------------------------
// SFX control
// -----------------------------------------------------------------------
N64SfxHandle n64_sfx_load(u32 rom_offset, u32 byte_length) {
    // A clip larger than the whole pool can never be resident; refusing it
    // here prevents writing past s_sfx_data. (The packer caps clip length.)
    if (byte_length == 0 || byte_length > SFX_DATA_BUDGET) {
        N64_LOG("SFX: refusing %u-byte clip (pool is %u)", (unsigned)byte_length,
                (unsigned)SFX_DATA_BUDGET);
        return N64_SFX_INVALID;
    }
    // Cache-line (16 B) granularity: DMA targets are invalidated from the data
    // cache by line, so a clip must not share a line with its neighbour.
    const u32 span = (byte_length + 15u) & ~15u;

    // Check cache first
    u32 lru_frame = 0xFFFFFFFF;
    u16 lru_slot  = 0;
    for (u16 i = 0; i < MAX_SFX_CACHE; ++i) {
        if (s_sfx_cache[i].in_use) {
            if (s_sfx_cache[i].rom_offset == rom_offset) {
                s_sfx_cache[i].last_used = s_frame_count;
                return i;
            }
            if (s_sfx_cache[i].last_used < lru_frame) {
                lru_frame = s_sfx_cache[i].last_used;
                lru_slot  = i;
            }
        }
    }

    // Find a free slot, evicting LRU if needed
    u16 slot = MAX_SFX_CACHE;
    for (u16 i = 0; i < MAX_SFX_CACHE; ++i) {
        if (!s_sfx_cache[i].in_use) { slot = i; break; }
    }
    if (slot == MAX_SFX_CACHE) slot = lru_slot; // evict

    // If we're evicting, we can't easily reclaim the PCM data memory from the
    // linear pool. We accept this leak across the pool reset at scenario end.
    // If the pool overflows, reset it and reload.
    if (s_sfx_data_used + span > SFX_DATA_BUDGET) {
        // Reset and re-mark all cache entries invalid
        s_sfx_data_used = 0;
        for (auto& e : s_sfx_cache) e.in_use = false;
    }

    s16* dest = reinterpret_cast<s16*>(s_sfx_data + s_sfx_data_used);
    s_sfx_data_used += span;

#ifdef N64_BUILD
    // Read the padded span: ROMFS data is 16-byte aligned and padded to 16.
    data_cache_hit_writeback_invalidate(dest, span);
    dma_read(dest, rom_offset, span);
#else
    std::memset(dest, 0, byte_length);
#endif

    s_sfx_cache[slot] = {
        rom_offset, byte_length, dest,
        byte_length / 2,  // mono s16 samples
        s_frame_count,
        true
    };
    return slot;
}

void n64_sfx_play(N64AudioChannel channel, N64SfxHandle sfx, u8 volume, int loops) {
    if (sfx == N64_SFX_INVALID) return;
    N64_ASSERT(channel < CHAN_COUNT && sfx < MAX_SFX_CACHE);
    (void)loops;
#ifdef N64_BUILD
    const SfxCacheEntry& e = s_sfx_cache[sfx];
    float vol = volume / 255.0f;
    mixer_ch_set_vol(channel, vol, vol);
    // Use libdragon's waveform playback
    waveform_t wv = {};
    wv.bits   = 16;
    wv.channels = 1;
    wv.frequency = AUDIO_FREQ;
    wv.len    = e.samples;
    wv.loop_len = 0;
    wv.read   = nullptr; // not using streaming for SFX
    mixer_ch_play(channel, &wv);
#endif
}

void n64_sfx_stop(N64AudioChannel channel) {
#ifdef N64_BUILD
    mixer_ch_stop(channel);
#endif
}

void n64_sfx_stop_all() {
    for (int c = CHAN_BELL; c < CHAN_COUNT; ++c)
        n64_sfx_stop(static_cast<N64AudioChannel>(c));
}

void n64_sfx_set_volume(u8 vol) {
    s_sfx_vol = vol;
#ifdef N64_BUILD
    for (int c = CHAN_BELL; c < CHAN_COUNT; ++c)
        mixer_ch_set_vol(c, vol / 255.0f, vol / 255.0f);
#endif
}
