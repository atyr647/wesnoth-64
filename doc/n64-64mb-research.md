# Fitting the whole game into a 64 MiB cartridge image: research

Status: research only, nothing in the build was changed. Numbers are marked
**[M]** measured on this repository (all files), **[S]** measured on a sample
and extrapolated by byte ratio, or **[E]** estimate / general knowledge that
still has to be verified on the console.

Scripts and raw results are in the session scratch directory
`/tmp/claude-0/-home-user-wesnoth-64/5270d4b3-60d7-548b-a9f0-c2163df4a43f/scratchpad/fit64`
(`inv.py`, `dims.py`, `imgexp.py`, `pack.py`, `port.py`, `textc.py`, `mus.sh`,
`sfx.sh`, `*_res.json`, `pack.log`, `mus_res.txt`, `sfx_res.txt`). The scratch
directory is not durable; the method is described in section 7.

## 0. Summary

* The shipped tree is **570.7 MiB** in 24,539 files [M]: music 218.8, big art
  (portraits, story, maps) 171.5, sprites 102.2, fonts 27.1, sound effects
  27.0, text 22.9 MiB. There are no translations in it (the port is
  English-only: `src/n64/n64_gettext.cpp` is a pass-through; `po/` is 475 MiB
  but is not part of the data tree).
* The usable cartridge space is **63.75 MiB** (`SWAP_CART_END` 0x03FC0000;
  the top 256 KiB is the IS-Viewer window). The ROM code is 13.55 MiB today
  and compresses per 16 KiB page to about 5.8 MiB [M].
* Everything except music fits in about **45-54 MiB** with reductions that
  the 640x240 screen cannot show (big art stored at display resolution,
  sprites palettized, non-unit sprites stored at half height).
* **Music is the deciding item.** 2 h 56 min 19 s (10,579 s) [M]. Cheap
  codecs are out (VADPCM / IMA ADPCM at 22 kHz mono: 112-125 MiB). It only
  fits as a perceptual codec at 12-18 kbit/s mono: 15.6-22.7 MiB [S].
* All content in one 64 MiB image is **just** feasible (about 62-64 MiB) and
  only with the most aggressive setting in every category; the margin is
  1-2 MiB. Nothing has to be cut, but music quality drops clearly.
* A real 64 MiB cartridge cannot run the game in any case: the engine heap
  (26-37 MiB observed) needs writable backing store. On a SummerCart64 the
  SD card is always present (the ROM is loaded from it), so "no SD needed
  for game data" realistically means *one .z64 file, SD used only for the
  swap file and saves*. Every candidate budget below needs heap swap on the
  SD card, at least as an overflow tier. That is the first thing to
  prototype.

## 1. Inventory [M]

`data/`, `images/`, `sounds/`, `fonts/`, hidden files excluded. Files / MiB.

| Category | Files | MiB | Notes |
|---|---|---|---|
| Music (`*/music/*.ogg`) | 52 | 218.80 | Vorbis 44.1 kHz stereo, mean 166-173 kbit/s, 10,579.4 s |
| Portraits | 592 | 67.78 | 589 WebP (lossless, 5.4 bpp) + 3 PNG; 106 Mpx; mean 400x450 |
| Story art | 133 | 59.07 | 113 WebP, 12 PNG, 8 JPG; 111 Mpx; up to 2560x1440 |
| Maps / title backgrounds (`*/maps/*`) | 166 | 44.61 | WebP; 205 Mpx; up to 4096x2160 |
| Terrain | 6,206 | 44.49 | 6,163 PNG (40.2 MiB, 111 Mpx) + 43 large WebP (4.3 MiB) |
| Fonts | 34 | 27.10 | see section 2.5 |
| Sound effects | 343 | 26.96 | 257 ogg + 86 wav; 573.6 s; 116 stereo, some 96 kHz wav |
| Items, halos, icons, attacks, projectiles, scenery | 3,225 | 22.95 | PNG, 76 Mpx |
| Text | 3,448 | 22.90 | cfg 16.83, map 3.03, lua 1.52, rest (py, txt, el...) |
| Other PNG (GUI, misc, themes, cursors, bigmap...) | 1,311 | 20.20 | 26 Mpx |
| Unit sprites | 9,005 | 14.59 | PNG, 55.8 Mpx, mean 6.2 kpx; 97 % have <= 256 colours |
| Other WebP/JPG | 13 | 0.98 | |
| Translations (po/mo) | 0 | 0 | none in the tree |
| **Total** | **24,539** | **570.67** | |

Per area (files / MiB): the largest contributors.

