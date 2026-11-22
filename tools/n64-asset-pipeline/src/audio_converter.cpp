/**
 * audio_converter.cpp
 *
 * Converts OGG/WAV audio to N64 formats:
 *
 *   SFX   → signed 16-bit PCM, mono, 22050 Hz
 *   Music → 4-bit ADPCM (IMA ADPCM), mono, 22050 Hz
 *           This achieves ~4:1 compression vs raw PCM.
 *           The libdragon RSP can decode IMA ADPCM in real-time.
 *
 * OGG decoding requires libvorbis (optional, guarded by HAVE_VORBIS).
 * If not available, OGG files are skipped with a warning.
 * WAV files are decoded using a minimal built-in parser.
 */

#include "pipeline.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <stdint.h>

#ifdef HAVE_VORBIS
#  include <vorbis/vorbisfile.h>
#endif

namespace n64pack {

static constexpr int TARGET_RATE = 22050;

// -----------------------------------------------------------------------
// WAV decoder (handles PCM WAV only)
// -----------------------------------------------------------------------
static uint32_t rd_u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd_u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static std::vector<int16_t> decode_wav(const std::filesystem::path& path) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return {};
    std::vector<uint8_t> file;
    {
        uint8_t buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) file.insert(file.end(), buf, buf + n);
    }
    std::fclose(fp);

    if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) || std::memcmp(file.data() + 8, "WAVE", 4))
        return {};

    uint16_t audio_format = 0, channels = 0, bits = 0;
    uint32_t sample_rate = 0;
    const uint8_t* data = nullptr;
    size_t data_len = 0;

    // Walk the RIFF chunks (fmt may be 16, 18 or 40 bytes; chunks are word padded).
    size_t pos = 12;
    while (pos + 8 <= file.size()) {
        const uint8_t* ck = file.data() + pos;
        size_t sz = rd_u32(ck + 4);
        size_t body = pos + 8;
        size_t avail = file.size() - body;
        if (std::memcmp(ck, "fmt ", 4) == 0 && sz >= 16 && avail >= 16) {
            audio_format = rd_u16(file.data() + body);
            channels     = rd_u16(file.data() + body + 2);
            sample_rate  = rd_u32(file.data() + body + 4);
            bits         = rd_u16(file.data() + body + 14);
            if (audio_format == 0xFFFE && sz >= 26 && avail >= 26)   // WAVE_FORMAT_EXTENSIBLE
                audio_format = rd_u16(file.data() + body + 24);
        } else if (std::memcmp(ck, "data", 4) == 0) {
            data = file.data() + body;
            data_len = std::min(sz, avail);   // tolerate bogus/streaming sizes
            break;
        }
        if (sz > avail) break;
        pos = body + sz + (sz & 1);
    }

    if (!data) return {};
    if (audio_format != 1) {
        std::fprintf(stderr, "  WAV: non-PCM format %u, skipping %s\n", audio_format, path.c_str());
        return {};
    }
    if (channels == 0 || sample_rate == 0 || (bits != 8 && bits != 16)) {
        std::fprintf(stderr, "  WAV: unsupported layout (ch=%u bits=%u rate=%u), skipping %s\n",
                     channels, bits, sample_rate, path.c_str());
        return {};
    }

    const size_t frame = (size_t)channels * (bits / 8);
    const size_t sample_count = data_len / frame;
    std::vector<int16_t> out(sample_count);
    for (size_t i = 0; i < sample_count; ++i) {
        int32_t sum = 0;
        for (int c = 0; c < channels; ++c) {
            const uint8_t* q = data + i * frame + (size_t)c * (bits / 8);
            sum += (bits == 8) ? (int32_t)(q[0] - 128) * 256 : (int32_t)(int16_t)rd_u16(q);
        }
        out[i] = (int16_t)std::max(-32768, std::min(32767, sum / (int)channels));
    }

    // Resample if needed (simple linear interpolation)
    if ((int)sample_rate != TARGET_RATE) {
        double ratio = (double)sample_rate / TARGET_RATE;
        size_t new_count = (size_t)(sample_count / ratio);
        std::vector<int16_t> resampled(new_count);
        for (size_t i = 0; i < new_count; ++i) {
            double src_pos = i * ratio;
            size_t idx = (size_t)src_pos;
            double frac = src_pos - idx;
            int16_t a = (idx < sample_count) ? out[idx] : 0;
            int16_t b = (idx + 1 < sample_count) ? out[idx + 1] : 0;
            resampled[i] = (int16_t)(a + frac * (b - a));
        }
        out = std::move(resampled);
    }
    return out;
}

