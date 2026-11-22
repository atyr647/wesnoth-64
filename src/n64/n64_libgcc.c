/**
 * n64_libgcc.c
 *
 * Minimal libgcc helper functions needed when targeting MIPS with -mabi=32.
 * The default mips64-elf libgcc was compiled for 64-bit long, so its
 * __divdi3 / __moddi3 / __udivdi3 / __umoddi3 functions actually correspond
 * to 128-bit operations (__divti3 etc.) and don't help us at all.
 *
 * Likewise, __clzsi2 / __ctzsi2 are missing because the default libgcc
 * uses native MIPS64 hardware for 32-bit count operations.
 *
 * These implementations are public-domain / textbook.
 */

#ifdef N64_BUILD

#include <stdint.h>

/* ---------------------------------------------------------------- */
/* Count leading zeros in a 32-bit word                              */
/* ---------------------------------------------------------------- */
int __clzsi2(unsigned int x) {
    int n = 0;
    if (x == 0) return 32;
    if (!(x & 0xFFFF0000U)) { n += 16; x <<= 16; }
    if (!(x & 0xFF000000U)) { n +=  8; x <<=  8; }
    if (!(x & 0xF0000000U)) { n +=  4; x <<=  4; }
    if (!(x & 0xC0000000U)) { n +=  2; x <<=  2; }
    if (!(x & 0x80000000U)) { n +=  1; }
    return n;
}

/* ---------------------------------------------------------------- */
/* Count trailing zeros in a 32-bit word                             */
/* ---------------------------------------------------------------- */
int __ctzsi2(unsigned int x) {
    int n = 0;
    if (x == 0) return 32;
    if (!(x & 0x0000FFFFU)) { n += 16; x >>= 16; }
    if (!(x & 0x000000FFU)) { n +=  8; x >>=  8; }
    if (!(x & 0x0000000FU)) { n +=  4; x >>=  4; }
    if (!(x & 0x00000003U)) { n +=  2; x >>=  2; }
    if (!(x & 0x00000001U)) { n +=  1; }
    return n;
}

/* ---------------------------------------------------------------- */
/* 64-bit shift helpers                                              */
/* The default mips64-elf libgcc only ships these for the 64-bit-long*/
/* ABI; we need our own for our 32-bit-long ABI build.               */
/* ---------------------------------------------------------------- */
uint64_t __lshrdi3(uint64_t a, int b) {
    b &= 63;
    return a >> b;
}

uint64_t __ashldi3(uint64_t a, int b) {
    b &= 63;
    return a << b;
}

int64_t __ashrdi3(int64_t a, int b) {
    b &= 63;
    return a >> b;
}

/* ---------------------------------------------------------------- */
/* 64-bit unsigned division and remainder                            */
/* Shift-subtract algorithm.                                         */
/* ---------------------------------------------------------------- */
static uint64_t udivmoddi4(uint64_t num, uint64_t den, uint64_t* rem_p) {
    uint64_t quot = 0, rem = 0;
    int i;
    for (i = 63; i >= 0; --i) {
        rem = (rem << 1) | ((num >> i) & 1U);
        if (rem >= den) {
            rem -= den;
            quot |= (uint64_t)1 << i;
        }
    }
    if (rem_p) *rem_p = rem;
    return quot;
}

uint64_t __udivdi3(uint64_t a, uint64_t b) {
    return udivmoddi4(a, b, 0);
}

uint64_t __umoddi3(uint64_t a, uint64_t b) {
    uint64_t r;
    udivmoddi4(a, b, &r);
    return r;
}

/* ---------------------------------------------------------------- */
/* 64-bit signed division and remainder                              */
/* ---------------------------------------------------------------- */
int64_t __divdi3(int64_t a, int64_t b) {
    int neg = 0;
    uint64_t ua, ub, uq;
    if (a < 0) { ua = (uint64_t)-a; neg = !neg; } else ua = (uint64_t)a;
    if (b < 0) { ub = (uint64_t)-b; neg = !neg; } else ub = (uint64_t)b;
    uq = udivmoddi4(ua, ub, 0);
    return neg ? -(int64_t)uq : (int64_t)uq;
}

int64_t __moddi3(int64_t a, int64_t b) {
    int neg = 0;
    uint64_t ua, ub, ur;
    if (a < 0) { ua = (uint64_t)-a; neg = 1; } else ua = (uint64_t)a;
    if (b < 0) { ub = (uint64_t)-b; }       else ub = (uint64_t)b;
    udivmoddi4(ua, ub, &ur);
    return neg ? -(int64_t)ur : (int64_t)ur;
}

/* ---------------------------------------------------------------- */
/* libc stubs that newlib expects to find on POSIX                    */
/* ---------------------------------------------------------------- */
int getentropy(void* buf, unsigned int n) {
    /* Return all zeros — not cryptographically secure, just enough
       to satisfy std::random_device on N64. */
    unsigned char* p = (unsigned char*)buf;
    unsigned int i;
    for (i = 0; i < n; ++i) p[i] = 0;
    return 0;
}

int geteuid(void) { return 0; }

/* newlib has no dup2; log.cpp treats failure as "could not redirect". */
#include <errno.h>
int dup2(int oldfd, int newfd) { (void)oldfd; (void)newfd; errno = ENOSYS; return -1; }

struct passwd; /* opaque */
struct passwd* getpwuid(int uid) { (void)uid; return 0; }

#endif /* N64_BUILD */