| Area | Total | Music | Portraits | Story | Maps | Terrain | SFX | Items etc. | Text | Other img | Units |
|---|---|---|---|---|---|---|---|---|---|---|---|
| data/core | 15059 / 286.98 | 43 / 160.99 | 286 / 32.20 | 19 / 14.57 | 26 / 9.63 | 5622 / 40.76 | 272 / 7.47 | 995 / 5.26 | 411 / 2.45 | 287 / 5.19 | 7095 / 8.46 |
| Heir_To_The_Throne | 1050 / 55.46 | 3 / 24.53 | 50 / 5.39 | 8 / 2.28 | 10 / 2.87 | 14 / 0.20 | 11 / 5.60 | 384 / 6.26 | 180 / 2.59 | 46 / 4.41 | 344 / 1.33 |
| The_Deceivers_Gambit | 1068 / 43.98 | 3 / 18.96 | 30 / 3.36 | 13 / 1.80 | 9 / 1.63 | - | 21 / 8.72 | 610 / 7.35 | 92 / 1.14 | 2 / 0.01 | 288 / 1.00 |
| The_Rise_Of_Wesnoth | 250 / 29.92 | - | 27 / 3.19 | 32 / 22.62 | 10 / 3.16 | - | 1 / 0.03 | - | 75 / 0.61 | 2 / 0.10 | 103 / 0.21 |
| fonts/ | 36 / 27.12 | | | | | | | | 2 / 0.02 | | |
| Eastern_Invasion | 573 / 16.75 | 2 / 8.39 | 11 / 1.67 | - | 8 / 2.13 | - | 6 / 0.14 | 190 / 1.48 | 95 / 1.38 | 12 / 0.54 | 249 / 1.03 |
| Winds_of_Fate | 85 / 11.91 | 1 / 5.93 | 1 / 0.06 | 2 / 3.32 | 1 / 1.50 | - | 2 / 0.06 | 17 / 0.38 | 45 / 0.44 | 1 / 0.00 | 15 / 0.21 |
| Heir_To_The_Throne_Classic | 247 / 9.82 | - | 27 / 3.13 | 9 / 2.54 | 10 / 2.86 | - | - | - | 86 / 0.89 | 2 / 0.10 | 113 / 0.29 |
| The_South_Guard | 284 / 9.49 | - | 16 / 2.07 | 8 / 1.38 | 3 / 0.49 | - | 2 / 0.69 | 21 / 0.19 | 57 / 0.62 | 75 / 3.61 | 102 / 0.43 |
| Secrets_of_the_Ancients | 276 / 8.46 | - | 11 / 1.14 | 17 / 5.40 | - | 78 / 1.09 | - | 21 / 0.08 | 79 / 0.54 | 2 / 0.08 | 68 / 0.13 |
| Under_the_Burning_Suns | 397 / 8.35 | - | 36 / 5.61 | - | - | 6 / 0.19 | 3 / 0.10 | 47 / 0.49 | 98 / 1.27 | 5 / 0.12 | 202 / 0.57 |
| Legend_of_Wesmere | 88 / 5.79 | | 11 / 1.24 | 11 / 1.85 | 8 / 2.00 | | | | 57 / 0.60 | 1 / 0.11 | |
| The_Hammer_of_Thursagan | 254 / 5.07 | | 13 / 1.63 | | 8 / 1.94 | | 2 / 0.35 | 35 / 0.45 | 48 / 0.34 | 1 / 0.14 | 147 / 0.22 |
| Liberty | 84 / 5.07 | | 7 / 0.75 | 4 / 1.59 | 9 / 2.01 | | | 31 / 0.28 | 22 / 0.25 | 2 / 0.14 | 9 / 0.04 |
| Sceptre_of_Fire | 189 / 4.95 | | 19 / 1.86 | 2 / 0.05 | 9 / 2.01 | 35 / 0.29 | | 3 / 0.02 | 55 / 0.48 | 14 / 0.16 | 52 / 0.09 |
| images/ | 1754 / 4.54 | | | | | 98 / 0.03 | | 831 / 0.55 | 4 / 0.00 | 819 / 3.88 | |
| Descent_Into_Darkness | 122 / 4.40 | | 10 / 1.03 | 4 / 0.48 | 10 / 2.01 | | | 8 / 0.02 | 49 / 0.68 | 10 / 0.11 | 31 / 0.06 |
| Son_Of_The_Black_Eye | 83 / 4.25 | | 13 / 1.28 | | 8 / 2.38 | | | 1 / 0.00 | 44 / 0.44 | 5 / 0.13 | 12 / 0.01 |
| data/internal | 430 / 4.07 | | 1 / 0.08 | | | 342 / 1.88 | 2 / 1.88 | 2 / 0.01 | 12 / 0.05 | | 71 / 0.16 |
| Two_Brothers | 31 / 3.74 | | 2 / 0.29 | 4 / 1.18 | 9 / 2.01 | | | | 15 / 0.12 | 1 / 0.14 | |
| Northern_Rebirth | 66 / 3.51 | | 14 / 0.83 | | 10 / 2.03 | | 1 / 0.01 | | 39 / 0.52 | 2 / 0.11 | |
| Dead_Water | 63 / 3.14 | | 5 / 0.74 | | 9 / 1.94 | | | 5 / 0.01 | 34 / 0.30 | 5 / 0.14 | 5 / 0.01 |
| Of_Pearls_and_Pirates | 113 / 2.56 | | 1 / 0.07 | | 9 / 2.01 | | | 9 / 0.02 | 30 / 0.21 | | 64 / 0.25 |
| Dusk_of_Dawn | 86 / 2.32 | | 1 / 0.14 | | | | | | 28 / 0.20 | 22 / 1.88 | 35 / 0.09 |
| data/test | 890 / 1.79 | | | | | | | | 889 / 1.79 | | |
| World_Conquest | 172 / 1.67 | | | | | 11 / 0.04 | 1 / 0.84 | 15 / 0.10 | 140 / 0.68 | 5 / 0.01 | |
| data/tools | 75 / 1.32 | | | | | | | | 67 / 1.10 (+0.22 other) | | |
| sounds/ | 20 / 1.07 | | | | | | 19 / 1.07 | | | | |
| data/ai, gui, multiplayer, lua, schema, themes, languages, hardwired, top level | 694 / 3.15 | | | | | | | | all text | | |

Free reductions that change nothing for the player:

* **Exact duplicates**: 306 files, 16.66 MiB (WebP 10.56, wav 5.48, PNG 0.48)
  [M]. A pack format stores them once.
