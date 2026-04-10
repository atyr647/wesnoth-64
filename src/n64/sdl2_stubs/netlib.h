/* Stub netlib.h for N64 build - N64-NetLib USB networking stubs */
#ifndef NETLIB_H_STUB
#define NETLIB_H_STUB

#include <stdint.h>
#include <stdbool.h>

/* Callback type for packet handlers */
typedef void (*netlib_callback_t)(uint8_t from);
typedef void (*netlib_disconnect_callback_t)(void);
typedef void (*netlib_reconnect_callback_t)(void);

/* Stub implementations - N64 networking requires USB hardware + PC relay */
static inline bool netlib_initialize(void) { return false; }
static inline uint8_t netlib_getclient(void) { return 0; }
static inline void netlib_register(uint8_t pkttype, netlib_callback_t cb) { (void)pkttype; (void)cb; }
static inline void netlib_callback_disconnect(int timeout_ms, netlib_disconnect_callback_t cb) { (void)timeout_ms; (void)cb; }
static inline void netlib_callback_reconnect(netlib_reconnect_callback_t cb) { (void)cb; }
static inline void netlib_poll(void) {}
static inline void netlib_readbytes(uint8_t* dst, int len) { (void)dst; (void)len; }
static inline void netlib_start(uint8_t pkttype) { (void)pkttype; }
static inline void netlib_writebytes(const uint8_t* src, int len) { (void)src; (void)len; }
static inline void netlib_sendtoserver(void) {}

#endif /* NETLIB_H_STUB */
