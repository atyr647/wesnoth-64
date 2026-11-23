/*
 * fontconfig.h -- the fontconfig calls Wesnoth makes, for the Nintendo 64.
 *
 * Wesnoth registers its font directory with fontconfig and loads fonts.conf
 * (hinting/antialiasing preferences). On the console the font directory is
 * registered with the Pango font map in src/n64/pango (which reads each
 * font's family, weight and style with FreeType); fonts.conf is accepted and
 * its settings are those the port uses anyway (grey antialiasing, full
 * hinting, no subpixel rendering).
 */
#ifndef N64_FONTCONFIG_H
#define N64_FONTCONFIG_H
typedef unsigned char FcChar8;
typedef int FcBool;
#define FcFalse 0
#define FcTrue 1
typedef struct _FcConfig FcConfig;
typedef struct _FcPattern FcPattern;
FcConfig* FcConfigGetCurrent(void);
FcBool FcConfigAppFontAddDir(FcConfig* config, const FcChar8* dir);
FcBool FcConfigAppFontAddFile(FcConfig* config, const FcChar8* file);
void FcConfigAppFontClear(FcConfig* config);
FcBool FcConfigParseAndLoadFromMemory(FcConfig* config, const FcChar8* buffer, FcBool complain);
FcBool FcInit(void);
#endif