* **Not used at run time**: `data/test` (1.79 MiB), `data/tools` (1.32 MiB)
  are developer material; `data/schema` (0.20 MiB) is only read by the WML
  validator. The text figures below exclude `data/test` and `data/tools`.

## 2. Compressed sizes per category

### 2.1 Text (WML, Lua, maps) [M]

Run-time corpus: 2,459 files (`*.cfg *.lua *.map *.mask *.fai`, without
`data/test` and `data/tools`), 19.73 MiB.

| Method | Size MiB | Ratio |
|---|---|---|
| raw | 19.73 | 1.00 |
| per file, LZ4HC | 5.83 | 0.30 |
| per file, deflate -9 | 4.44 | 0.23 |
| per file, zstd -19 | 4.29 | 0.22 |
| per file, zstd -19 + one 110 KiB dictionary | 3.46 | 0.18 |
| 16 KiB blocks: zstd / deflate / LZ4HC | 4.15 / 4.30 / 5.70 | |
| 64 KiB blocks: zstd / deflate / LZ4HC | 3.47 / 3.73 / 4.62 | |
| 256 KiB blocks: zstd / deflate / LZ4HC | 3.05 / 3.55 / 4.12 | |
| solid: LZ4HC / deflate / zstd -19 / xz -9 | 3.96 / 3.49 / 2.42 / 2.38 | |

Decompression cost: on the build host zstd decoded this corpus at 1090 MB/s,
LZ4 at 1850, zlib at 274, xz at 114 MB/s [M]. Scaling to the VR4300 is an
estimate [E]: LZ4 several MB/s (libdragon documents its LZ4 loader as
faster than reading the same data uncompressed), zstd and inflate 1-3 MB/s,
xz/LZMA well under 1 MB/s. The game reads a few MiB of WML at start-up, so
zstd or deflate adds seconds at most, next to a WML preprocessing and
parsing phase that already takes far longer. **Plan figure: 3.5 MiB**
(zstd, 64 KiB blocks or per-file with dictionary; random access kept).

A pre-parsed WML cache (`src/n64/n64_binary_wml.cpp`, old
`tools/n64-asset-pipeline/src/wml_compiler.cpp`) would not be smaller than
compressed text, but could cut load time and heap use; it is a separate topic.

### 2.2 Images

What the screen can show: the engine lays out 640x480 and the RDP renderer
stores every texture at half height (`src/n64/sdl2/SDL_n64render.c`), so
any image drawn 1:1 carries twice the vertical resolution that is ever
displayed, and any image drawn scaled to the screen (story art, maps,
title backgrounds) needs at most 640x240 stored pixels.

#### Big art (portraits, story, maps, large WebP terrain): 176.7 MiB in 947 files

Sample: every 4th file (237 files) [S]; portraits at dialog size: every 3rd
(198 files) [S].

| Stored size / encoding | Portraits | Story | Maps | WebP terrain | Other | Total |
|---|---|---|---|---|---|---|
| original | 67.78 | 59.07 | 44.61 | 4.29 | 0.98 | 176.72 |
| fit 640x480 then half height (<= 640x240), WebP q75 | 9.60 | 2.05 | 3.37 | 0.96 | 0.11 | 16.09 |
| same, WebP q90 | 14.03 | 3.79 | 4.72 | 1.21 | 0.17 | 23.92 |
| same, WebP lossless | 38.60 | 16.58 | 9.16 | 2.41 | 0.46 | 67.19 |
| same, CI8 (256 colours) + zstd | 11.86 | 4.26 | 3.02 | 0.82 | 0.14 | 20.10 |
| same, CI8 + LZ4HC | 14.65 | 5.50 | 3.60 | 0.98 | 0.17 | 24.90 |
| same, RGBA5551 + zstd | 23.26 | 9.83 | 5.37 | 1.18 | 0.27 | 39.92 |
| half width, quarter height, WebP q75 / q90 | 3.78 / 5.28 | 0.78 / 1.40 | 1.31 / 1.79 | 0.33 / 0.41 | 0.04 / 0.06 | 6.24 / 8.94 |
| **portraits at 250 px layout width, half height** (mean 31.5 kpx): WebP q60 / q75 / q90 / lossless | 4.37 / 4.77 / 6.74 / 16.39 | | | | | |
| same, CI8 + zstd / CI8 + LZ4HC / RGBA5551 + zstd | 5.62 / 6.92 / 10.09 | | | | | |

Why 250 px for portraits: `data/gui/themes/default/dialogs/wml_message.cfg`
computes the portrait width as `clamp(best_size, 250, 500)` from the map
area width; at a 640x480 window the map area is far below the saturation
point, so message portraits are drawn 250 layout pixels wide, i.e. about
250 x 140 physical pixels. Storing them at that size loses nothing visible
in dialogs (portraits are also shown smaller in the help and unit preview).
This should be confirmed with a screenshot on the console [E].

**Display-resolution big art: 11.3 MiB at WebP q75, 16.6 MiB at q90,
13.9 MiB as CI8 + zstd** (portraits at 250 px, story/maps <= 640x240, WebP
terrain at half height). Exact duplicates take roughly another 6 % off.

Engine impact: none beyond what exists. `tools/n64-prescale-images.py`
already shrinks WebP images above 0.7 Mpx and records the original size in
an `N64O` RIFF chunk; `src/n64/sdl2/IMG_webp_n64.c` reads it and
`src/sdl/texture.cpp` (line 74 ff.) keeps the original draw size. The same
mechanism can carry every portrait, story image and map. Caveats:

