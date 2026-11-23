#pragma once
/**
 * n64_engine_prelude.hpp -- force-included into every engine C++ source on N64.
 *
 * The engine uses the real SDL 2 and SDL_image (built for the console from
 * upstream sources, see cmake/n64_sdl2.cmake and src/n64/sdl2). What remains
 * here are version/feature stubs for libraries the console build leaves out
 * (libcurl and OpenSSL: online features only) and the platform basics.
 */
#ifdef N64_BUILD

// libcurl is not built (add-on server / online only); block its header.
#define CURLINC_CURL_H
#define __CURL_CURL_H

#include "n64_platform.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#ifndef BOOST_LIB_VERSION
#define BOOST_LIB_VERSION "1_85"
#endif

// SDL_mixer: still provided by n64_sdl_mixer_shim.hpp
#define SDL_MIXER_VERSION(x) do { (x)->major = 0; (x)->minor = 0; (x)->patch = 0; } while(0)
inline const SDL_version* Mix_Linked_Version() { static SDL_version v = {0,0,0}; return &v; }

// -----------------------------------------------------------------------
// libcurl stubs
// -----------------------------------------------------------------------
#ifndef LIBCURL_VERSION_NUM
#define LIBCURL_VERSION_NUM 0x075500
#endif

typedef int CURLversion;
#define CURLVERSION_NOW 0
struct curl_version_info_data { const char* version; };
inline curl_version_info_data* curl_version_info(CURLversion) {
    static curl_version_info_data d = { "0.0.0" };
    return &d;
}

// -----------------------------------------------------------------------
// Cairo / Pango stubs
// -----------------------------------------------------------------------
#ifndef CAIRO_VERSION_STRING
#define CAIRO_VERSION_STRING "0.0.0"
#endif
inline const char* cairo_version_string() { return "0.0.0"; }

#ifndef PANGO_VERSION_STRING
#define PANGO_VERSION_STRING "0.0.0"
#endif
inline const char* pango_version_string() { return "0.0.0"; }

// -----------------------------------------------------------------------
// OpenSSL version stubs (build_info.cpp uses these)
// -----------------------------------------------------------------------
#ifndef OPENSSL_OPENSSLV_H
#define OPENSSL_OPENSSLV_H
#define OPENSSL_VERSION_NUMBER 0x1010100fL
#endif

#ifndef OPENSSL_CRYPTO_H
#define OPENSSL_CRYPTO_H
inline long SSLeay() { return 0x1010100fL; }
#endif

// OpenSSL stubs — preferences.cpp includes these conditionally
#ifndef OPENSSL_EVP_H
#define OPENSSL_EVP_H
#define OPENSSL_ERR_H
// Stub the functions used by preferences for password hashing
typedef void EVP_MD_CTX;
typedef void EVP_MD;
inline EVP_MD_CTX* EVP_MD_CTX_new() { return nullptr; }
inline void EVP_MD_CTX_free(EVP_MD_CTX*) {}
inline const EVP_MD* EVP_md5() { return nullptr; }
inline const EVP_MD* EVP_sha256() { return nullptr; }
inline int EVP_MD_size(const EVP_MD*) { return 16; }
inline int EVP_DigestInit_ex(EVP_MD_CTX*, const EVP_MD*, void*) { return 0; }
inline int EVP_DigestUpdate(EVP_MD_CTX*, const void*, size_t) { return 0; }
inline int EVP_DigestFinal_ex(EVP_MD_CTX*, unsigned char*, unsigned int*) { return 0; }
// Cipher API: used only to store multiplayer server passwords (online play).
// Every call fails, so preferences.cpp logs the error and stores nothing.
typedef struct n64_evp_cipher_ctx EVP_CIPHER_CTX;
typedef struct n64_evp_cipher EVP_CIPHER;
typedef struct n64_engine ENGINE;
inline EVP_CIPHER_CTX* EVP_CIPHER_CTX_new() { return nullptr; }
inline void EVP_CIPHER_CTX_free(EVP_CIPHER_CTX*) {}
inline const EVP_CIPHER* EVP_aes_256_cbc() { return nullptr; }
inline int EVP_EncryptInit_ex(EVP_CIPHER_CTX*, const EVP_CIPHER*, ENGINE*, const unsigned char*, const unsigned char*) { return 0; }
inline int EVP_EncryptUpdate(EVP_CIPHER_CTX*, unsigned char*, int*, const unsigned char*, int) { return 0; }
inline int EVP_EncryptFinal_ex(EVP_CIPHER_CTX*, unsigned char*, int*) { return 0; }
inline int EVP_DecryptInit_ex(EVP_CIPHER_CTX*, const EVP_CIPHER*, ENGINE*, const unsigned char*, const unsigned char*) { return 0; }
inline int EVP_DecryptUpdate(EVP_CIPHER_CTX*, unsigned char*, int*, const unsigned char*, int) { return 0; }
inline int EVP_DecryptFinal_ex(EVP_CIPHER_CTX*, unsigned char*, int*) { return 0; }
inline unsigned long ERR_get_error() { return 0; }
inline char* ERR_error_string(unsigned long, char*) { static char msg[] = "no cryptography on the Nintendo 64"; return msg; }
#endif

#endif // N64_BUILD
