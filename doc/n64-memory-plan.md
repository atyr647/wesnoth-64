# Everything in ROM: memory redesign plan

Decision: the whole game ships in the cartridge image, no SD card needed
for data or swap, 100% of the content. This document records the
measurements that drive the plan and the plan itself.

## Measured on the console (ares, SC64 emulation)

Heir to the Throne, scenario 1, live heap after loading (KiB of
allocations; the compressed swap stores it at about 42%):

| Part | Approx. MiB |
|---|---|
| Campaign game config (WML tree: 110k nodes, 227k attributes) | 28 |
| Terrain graphics rules (`terrain_builder::building_rules_`) | 20 |
| Unit types and unit animations | 6 |
| GUI definitions, fonts, textures, Lua, the rest | 12 |
| Growth in the first turns (images, game state) | 10+ |
| **Total** | **~84, peak ~90** |

(An earlier host estimate of ~21 MiB was wrong: it used the define
`CAMPAIGN_HEIR_TO_THE_THRONE`; the campaign's define is
`CAMPAIGN_HEIR_2_THE_THRONE`.)

## Space

The SummerCart64 has 64 MiB of SDRAM (the ROM image and the heap swap share
it) plus 14 MiB of flash that can be mapped read-only as "extended ROM" at
PI 0x14000000 (config ROM_EXTENDED_ENABLE). The full image is about
62.4 MiB with aggressive encoding (doc/n64-64mb-research.md, budget A).

* Without extended ROM: 1.3 MiB of SDRAM left for swap. Does not work.
* With 14 MiB of the image in flash: about 15.6 MiB of SDRAM for swap,
  which holds about 30-35 MiB of heap compressed. **Target: in-game heap
  at most ~30 MiB.**

The libdragon USB log reserves the top 8 MiB of SDRAM; release images are
built with -DN64_USB_LOG=OFF.

## Change of approach: bake on the host, read in place from ROM

Measured before the change (profiled Heir to the Throne run): 14.7 MiB of
code and an 84 MiB heap paged through 3.3 MiB of frames; 88% of the time to
the first map frame in page faults; frames 13-200 s apart. Tuning the pager
(more frames while loading, faster swap compression) gained 7%. Shrinking
the heap does not change the shape: the console repeats, on every boot, work
whose result never changes.

The port now moves that work to ROM build time. The rules code (combat,
movement, AI, WML events, Lua) stays the engine's own source.

1. **WML (done).** `tools/n64-bake-config.sh` compiles every config the game
   can load (cores list and WML tree for the title screen, multiplayer, test,
   editor, each campaign at each difficulty: 138 roots, 3.67 million nodes)
   with the stock preprocessor and parser into one image with identical
   subtrees and strings stored once: 149 thousand nodes, 13.6 MiB (3.7 MiB
   gzipped), 45 s. It is linked into the paged read-only data
   (`-DN64_FROZEN_IMAGE`); `config` copies a node into RAM when it is first
   used (`config::thaw_impl_`). `--n64-check-frozen` recomputes on the
   console the hash the tool stored from the stock loader's result.
   Measured: title config 38 s -> 10 s, campaign config 155 s -> 22 s, unit
   types 142 s -> 79 s, heap at game start 73 MiB -> 43 MiB.
2. **Terrain rules (next).** Parsing `[terrain_graphics]` into
   `building_rules_` and building the map is now the largest stage (724 s;
   string compares, rule sorting, image existence checks on the SD card).
   Bake the parsed, rotated, sorted rule set, with image existence resolved.
3. **Unit types, GUI definitions, fonts.** Same treatment where the profile
   says so.
4. **Presentation.** A loading-screen frame references about 3.5 MiB of code
   and 2-4 MiB of heap (`[workset]` lines with `--n64-watchdog`), more than
   the 3.3 MiB of frames. The desktop GUI toolkit and text stack are to be
   replaced by console-built screens that keep the stock look, with
   pre-rendered fonts.
5. **Image layout.** One .z64 of at most 64 MiB, nothing on the SD card
   (decision). The baked data must be stored compressed in ROM.

## Superseded plan (heap compaction)

1. **Terrain graphics rules (20 MiB).** Compact the rule representation:
   interned image names and flags, shared locators, no per-rotation copies
   of identical data.
2. **WML config (28 MiB + menu 10 MiB).** Compact storage for attribute
   keys/values (interned keys, small-string values) behind the unchanged
   `config` API; investigate keeping scenarios not being played in
   serialized form until first accessed.
3. **Unit types and animations (6 MiB).** Share identical animation data.
4. **Build-time work.** Preprocessed, pre-parsed config caches in the
   image (no preprocessing on the console: loading currently takes about
   30 minutes of console time to reach the map).
5. **Image layout.** Read-only data in the flash window; installer for the
   flash part; verify PI DMA from extended ROM.