* The hook only helps when the surface goes to a texture unmodified. Image
  path functions that use pixel coordinates (`~CROP`, `~BLIT`, `~SCALE`,
  `~MASK`) work on the smaller surface. Portraits and story art mostly use
  none or only `~FL()`/`~RIGHT()`, but WML does contain 306 `~CROP`, 483
  `~BLIT`, 300 `~SCALE`, 189 `~MASK` uses in total [M]; see the rule
  proposed under sprites.
* WebP decoding is slow on the console (the repo notes about 100 s for the
  4096x2160 title image, i.e. under 0.1 Mpx/s). At 640x240 that is still
  around 2 s per story screen and about 0.4 s per portrait [E]. CI8 + LZ
  decodes at least an order of magnitude faster and costs about 2.5 MiB
  more than WebP q75.

#### Sprites (PNG): 97.9 MiB in 19,704 files

Per-file re-encodings, sample every 8th file (2,463 files) [S]:

| Encoding | Other | Items etc. | Units | Terrain | Total |
|---|---|---|---|---|---|
| original PNG | 20.20 | 22.95 | 14.59 | 40.20 | 97.94 |
| full res, WebP lossless (m6) | 12.64 | 14.64 | 7.61 | 25.81 | 60.70 |
| full res, zopflipng (sample 1/160) | 18.80 | 17.90 | 8.01 | 35.43 | 80.14 |
| full res, pngquant 256 (dithered) + zopflipng (sample 1/40) | 8.65 | 13.89 | 7.93 | 20.24 | 50.71 |
| half height, WebP lossless | 7.34 | 9.96 | 8.26 | 14.68 | 40.24 |
| half height, WebP q90 (lossless alpha) | 3.32 | 7.67 | 8.82 | 7.97 | 27.78 |
| half height, RGBA8888 + zstd / + LZ4HC | 9.91 / 13.07 | 13.84 / 18.35 | 9.74 / 13.37 | 19.48 / 24.55 | 52.97 / 69.35 |
| half height, RGBA5551 + zstd / + LZ4HC | 3.40 / 4.41 | 3.96 / 5.27 | 7.73 / 9.32 | 7.57 / 9.95 | 22.66 / 28.95 |

Unit sprites are tiny (mean 1.6 KiB), so per-file overhead dominates and
the half-height versions are not smaller. Pack-level experiment
(`pack.py`: exact palette where the image has <= 256 colours, otherwise
RGBA8888 or 256-colour quantization without dithering; units all 9,005
files [M], other categories every 4th file [S]):

| Category, representation | zstd per file | zstd per file + 256 KiB dict | zstd solid 256 KiB blocks | LZ4HC per file | LZ4HC solid blocks |
|---|---|---|---|---|---|
| **Units**, full res, lossless (97 % exact CI8, rest RGBA) | 8.29 | 7.64 | **5.23** | 11.76 | 7.79 |
| Units, full res, all CI8 | 7.75 | 7.10 | 4.81 | 11.02 | 7.19 |
| Units, half height, all CI8 | 9.01 | 7.31 | 5.61 | 10.27 | 6.79 |
| **Terrain**, full res, lossless (21 % exact CI8) | 34.64 | 35.36 | 32.00 | 44.04 | 42.40 |
| Terrain, full res, all CI8 (PSNR 41 dB) | 10.91 | 11.13 | 10.46 | 14.08 | 13.34 |
| Terrain, half height, lossless | 19.89 | 20.23 | 18.13 | 24.68 | 22.89 |
| Terrain, half height, all CI8 | 6.98 | 6.93 | **6.43** | 8.57 | 7.82 |
| **Items etc.**, full res, lossless (61 % exact CI8) | 20.62 | 21.06 | 19.99 | 26.40 | 25.46 |
| Items etc., full res, all CI8 (PSNR 37.5 dB) | 10.05 | 10.07 | 9.70 | 12.95 | 12.38 |
| Items etc., half height, all CI8 | 5.58 | 5.56 | **5.20** | 7.12 | 6.58 |
| **Other PNG**, full res, lossless (62 % exact CI8) | 19.49 | 19.89 | 19.67 | 25.30 | 25.21 |
| Other PNG, full res, all CI8 (PSNR 36.6 dB) | 4.02 | 4.07 | 3.94 | 5.26 | 5.15 |
| Other PNG, half height, all CI8 | 2.34 | 2.35 | **2.25** | 3.02 | 2.89 |

Sprite totals:

| Scheme | MiB | Fidelity | Engine work |
|---|---|---|---|
| Everything lossless at full res (palette where exact), zstd solid | 76.9 | bit-exact | image loader only |
| Units lossless; rest 256-colour CI8 at full res | 29.3 | units exact; rest quantized, 37-41 dB | image loader only |
| Units lossless at full res; rest CI8 at half height | **19.1** (zstd solid), 22.5 (zstd per file), 25.1 (LZ4HC solid) | units exact; rest equal to what the renderer shows, then quantized | loader + "expand on modify" rule |

What is safe:

* **Units must stay at full resolution and unquantized.** They are team
  coloured (`~TC`/`~RC`, 546 explicit uses plus the implicit one for every
  unit) by exact palette match on the surface; averaging or quantizing
  shifts the magenta key colours and breaks recolouring. Lossless CI8 is
  bit-exact after expansion and costs only 5.2-7.6 MiB, so there is no
  reason to touch them.
