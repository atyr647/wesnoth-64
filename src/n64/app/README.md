# Wesnoth N64 front-end (`wesnoth_n64_app`)

The runnable ROM. It boots on the console (verified on ares), shows a title
screen, lists every campaign and scenario packed in the ROM, and opens the real
Wesnoth map for the chosen scenario in a scrollable hex viewer, drawn with the
game's own terrain art.

**What it is not:** the Wesnoth engine. There is no rules engine, AI, units,
combat or campaign scripting here. The full-engine port (`wesnoth_n64`) does
not fit the console: its ELF is ~9.2 MB against 8 MiB of RAM before any heap,
and Wesnoth's game config alone needs far more RAM than that to parse. See
`CMakeLists.txt` for the opt-in target.

## Build

    tools/n64-build-rom.sh            # -> build-n64/wesnoth_n64_app.z64

## Controls

| Where        | Control            | Action                    |
|--------------|--------------------|---------------------------|
| Title        | A / Start          | continue                  |
| Menus        | D-pad / stick      | move                      |
|              | A / B              | select / back             |
| Map viewer   | D-pad / stick      | move the cursor           |
|              | C buttons          | nudge the camera          |
|              | A                  | show/hide the help panel  |
|              | B / Start          | back                      |

## Data flow

`tools/n64-asset-pipeline` packs `data/`, `images/` and `sounds/` into one
big-endian blob (`wesnoth.wnrf`) that `n64tool` appends to the ROM. At runtime
`n64_romfs.cpp` finds it through the ROM table of contents and looks files up by
name hash. The front-end reads `app/terrain_codes.txt` (generated from
`terrain.cfg`) to map terrain codes to textures, and `app/manifest.txt` for the
campaign/scenario lists.

## Testing without a console

    tools/n64-ares-drive.sh build-n64/wesnoth_n64_app.z64 script.txt out/

drives the ROM on headless ares with scripted button presses and screenshots
(see the header of that script for the command set).
