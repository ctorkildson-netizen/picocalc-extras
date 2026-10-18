# NES emulator for the PicoCalc (ShapoNES)

`shapones_picocalc.uf2` is [ShapoNES](https://github.com/shapoco/shapones)
by Shapoco, built for the ClockworkPi PicoCalc with a Pico 2 or Pico 2 W.
The PicoCalc port is part of ShapoNES itself (`samples/picocalc`);
this is a ready-made build of it, shared with Shapoco's permission.

## Install

Copy `shapones_picocalc.uf2` to the SD card's `pico2-apps` folder and pick
it from the UF2 Loader boot menu. Put your `.nes` files in a folder called
`nes` (or in the root of the card). Saved states go in `nes-saves`.

## Keys

| Key | Button |
|---|---|
| Arrows | D-pad |
| `]` / X / K | A |
| `[` / Z / J | B |
| Enter / `=` | Start (Enter picks in the menu) |
| `-` / Space | Select |
| Esc | menu: ROM list and saved states (Left/Right switch pages) |
| Ctrl+Esc | back to the boot menu |

## What works

- Super Mario Bros., Mega Man, The Legend of Zelda and Wizardry play, with
  sound and saved states.
- The common cartridge types (mappers 0-4: NROM, MMC1, UxROM, CNROM, MMC3),
  ROMs up to 384 KB.

## Known problems

ShapoNES describes itself as a work in progress, and it shows in places:

- Tetris doesn't draw its playfield.
- Some flashing graphics look faint (Zelda's name-entry cursor).
- Games on other cartridge types won't run.

Build: from ShapoNES main, October 2026.