* **Half height for terrain, items, halos, GUI** is display-lossless only
  if the image reaches the renderer unmodified. Proposed rule to make this
  safe without touching each image path function: the loader tags a
  half-height surface (as the WebP hook already does via `userdata`);
  `image::get_texture` for a locator without modifications uploads it as
  stored with the original draw size; every other path (`get_surface`,
  any locator with `~` functions, masks, minimap) first expands the
  surface by row doubling. Row doubling followed by the renderer's
  row-pair average returns the stored rows exactly, so results equal
  today's output up to the order of filtering. This is an engine change
  in `src/picture.cpp`/`src/sdl/texture.cpp` and needs a visual regression
  pass (terrain transitions, halos with `~O`, `~BLIT` composites).
* **Quantizing to 256 colours** is a real (small) loss for 40-80 % of
  non-unit sprites. Full-colour alternatives at half height: RGBA5551 + zstd
  (15.0 MiB for the three non-unit groups, 1-bit alpha: not acceptable for
  halos and soft shadows) or WebP q90 with lossless alpha (19.0 MiB, slow
  to decode). A mixed rule (CI8 where exact or where the error is below a
  threshold, RGBA8888 otherwise) lands between 13.9 and 41.9 MiB; it should
  be tuned by looking at the worst cases (halos, large scenery).
* Standard PNG8 output from pngquant needs no loader change, but its
  dithering costs size (50.7 MiB vs 28.9 MiB) [S, small sample].

### 2.3 Music

52 tracks, **10,579.4 s = 2 h 56 min 19 s**, 218.80 MiB, all Vorbis 44.1 kHz
stereo [M]. Core 43 tracks (161.0 MiB); campaign-specific: Heir to the
Throne 3 (24.5 MiB), The Deceiver's Gambit 3 (19.0 MiB, 1,339 s), Eastern
Invasion 2 (8.4 MiB), Winds of Fate 1 (5.9 MiB).

Sample: 10 tracks spread over the length-sorted list, 2,076 s (20 % of the
total), each encoded completely [S]. Size extrapolated by duration.

| Codec, setting | kbit/s | Whole soundtrack MiB |
|---|---|---|
| original | 166 | 218.8 [M] |
| Ogg Opus mono 12 kbit/s (`opusenc --bitrate 12 --downmix-mono`) | 12.4 | 15.6 |
| Ogg Opus mono 16 | 16.7 | 21.1 |
| Ogg Opus mono 24 | 25.7 | 32.4 |
| Ogg Opus mono 32 / stereo 32 | 34.1 / 34.4 | 43.0 / 43.4 |
| Ogg Opus stereo 24 / 48 | 26.3 / 51.0 | 33.2 / 64.4 |
| Vorbis 44.1 kHz stereo q-1 | 37.9 | 47.8 |
| Vorbis 44.1 kHz mono q-1 | 30.1 | 37.9 |
| Vorbis 32 kHz mono q-1 / q1 | 28.1 / 47.7 | 35.4 / 60.2 |
| Vorbis 22.05 kHz mono q-1 / q1 | 22.1 / 28.8 | 27.9 / 36.4 |
| Vorbis 16 kHz mono q-1 / q1 | 17.6 / 23.7 | 22.2 / 29.8 |
| libdragon wav64 Opus (`audioconv64 --wav-compress 3`), mono, `--wav-resample 12000` | 14.1 | 17.8 |
| same, 16000 / 24000 / 32000 | 18.0 / 26.4 / 34.1 | 22.7 / 33.3 / 43.0 |
| same, stereo 16000 | 34.9 | 44.0 |
| libdragon VADPCM mono 22.05 / 16 / 11.025 kHz | 99.2 / 72.0 / 49.6 | 125.2 / 90.8 / 62.6 |
| IMA ADPCM mono 22.05 kHz | 88.5 | 111.7 |

Notes:

* The ADPCM family is 4-4.5 bits per sample at a fixed rate; even 11 kHz
  mono VADPCM (62.6 MiB) is the whole cartridge. Tracker formats
  (xm64/ym64) would mean re-composing the soundtrack and are not a faithful
  option.
* libdragon's wav64 Opus is CELT-only at 48 kHz with 20 ms frames, and
  `--wav-resample` only selects the bit rate (`3000 + rate x channels`
  bit/s, `tools/audioconv64/conv_wav64.c`). Its IMDCT, FFT and comb filter
  run on the RSP (`src/audio/rsp_opus_*.S`), which is what makes it
  interesting for a 93 MHz CPU. CELT without the SILK layer is weak below
  about 24 kbit/s; at 14-18 kbit/s expect a dull, narrow-band result [E].
  Standard Ogg Opus at 12-16 kbit/s uses SILK/hybrid modes and sounds
  better at the same size, but libdragon cannot decode it and a CPU-only
  libopus is not cheap: on the build host `opusdec` needed about 2.0 s for
  a 325 s track, against 0.77 s for the original Vorbis file and 0.22 s for
  the 22.05 kHz mono Vorbis version [M, host, float builds, noisy].
* Decode cost on the console is not known for any of these [E]. Current
  state: 44.1 kHz stereo Vorbis via stb_vorbis is slower than real time.
  Vorbis at 22.05 kHz mono decodes a quarter of the samples (host: 3.5x
  faster), which would land near or somewhat better than real time and
  still take a large share of the CPU. The RSP Opus path is the only
  candidate that plausibly leaves the CPU mostly free; it has no seeking
  and loops only as a whole, which fits how Wesnoth plays music.
* No listening test was possible in this environment. The quality
  statements above are expectations; a few tracks must be auditioned on
  hardware or in ares before the bit rate is fixed.

### 2.4 Sound effects

