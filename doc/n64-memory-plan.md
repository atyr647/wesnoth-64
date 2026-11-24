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

## Plan (each step keeps the engine's behaviour identical)

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
