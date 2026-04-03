/**
 * image_converter.cpp
 *
 * Converts PNG images to N64 RGBA5551 format.
 *
 * Process:
 *   1. Decode PNG via libpng → 32-bit RGBA
 *   2. Downscale to N64 resolution (1/4 by default: 72px tiles → 18px)
 *   3. Round up to nearest power-of-2 dimensions (N64 TMEM requirement)
 *   4. Quantise each pixel to RGBA5551 (5 bits per RGB channel, 1-bit alpha)
 *
 * The texture scale factor is 4 (quarter resolution) by default, matching
 * the N64's 320x240 vs Wesnoth's 1280x720 resolution ratio.
 */

#include "pipeline.hpp"
#include <png.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

namespace n64pack {

// -----------------------------------------------------------------------
// PNG loader — returns RGBA8888 row data
// -----------------------------------------------------------------------
struct Rgba8Image {
    std::vector<uint8_t> pixels;  // RGBA, row-major
    int width, height;
};

static Rgba8Image load_png(const std::filesystem::path& path) {
    Rgba8Image img{};
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return img;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { std::fclose(fp); return img; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, nullptr, nullptr); std::fclose(fp); return img; }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return img;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    int width      = png_get_image_width(png, info);
    int height     = png_get_image_height(png, info);
    int color_type = png_get_color_type(png, info);
    int bit_depth  = png_get_bit_depth(png, info);

    // Normalise to 8-bit RGBA
    if (bit_depth == 16)               png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE) png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    std::vector<png_bytep> rows(height);
    img.pixels.resize(width * height * 4);
    for (int y = 0; y < height; ++y)
        rows[y] = img.pixels.data() + y * width * 4;

    png_read_image(png, rows.data());
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(fp);

    img.width  = width;
    img.height = height;
    return img;
}

// -----------------------------------------------------------------------
// Box-filter downscale (RGBA8888)
// -----------------------------------------------------------------------
static Rgba8Image downscale(const Rgba8Image& src, int dst_w, int dst_h) {
    Rgba8Image dst;
    dst.width  = dst_w;
    dst.height = dst_h;
    dst.pixels.resize(dst_w * dst_h * 4, 0);

    float sx = (float)src.width  / dst_w;
    float sy = (float)src.height / dst_h;

    for (int dy = 0; dy < dst_h; ++dy) {
        for (int dx = 0; dx < dst_w; ++dx) {
            float r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            int src_x0 = (int)(dx * sx),       src_x1 = (int)((dx + 1) * sx);
            int src_y0 = (int)(dy * sy),        src_y1 = (int)((dy + 1) * sy);
            src_x1 = std::min(src_x1, src.width);
            src_y1 = std::min(src_y1, src.height);
            for (int sy_ = src_y0; sy_ < src_y1; ++sy_) {
                for (int sx_ = src_x0; sx_ < src_x1; ++sx_) {
                    const uint8_t* px = src.pixels.data() + (sy_ * src.width + sx_) * 4;
                    r += px[0]; g += px[1]; b += px[2]; a += px[3];
                    ++count;
                }
            }
            if (count > 0) {
                uint8_t* out = dst.pixels.data() + (dy * dst_w + dx) * 4;
                out[0] = (uint8_t)(r / count);
                out[1] = (uint8_t)(g / count);
                out[2] = (uint8_t)(b / count);
                out[3] = (uint8_t)(a / count);
            }
        }
    }
    return dst;
}

// -----------------------------------------------------------------------
// Convert RGBA8888 → RGBA5551 (N64 native format)
// -----------------------------------------------------------------------
static uint16_t rgba8_to_5551(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint16_t)(
        ((r >> 3) << 11) |
        ((g >> 3) <<  6) |
        ((b >> 3) <<  1) |
        (a >= 128 ? 1 : 0)
    );
}

// -----------------------------------------------------------------------
// Next power of 2
// -----------------------------------------------------------------------
static int next_pow2(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

// -----------------------------------------------------------------------
// convert_image — main entry point
// -----------------------------------------------------------------------
Texture16 convert_image(const std::filesystem::path& png_path,
                        int target_w, int target_h) {
    Texture16 result{};

    Rgba8Image src = load_png(png_path);
    if (src.pixels.empty() || src.width == 0 || src.height == 0) return result;

    // If target dimensions not specified, default to 1/4 scale
    if (target_w <= 0) target_w = std::max(1, src.width  / 4);
    if (target_h <= 0) target_h = std::max(1, src.height / 4);

    // Round up to power-of-2 for N64 TMEM
    int tex_w = next_pow2(target_w);
    int tex_h = next_pow2(target_h);

    // Clamp to N64 TMEM limits:
    //   max texture size with RGBA16: sqrt(4096/2) = ~45px per side
    //   practical safe limit: 32x32 (fits cleanly in TMEM)
    tex_w = std::min(tex_w, 64);
    tex_h = std::min(tex_h, 64);

    // Downscale source to final texture dimensions
    Rgba8Image scaled = (src.width != tex_w || src.height != tex_h)
                        ? downscale(src, tex_w, tex_h)
                        : src;

    // Convert to RGBA5551
    result.width  = (uint16_t)tex_w;
    result.height = (uint16_t)tex_h;
    result.pixels.resize(tex_w * tex_h);

    for (int i = 0; i < tex_w * tex_h; ++i) {
        const uint8_t* px = scaled.pixels.data() + i * 4;
        result.pixels[i] = rgba8_to_5551(px[0], px[1], px[2], px[3]);
    }

    return result;
}

} // namespace n64pack