343 files, 573.6 s, 26.96 MiB (227 mono, 116 stereo; ten 96 kHz stereo wav
files alone are 9 MiB; 5.48 MiB are exact duplicates) [M]. All files
converted, mono [M]:

| Codec | MiB |
|---|---|
| original | 26.96 |
| PCM 16-bit 22.05 kHz mono | 24.15 |
| libdragon VADPCM 22.05 / 16 / 11.025 kHz | 6.86 / 5.00 / 3.47 |
| IMA ADPCM 22.05 kHz | 6.25 |
| Vorbis 22.05 kHz q0 | 3.20 |
| libdragon wav64 Opus | 3.76 (the bit-rate option had no effect on these short files; not investigated) |
| Ogg Opus 24 kbit/s | 1.93 |

SDL_mixer decodes a sound effect completely when it is loaded, so decode
speed matters less than for music, but the decoded PCM sits in the heap.
VADPCM at 22 kHz is the native, RSP-decoded, near-free choice if the
effects are moved to libdragon's mixer; Vorbis 22 kHz mono keeps the
existing SDL_mixer path unchanged. **Plan figure: 3.2 MiB (Vorbis) to
5.0-6.9 MiB (VADPCM).** `audioconv64` crashes when the output directory
does not exist, which is easy to mistake for a codec failure.

### 2.5 Fonts [M]

| Group | Bytes | Needed for English? |
|---|---|---|
| Lato, 18 faces | 10,672,436 | yes, default family (`family_order` in `data/hardwired/fonts.cfg`); WML uses regular, bold, italic and `weight='heavy'` |
| DejaVu Sans, 3 faces | 2,098,176 | yes: fallback for symbols and the alias for `sans`/`serif` in `src/n64/pango/n64_pango_fonts.cpp` |
| DejaVu Sans Mono, 2 faces | 672,704 | yes: `monospace` |
| WesScript, 4 faces | 164,108 | yes: `script` family, story text |
| Oldania ADF, 4 faces | 174,808 | yes (2 uses in WML) |
| Akademie Alte, Andagii | 68,760 | no references found in `data/` or `src/`; tiny, keep |
| NotoSansJP variable + DroidSansFallbackFull | 14,427,532 | no: CJK only |
| Lohit-Bengali | 139,460 | no |

The port has no translations, so the CJK and Bengali fonts (13.9 MiB) only
ever render the native language names in `data/languages` (a list that is
useless without translations). Dropping them is a content cut only if
translations are added later.

Subsetting with `pyftsubset` (layout features kept):

| Face | Original | Latin + Latin Ext + Greek + Cyrillic + punctuation/arrows/symbols | Only the 322 characters that occur in the run-time text | Wide subset + zstd |
|---|---|---|---|---|
| Lato Regular / Bold / Italic / BoldItalic / Heavy / HeavyItalic | 657-724 K each, 4.10 M | 2.46 M | 0.61 M | 1.02 M |
| DejaVu Sans / Bold / Oblique | 2.10 M | 0.75 M | 0.14 M | 0.42 M |
| DejaVu Sans Mono / Bold | 0.67 M | 0.35 M | 0.07 M | 0.21 M |

English-only font budget: all needed faces unsubsetted 6.9 MiB (drop 12
unused Lato weights and the CJK/Bengali fonts); wide subset 3.8 MiB
uncompressed; used-characters subset about **1.2 MiB uncompressed**
including the script fonts. Fonts are streamed by FreeType, so they should
stay uncompressed (or block-compressed) in the image. The used-characters
subset must be regenerated from the text corpus at pack time and should
keep full ASCII for player-typed names.

### 2.6 Code [M]

ROM today: 14,204,928 bytes = 13.55 MiB: boot ELF and headers 0.72 MiB,
paged blob `wesnoth_n64.pgc` 12.83 MiB (821 pages of 16 KiB; 20 % zero
bytes).

| Paged blob | MiB |
|---|---|
| raw | 12.83 |
| per 16 KiB page: LZ4HC / deflate -9 / zstd -19 / zstd -19 + 64 KiB dictionary | 6.58 / 5.18 / 4.82 / 4.48 |
| per 64 KiB group: LZ4HC / deflate / zstd / zstd + dict | 5.82 / 4.84 / 4.39 / 4.20 |
| solid (not pageable): LZ4HC / zstd / xz | 5.30 / 3.63 / 3.07 |

Per-page compression saves 6.2-8.3 MiB. Cost: each code page fault
decompresses 16 KiB instead of copying it: about 2-4 ms with LZ4 and
8-15 ms with zstd/inflate on top of the cartridge read [E]; the pager's
fault counters (`n64_pager_get_stats`) give the real rate to multiply by.
The decompressor must live in the resident (non-paged) part. **Plan
figure: 0.72 + 5.1 = 5.8 MiB (zstd or deflate), 7.3 MiB with LZ4.**

## 3. Budgets

Fixed points: usable cartridge space 63.75 MiB; file index and names for
about 23,000 entries 0.5 MiB (path list is 1.35 MiB raw, 0.10 MiB zstd
[M], plus 16 bytes per entry [E]); heap swap need 26-37 MiB at the title
screen, more is likely inside large scenarios (not measured here).

A real 64 MiB cartridge has no writable memory beyond a save chip, so the
paged heap has nowhere to go: the game is a flashcart title under every
budget. On the SummerCart64 the ROM image and the heap swap share the same
64 MiB of SDRAM, and the SD card is always there.

Building blocks (MiB):

