/*
 * n64_md5.cpp -- MD5 (RFC 1321) behind the OpenSSL EVP digest calls that
 * src/hash.cpp makes (declared in n64_engine_prelude.hpp; there is no OpenSSL
 * on the console). Wesnoth names its config cache files by the MD5 of the
 * preprocessor defines, so a working digest is required for the cache.
 */
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

struct n64_md5_ctx {
    uint32_t a, b, c, d;
    uint64_t length;            // bytes processed
    unsigned char buffer[64];
    std::size_t used;           // bytes in buffer
};
struct n64_evp_md { int unused; };

namespace {

const n64_evp_md s_md5 = {0};

inline uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                   5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

void block(n64_md5_ctx* c, const unsigned char* p) {
    uint32_t m[16];
    for (int i = 0; i < 16; ++i)   // little-endian words
        m[i] = uint32_t(p[i * 4]) | uint32_t(p[i * 4 + 1]) << 8 | uint32_t(p[i * 4 + 2]) << 16 |
               uint32_t(p[i * 4 + 3]) << 24;
    uint32_t a = c->a, b = c->b, cc = c->c, d = c->d;
    for (int i = 0; i < 64; ++i) {
        uint32_t f;
        int g;
        if (i < 16)      { f = (b & cc) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & cc); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ cc ^ d;          g = (3 * i + 5) & 15; }
        else             { f = cc ^ (b | ~d);       g = (7 * i) & 15; }
        const uint32_t t = d;
        d = cc;
        cc = b;
        b = b + rol(a + f + K[i] + m[g], R[i]);
        a = t;
    }
    c->a += a; c->b += b; c->c += cc; c->d += d;
}

} // namespace

n64_md5_ctx* EVP_MD_CTX_new() { return new (std::nothrow) n64_md5_ctx(); }
void EVP_MD_CTX_free(n64_md5_ctx* c) { delete c; }
const n64_evp_md* EVP_md5() { return &s_md5; }

int EVP_DigestInit_ex(n64_md5_ctx* c, const n64_evp_md* md, void*) {
    if (!c || md != &s_md5) return 0;
    c->a = 0x67452301; c->b = 0xefcdab89; c->c = 0x98badcfe; c->d = 0x10325476;
    c->length = 0;
    c->used = 0;
    return 1;
}

int EVP_DigestUpdate(n64_md5_ctx* c, const void* data, std::size_t n) {
    if (!c) return 0;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    c->length += n;
    while (n > 0) {
        const std::size_t take = (64 - c->used < n) ? 64 - c->used : n;
        std::memcpy(c->buffer + c->used, p, take);
        c->used += take;
        p += take;
        n -= take;
        if (c->used == 64) { block(c, c->buffer); c->used = 0; }
    }
    return 1;
}

int EVP_DigestFinal_ex(n64_md5_ctx* c, unsigned char* out, unsigned int* len) {
    if (!c) return 0;
    const uint64_t bits = c->length * 8;
    static const unsigned char pad[64] = {0x80};
    const std::size_t padlen = (c->used < 56) ? 56 - c->used : 120 - c->used;
    EVP_DigestUpdate(c, pad, padlen);
    unsigned char lenbytes[8];
    for (int i = 0; i < 8; ++i) lenbytes[i] = static_cast<unsigned char>(bits >> (8 * i));
    EVP_DigestUpdate(c, lenbytes, 8);
    const uint32_t words[4] = {c->a, c->b, c->c, c->d};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<unsigned char>(words[i] >> (8 * j));
    if (len) *len = 16;
    return 1;
}