// -----------------------------------------------------------------------
// OGG/Vorbis decoder
// -----------------------------------------------------------------------
static std::vector<int16_t> decode_ogg(const std::filesystem::path& path) {
#ifdef HAVE_VORBIS
    OggVorbis_File vf;
    if (ov_fopen(path.c_str(), &vf) != 0) return {};

    vorbis_info* vi = ov_info(&vf, -1);
    int channels    = vi->channels;
    long src_rate   = vi->rate;

    std::vector<int16_t> raw;
    char buf[4096];
    int bitstream;
    long n;
    while ((n = ov_read(&vf, buf, sizeof(buf), 0, 2, 1, &bitstream)) > 0) {
        size_t samples = n / 2;
        const int16_t* data = reinterpret_cast<const int16_t*>(buf);
        // Downmix to mono
        for (size_t i = 0; i < samples / channels; ++i) {
            int32_t sum = 0;
            for (int c = 0; c < channels; ++c) sum += data[i * channels + c];
            raw.push_back((int16_t)(sum / channels));
        }
    }
    ov_clear(&vf);

    // Resample to TARGET_RATE
    if (src_rate != TARGET_RATE) {
        double ratio = (double)src_rate / TARGET_RATE;
        size_t new_count = (size_t)(raw.size() / ratio);
        std::vector<int16_t> resampled(new_count);
        for (size_t i = 0; i < new_count; ++i) {
            double pos = i * ratio;
            size_t idx = (size_t)pos;
            double frac = pos - idx;
            int16_t a = (idx < raw.size()) ? raw[idx] : 0;
            int16_t b = (idx + 1 < raw.size()) ? raw[idx + 1] : 0;
            resampled[i] = (int16_t)(a + frac * (b - a));
        }
        return resampled;
    }
    return raw;
#else
    std::fprintf(stderr, "  OGG: libvorbis not available, skipping %s\n", path.c_str());
    return {};
#endif
}

// -----------------------------------------------------------------------
// IMA ADPCM encoder (for music tracks)
//
// IMA ADPCM compresses 16-bit PCM at 4:1 ratio (4 bits per sample).
// Each block: 512 bytes header + nibble pairs.
// The libdragon ys64 player expects this format.
// -----------------------------------------------------------------------
static const int s_ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};
static const int s_ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34,
    37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
    544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
    1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
    13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static std::vector<uint8_t> encode_adpcm(const std::vector<int16_t>& pcm) {
    std::vector<uint8_t> out;
    if (pcm.empty()) return out;

    // Header: 4 bytes (predictor s16, step_index u8, reserved u8)
    int predictor   = 0;
    int step_index  = 0;

    // Reserve space for header
    out.push_back(0); out.push_back(0); // predictor
    out.push_back(0); out.push_back(0); // step_index + reserved

    bool odd = false;
    uint8_t packed = 0;

    for (int16_t sample : pcm) {
        int step   = s_ima_step_table[step_index];
        int diff   = (int)sample - predictor;
        int nibble = 0;

        if (diff < 0) { nibble = 8; diff = -diff; }
        if (diff >= step)     { nibble |= 4; diff -= step; }
        if (diff >= step / 2) { nibble |= 2; diff -= step / 2; }
        if (diff >= step / 4) { nibble |= 1; }

        // Update predictor
        step = s_ima_step_table[step_index];
        int delta = step / 8;
        if (nibble & 4) delta += step;
        if (nibble & 2) delta += step / 2;
        if (nibble & 1) delta += step / 4;
        if (nibble & 8) predictor -= delta;
        else            predictor += delta;

        predictor = std::max(-32768, std::min(32767, predictor));
        step_index = std::max(0, std::min(88, step_index + s_ima_index_table[nibble & 7]));

        if (!odd) {
            packed = (uint8_t)(nibble & 0x0F);
            odd = true;
        } else {
            packed |= (uint8_t)((nibble & 0x0F) << 4);
            out.push_back(packed);
            odd = false;
        }
    }
    if (odd) out.push_back(packed); // flush last nibble

    // Write header
    out[0] = (uint8_t)(pcm[0] & 0xFF);
    out[1] = (uint8_t)((pcm[0] >> 8) & 0xFF);
    out[2] = 0; // step_index starts at 0
    out[3] = 0;

    return out;
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------
std::vector<uint8_t> convert_sfx(const std::filesystem::path& path) {
    std::vector<int16_t> pcm;
    if (path.extension() == ".ogg") pcm = decode_ogg(path);
    else if (path.extension() == ".wav") pcm = decode_wav(path);
    if (pcm.empty()) return {};

    // Return raw s16 PCM as bytes
    std::vector<uint8_t> out(pcm.size() * 2);
    std::memcpy(out.data(), pcm.data(), out.size());
    return out;
}

std::vector<uint8_t> convert_music(const std::filesystem::path& path) {
    std::vector<int16_t> pcm;
    if (path.extension() == ".ogg") pcm = decode_ogg(path);
    else if (path.extension() == ".wav") pcm = decode_wav(path);
    if (pcm.empty()) return {};
    return encode_adpcm(pcm);
}

} // namespace n64pack