| Item | Aggressive | Moderate |
|---|---|---|
| Boot + compressed paged code | 5.8 (zstd pages) | 7.3 (LZ4 pages) |
| Text | 3.5 | 3.5 |
| Fonts (English) | 1.2 (used glyphs) | 3.8 (wide subset) |
| Unit sprites, lossless | 5.2 (solid blocks) | 7.6 (per file + dict) |
| Other sprites | 13.9 (half height, CI8) | 24.1 (full res, CI8) |
| Big art at display resolution | 11.3 (WebP q75) | 16.6 (WebP q90) |
| Sound effects | 3.2 (Vorbis 22 kHz) | 6.9 (VADPCM 22 kHz) |
| Index | 0.5 | 0.5 |
| **Everything except music** | **44.6** | **70.3** |
| Music | 15.6 (Ogg Opus 12k) / 17.8 (RSP Opus 14k) | 22.7 (RSP Opus 18k) / 27.9 (Vorbis 22 kHz mono) |

The moderate column does not fit even without music; the half-height
sprite store (10.2 MiB) and the code compression are not optional.

### Budget A: everything in the image, heap swap on the SD card

| | MiB |
|---|---|
| Everything except music, aggressive | 44.6 |
| Music, RSP Opus about 14 kbit/s mono | 17.8 |
| **Image** | **62.4** |
| Left in SDRAM | 1.3 |

* Fits with all content. With Ogg Opus 12 kbit/s it is 60.2 MiB, with RSP
  Opus 18 kbit/s 67.3 MiB (does not fit).
* Any one category moving to its moderate setting breaks it, except fonts
  (wide subset: +2.6) only together with 12 kbit/s music.
* The whole heap swap (26-37+ MiB) moves to a preallocated swap file on the
  SD card. Page-in then costs an SD read through the SC64 instead of a
  cartridge DMA; page-out an SD write. Speed, and the wear from constant
  small writes, are unknown [E].
* Saves and preferences stay on the SD card (a compressed Wesnoth save is
  tens to hundreds of KiB [E]; the cartridge save types top out at
  128 KiB, so on-cart saves are not realistic).

### Budget B: game in the image, music on the SD card, swap split

| | MiB |
|---|---|
| Everything except music, aggressive | 44.6 |
| optional upgrades: art at WebP q90 (+5.3), SFX as VADPCM (+3.7) | up to 53.6 |
| **Image** | **44.6-53.6** |
| SDRAM left for swap | 19.1-10.1 |

* The game is complete and playable from the one file; music is read from
  the SD card when present (it can then be 22 kHz mono Vorbis or better,
  27.9 MiB+, with no size pressure). Without the music files the game is
  silent apart from effects: that is a content cut for a card-less setup,
  which is why this is not the "whole game in 64 MiB".
* 10-19 MiB of SDRAM swap is less than the observed 26-37 MiB, so an SD
  overflow tier (or compressed swap pages, typically 2-3x on C++ heaps [E])
  is still needed; the hot pages stay in SDRAM.

### Budget C: everything in the image with better music, paid for by cuts

Starting from budget A, raising the music to RSP Opus 18 kbit/s (+4.9) or
22 kHz mono Vorbis (+10.1) needs the same amount removed elsewhere. What
is available, **all of these cut or degrade content**:

