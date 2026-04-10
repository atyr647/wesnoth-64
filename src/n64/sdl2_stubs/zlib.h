/* Minimal zlib stub for N64 build — no actual compression */
#ifndef ZLIB_H_N64_STUB
#define ZLIB_H_N64_STUB

#include <stddef.h>

typedef unsigned long uLong;
typedef unsigned char Byte;
typedef Byte Bytef;
typedef uLong uLongf;

#define Z_OK 0
#define Z_MEM_ERROR (-4)
#define Z_BUF_ERROR (-5)
#define Z_DEFAULT_COMPRESSION (-1)
#define Z_BEST_COMPRESSION 9

/* Store data without compression on N64 */
static inline uLong compressBound(uLong sourceLen) {
    return sourceLen + 12; /* small overhead for header */
}

static inline int compress2(Bytef* dest, uLongf* destLen,
                             const Bytef* source, uLong sourceLen, int level) {
    (void)level;
    if (*destLen < sourceLen) return Z_BUF_ERROR;
    for (uLong i = 0; i < sourceLen; i++) dest[i] = source[i];
    *destLen = sourceLen;
    return Z_OK;
}

static inline int uncompress(Bytef* dest, uLongf* destLen,
                              const Bytef* source, uLong sourceLen) {
    if (*destLen < sourceLen) return Z_BUF_ERROR;
    for (uLong i = 0; i < sourceLen; i++) dest[i] = source[i];
    *destLen = sourceLen;
    return Z_OK;
}

#endif /* ZLIB_H_N64_STUB */
