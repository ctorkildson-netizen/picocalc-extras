# Bluetooth keyboards for the PicoCalc

A Bluetooth keyboard for programs on the ClockworkPi **PicoCalc** with a
**Pico 2 W** (the one with the radio). Pair the keyboard once with
**btpair**; from then on, any program built with this code connects to it
by itself, and its keys work alongside the PicoCalc's own.

## Pairing a keyboard (btpair)

1. Copy `btpair.uf2` (from the latest
   [picocalc-extras release](https://github.com/ctorkildson-netizen/picocalc-extras/releases))
   into `pico2-apps` on the SD card and run btpair from the shell.
2. Press **P**, and put the keyboard in pairing mode: on most keyboards you
   hold its Bluetooth or connect key until its light blinks quickly.
3. Press **Enter** on the PicoCalc. After about ten seconds the keyboards
   it found are listed: press the number of yours. A keyboard may show as
   "(asking its name...)" for a few seconds before its name appears; if it
   never answers, it's listed by its Bluetooth address, a string like
   `A1:B2:C3:D4:E5:F6`. Either way you can choose it.
4. If a number appears, type it on the Bluetooth keyboard and press that
   keyboard's Enter key.

On a Logitech K380, pairing mode is holding one of its 1, 2, 3 keys (the
channel you want) until its light blinks quickly. If some keys come out
wrong, set it to its Windows layout: hold **Fn** and **P** for three
seconds.

btpair says when it's paired and connected, and you can try the keyboard
out on its screen. **F** forgets the keyboard; **D** shows the details of
what Bluetooth is doing, and **F5** saves the screen as a text file in
`/screenshots`, and beside it a log of every Bluetooth message since btpair
started (`btpair-001.pklg` and so on). If something goes wrong, those two
files are what to send with a report: the log opens in
[Wireshark](https://www.wireshark.org/), which shows each message in full.

The pairing is kept on the SD card, in `/bluetooth/pairing.dat`, so every
program finds the same keyboard. A program connects when you press a key on
the keyboard, or within a few seconds of starting if the keyboard is
already awake.

Because the pairing is on the card, a different SD card doesn't have it:
pair again with btpair, or copy `/bluetooth/pairing.dat` across to the new
card. (The copy only works in the same PicoCalc: the pairing belongs to
that PicoCalc's Pico 2 W as well as to the keyboard.)

**Keys:** everything the PicoCalc's own keyboard has comes through: letters,
numbers, symbols, Ctrl, Shift, Alt, the arrows, Home, End, PgUp, PgDn, Ins,
Del, Esc and F1 to F10. The Windows (or Start) key and F11 and F12 do
nothing. Many keyboards' top row sends media and shortcut keys (volume,
search, switch apps) unless you hold **Fn**; those are ignored too, except
that a shortcut made of ordinary keys comes through as them (the Logitech
K380's app-switch key arrives as a Tab, for instance). Hold Fn for the real
F keys.

**What works:** "classic" Bluetooth keyboards, the kind that have been
around for years, and Bluetooth Low Energy (BLE) keyboards; btpair marks
those "(BLE)" in its list. Tried so far: the Logitech K380 (classic), the
Logitech K480 (classic, tried by a forum member), and the ProtoArc XK01 TP
folding keyboard (BLE). One keyboard at a time.

**Mice and trackpads:** a Bluetooth mouse pairs in btpair the same way (a
Logitech M720 has been tried, over BLE), and so does a keyboard's trackpad.
Programs get the movement and buttons from `btkbd_pointer()`: the movement
added up since starting, so take the last reading from the new one. A mouse
takes the one pairing, so pair the keyboard again afterwards.

**If a BLE keyboard stops connecting:** most of them choose a new identity
each time they're put into pairing mode, so the PicoCalc no longer knows
them. Pair it again in btpair.

## Using it in a program

For a Pico SDK program built with Blair Leduc's
[picocalc-text-starter](https://github.com/BlairLeduc/picocalc-text-starter)
drivers:

```cmake
set(PICO_BOARD pico2_w)
include(path/to/bluetooth/bluetooth.cmake)
target_link_libraries(myprogram picocalc_btkbd ...)
```

```c
#include "btkbd.h"
...
fat32_init();
btkbd_init();        // starts Bluetooth, which then runs in the background
```

That's all most programs need. The Bluetooth keyboard's keys come out of
`sb_read_keyboard()` after the PicoCalc's own (the library links with
`--wrap=sb_read_keyboard`), in the same form: `(state << 8) | code`, with
Ctrl, Shift and Alt as presses and releases of their own, letters in lower
case and other characters already shifted, just as the PicoCalc sends them.
So the text starter's keyboard driver, and any code that reads the
keyboard, gets both keyboards with no other changes.

To tell the two keyboards apart instead, call `btkbd_merge(false)` and read
the Bluetooth keyboard with `btkbd_read()`. See [btkbd.h](btkbd.h) for the
rest: the connection's state, and the search and pairing calls btpair uses.

[`btpair/`](btpair/) is a complete program using it, and builds the same way
as the [example](../examples/showpic/) in this repository; its submodules are
Blair's shell (v1.2) and the text starter with the FAT32 fixes offered to
him (the `diamond-dev` branch of
[a fork](https://github.com/ctorkildson-netizen/picocalc-text-starter)).

Things to know:

- **Size:** Bluetooth adds a few hundred KB to a program, most of it the
  radio's firmware. If a program's quick load time matters, make the
  Bluetooth version a separate build, as Diamond does (`diamond-bt.uf2`).
- **Memory:** BTstack takes a few KB as it connects. A program that grabs
  most of the memory at the start should leave some free (Diamond leaves
  32 KB).
- **The pairing on the card:** the SDK's own Bluetooth setup keeps pairings
  in the last sectors of flash, which UF2 Loader and other programs may
  use. This code supplies its own `btstack_cyw43_init()` that keeps them
  on the card instead, so it doesn't link `pico_btstack_cyw43`.
- It brings its own `btstack_config.h` (classic Bluetooth and BLE, with
  memory from `malloc`).
- **Two fixes to BTstack.** `bluetooth.cmake` builds BTstack's `hci.c` and
  `l2cap.c` with one small change each, made at configure time from the
  SDK's own copies, so they follow the SDK; the build stops with a message
  if the SDK's code there changes. Without them a K380's first connection
  after pairing stalled until the keyboard gave up, and forgetting a
  keyboard while it was being called left BTstack refusing to connect to it
  until a restart. The comments in `bluetooth.cmake` say what each changes.
  BlueKitchen fixed both in BTstack itself (the `hci.c` one by a change in
  1.8.3, the `l2cap.c` one on their development branch), so with a Pico SDK
  that brings a newer BTstack, turn them off: `-DBTKBD_FIX_HCI=OFF`,
  `-DBTKBD_FIX_L2CAP=OFF`.
- **A log, if you want one:** built with `BTKBD_LOG` defined (btpair is),
  the library keeps every Bluetooth message in 256 KB of RAM in Apple's
  PacketLogger format, along with BTstack's own notes, and `btkbd_log()`
  hands it to the program to save.

## Whose code

The code here is MIT-licensed. It's built on BTstack's `hid_host_demo.c`, and the BLE part uses
BTstack's HID-over-GATT host as its `hog_host_demo.c` does.
BTstack itself (BlueKitchen GmbH) comes with the Pico SDK and is licensed by
Raspberry Pi for use on the Pico W and Pico 2 W; see `LICENSE.RP` in the
SDK's `src/rp2_common/pico_btstack`.