| Cut | Saves (in the packed image) |
|---|---|
| Heir to the Throne Classic (legacy duplicate campaign) | about 1 MiB (9.8 MiB original, mostly art shared in kind with HttT) |
| Campaign-specific music (9 tracks, 2,434 s incl. Deceiver's Gambit 1,339 s) | 3.6-4.1 MiB at 12-14 kbit/s |
| Alternate versions of tracks (`frantic-old`, `journeys_end_faststart`, `heavens-remix`, `juggernaut-louder`; 780 s; the last three are also in the row above) | 1.2-1.3 MiB |
| Portraits at half the dialog size | about 2.5 MiB |
| Multiplayer-only and editor-only art and text (not itemised) | 1-2 MiB [E] |
| Big art at WebP q60 | about 1.5 MiB [E] |

Vorbis at 22 kHz does not fit under any reasonable set of cuts (it would
need about 10 MiB, i.e. dropping several campaigns' art). RSP Opus at
18 kbit/s fits with the first two rows. This budget also keeps zero
SDRAM for swap.

For reference, outside the 64 MiB premise: the SummerCart64 can also map
14 MiB of its flash as "extended ROM" above the SDRAM (78 MiB total) [E,
from the SC64 documentation, not checked on this setup]. That would hold
the music of budget A while leaving the SDRAM split of budget B, but it is
flashcart-specific and the image is no longer a 64 MiB ROM.

## 4. Recommendation

1. **Target budget A, with B as the fallback, and decide between them with
   two prototypes.** Both depend on the SD swap tier, and A additionally on
   the music codec. If music at about 14 kbit/s is judged unacceptable, or
   SD-only swap is too slow, ship B (one-file game, music beside it on the
   card): nothing built for A is wasted, because B is A minus the music
   blob.
2. Build the pack in this order; each step is useful on its own and for
   the current SD layout too:
   1. Per-page compressed `.pgc` (saves 6-8 MiB of SDRAM for swap today).
   2. Read-only pack file system in the ROM (deduplicated, block
      compressed text, uncompressed subset fonts) behind
      `src/n64/n64_filesystem.cpp`; the old `tools/n64-asset-pipeline`
      (ROMFS + LZ4, RGBA5551) is the wrong shape: RGBA5551 loses soft
      alpha, and LZ4 alone costs 6-9 MiB over zstd across the image.
   3. Big art at display resolution through the existing `N64O` hook
      (171 MiB down to 11-17 MiB; also removes the multi-second WebP
      decodes).
   4. Lossless CI8 unit sprites; then half-height CI8 for the rest with the
      "expand on modify" rule.
   5. Music and effects.

### Prototype first

1. **Heap swap on the SD card**: preallocate a contiguous swap file, page
   through raw sector I/O, run the title screen and one large scenario,
   compare `swapins`/`writebacks` time against SDRAM swap. Also try LZ4
   compressed swap pages in SDRAM. Every budget needs this answer.
2. **RSP Opus music**: convert three contrasting tracks with
   `audioconv64 --wav-compress 3 --wav-mono --wav-resample 12000/16000/24000`,
   play them through libdragon's mixer next to the RDP renderer (both use
   the RSP queue), measure CPU share and listen. This decides the music
   size (17.8 / 22.7 / 33.3 MiB) and may fix today's broken-up music
   regardless of the 64 MiB goal. Compare with 22 kHz mono Vorbis through
   the existing SDL_mixer path as the no-new-code baseline.
3. **Half-height sprite store**: implement the tag + expand-on-modify rule
   for terrain only and diff screenshots against the current build.

### Risks and unknowns

* Margin: budget A has 1-2 MiB of slack on sampled extrapolations (sprites
  1/4-1/8 samples, art 1/3-1/4, music 20 % by duration). A full-corpus pack
  run is needed before committing; expect a few percent either way.
* Music quality at 12-18 kbit/s is the main fidelity loss and was not
  auditioned.
* SD swap speed and card wear; heap growth in late campaign scenarios.
* 256-colour quantization of non-unit sprites (37-41 dB mean PSNR with a
  plain octree quantizer, no dithering) needs a visual check on halos,
  water and large scenery; a better quantizer or an exception list raises
  the size by an unknown amount.
* The portrait size of 250 px rests on reading the dialog WML, not on a
  console screenshot.
* Decompression time per page fault and per file is estimated, not
  measured, on the VR4300.
* Code size keeps moving (12.83 MiB today for the paged blob); each MiB of
  code costs about 0.4 MiB compressed.

## 5. What cuts content (for clarity)

Not cuts: deduplication, display-resolution art, lossless unit sprites,
half-height non-unit sprites (given the expand rule), dropping
`data/test`, `data/tools`, CJK/Bengali fonts in an English-only build,
unused Lato weights, glyph subsetting from the shipped text.

Fidelity losses without removing content: 256-colour quantization of
non-unit sprites, lossy WebP for big art, mono low-bit-rate music, mono
22 kHz sound effects.

Content cuts (only in budget C, or B without an SD music folder): any
campaign, any music track, multiplayer/editor assets.

## 6. Tools used

Host: zstd 1.5, lz4, xz, pngquant, zopflipng, cwebp/Pillow 13 (libwebp),
opus-tools, vorbis-tools, ffmpeg/ffprobe, fonttools (`pyftsubset`),
`/opt/n64_toolchain/bin/audioconv64` (libdragon at `/tmp/pak-libdragon`,
commit c4a7e119), Python 3.11 venv with pillow, numpy, zstandard, lz4.

## 7. Method

* Inventory: walk `data images sounds fonts`, skip dot files, classify by
  extension and path component (`/music/`, `/portraits/`, `/story/`,
  `/maps/`, `/units/`, `/terrain/`, then
  attacks|items|icons|halo|projectiles|scenery). Duplicates by MD5.
* Text: `find ... -name '*.cfg' -o -name '*.lua' -o -name '*.map' -o -name
  '*.mask' -o -name '*.fai'`, minus `data/test`, `data/tools`;
  `zlib.compress(x, 9)`, `lz4.block.compress(mode='high_compression',
  compression=12)`, `ZstdCompressor(level=19)` (+ `train_dictionary`),
  `lzma.compress(preset=9)` per file, per fixed block and solid.
* Images: Pillow. Half height = premultiplied average of row pairs (what
  the renderer does). WebP: `lossless=True, quality=100, method=6` or
  `quality=75/90, method=6`. CI8: `Image.quantize(256, FASTOCTREE, no
  dither)` or the exact palette when the image has <= 256 colours.
  RGBA5551 big-endian. Art resize: Lanczos on premultiplied RGBA to
  (a) fit 640x480 then half height, (b) width <= 640 then half height,
  (c) half of (b) in both axes, (d) portraits to 250 px width then half
  height. Extrapolation: compressed/original byte ratio of the sample
  times the category's total original bytes.
* `pngquant --speed 1 --strip 256`, `zopflipng -y`.
* Audio: `ffprobe -show_entries format=duration` for every file. Music
  sample decoded with ffmpeg to 44.1 kHz wav, then `opusenc --bitrate N
  [--downmix-mono]`, `oggenc -q Q [--downmix] [--resample R]`,
  `audioconv64 --wav-compress 1|3 [--wav-mono] --wav-resample R`,
  `ffmpeg -c:a adpcm_ima_wav -ar 22050`. Sound effects: all files, mono.
* Fonts: `pyftsubset FONT --unicodes=U+0020-024F,U+0370-03FF,U+0400-04FF,
  U+1E00-1EFF,U+2000-206F,U+20A0-20BF,U+2190-21FF,U+2200-22FF,U+25A0-27BF
  --layout-features='*'` and `--text-file=<all characters in the text
  corpus>`.
* Code: a copy of `build-n64/wesnoth_n64.pgc` split into 16 KiB and 64 KiB
  pieces, compressed as for text.
