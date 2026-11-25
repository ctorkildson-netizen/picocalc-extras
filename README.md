# picocalc-extras

Small pieces for programs on the ClockworkPi **PicoCalc** with an RP2350
(Pico 2 / Pico 2 W) and UF2 Loader: Shift+F5 screenshots for PDCurses
programs, a file picker, JPEG/PNG/BMP decoders, a Bluetooth keyboard, mouse and gamepad, Wi-Fi
with TCP and ping, and a header for the shell's clock.

They came out of porting Empire, Colossal Cave Adventure and VirtualT
(a TRS-80 Model 100 emulator) to the PicoCalc, and of writing its
[picture viewer](https://github.com/ctorkildson-netizen/picocalc-viewer) and
[Diamond](https://github.com/ctorkildson-netizen/diamond), a WordStar-style
word processor.

| Piece | What it is |
|---|---|
| [`picocalc_clock.h`](picocalc_clock.h) | The shell's clock (shell v1.1), for programs written for the earlier shared clock here |
| [`screenshot/`](screenshot/) | Shift+F5 screenshots (BMP): of any program on the text starter's drivers (`lcdshot.c`), or of a PDCurses program (`pdcshot.c`) |
| [`nes/`](nes/) | A ready-made build of the ShapoNES NES emulator for the PicoCalc |
| [`picker/`](picker/) | A file picker: lists a folder, moves around the card, returns the file chosen, with an optional banner and preview panel |
| [`bluetooth/`](bluetooth/) | A Bluetooth keyboard, mouse and gamepad for programs on a Pico 2 W, and **btpair**, which pairs them: the keyboard's keys come through the normal keyboard reads, so most programs need only one line |
| [`images/`](images/) | JPEG, PNG and BMP decoders that fit a picture to a frame, plus a PC harness for testing them |
| [`net/`](net/) | Wi-Fi from `/ntp/ntp.cfg`, a TCP connection, name lookups and ping, for programs on a Pico 2 W, with a ping program as its example |
| [`fatfs/`](fatfs/) | The SD card's file system (ChaN's FatFs from the Pico SDK) built the way Blair Leduc's picocalc-text-starter wants it, as one CMake library |
| [`HOWTO.md`](HOWTO.md) | How to use the picker and the decoders in your own program, with an example |

## Using a piece in your program

Add the repository as a git submodule, pinned to a release, then bring in
the piece you want with a line or two of CMake. For a Bluetooth keyboard,
in a program built on Blair Leduc's
[picocalc-text-starter](https://github.com/BlairLeduc/picocalc-text-starter)
drivers:

```sh
git submodule add https://github.com/ctorkildson-netizen/picocalc-extras modules/picocalc-extras
git -C modules/picocalc-extras checkout v2.3     # the latest release
```

```cmake
set(PICO_BOARD pico2_w)                            # before pico_sdk_init()
include(modules/picocalc-extras/bluetooth/bluetooth.cmake)
target_link_libraries(myprogram picocalc_btkbd ...)
```

```c
#include "btkbd.h"
fs_init();       /* the SD card first: the pairings are kept on it */
btkbd_init();    /* then the Bluetooth keyboard's keys come through sb_read_keyboard() */
```

Each piece's README has a "Using it in a program" section like this, and
says what it needs. In general the pieces assume:

- an RP2350 PicoCalc (Pico 2 or Pico 2 W; Bluetooth needs the W), with
  programs started from UF2 Loader or Blair Leduc's
  [shell](https://github.com/BlairLeduc/picocalc-shell);
- the Pico SDK 2.2 or later; Bluetooth needs the SDK's `develop` branch,
  whose BTstack has the host for BLE keyboards;
- for `bluetooth/` and `fatfs/`, picocalc-text-starter's drivers (v0.17 or
  later) in the program.

Programs that use these: [calcterm](https://github.com/ctorkildson-netizen/calcterm)
(a VT100 terminal), [Diamond](https://github.com/ctorkildson-netizen/diamond),
the [picture viewer](https://github.com/ctorkildson-netizen/picocalc-viewer),
and the PicoCalc port of [ShapoNES](https://github.com/ctorkildson-netizen/shapones/tree/picocalc-port/samples/picocalc).
They're worked examples to copy from.

Want to add a piece or fix one? See [CONTRIBUTING.md](CONTRIBUTING.md).

Everything here is MIT-licensed (see [LICENSE](LICENSE)), except the build in
`nes/`, which is Shapoco's ShapoNES, shared with his permission, and the
JPEG and PNG decoders that `images/` uses, which keep their own licences
(see [its README](images/README.md#whose-code)), and BTstack, which
`bluetooth/` builds on and which comes with the Pico SDK under Raspberry Pi's
licence for the Pico W and Pico 2 W (see
[its README](bluetooth/README.md#whose-code)).

## The shared clock

The shared clock has moved into Blair Leduc's
[PicoCalc shell](https://github.com/BlairLeduc/picocalc-shell): from
[v1.1](https://github.com/BlairLeduc/picocalc-shell/releases/tag/v1.1) the
shell keeps the time in the RP2350's always-on timer, which runs through
the reboot into each program, along with a record that says whether the
time is set and what the local time zone is. It also keeps the time in a
DS3231 when the PicoCalc has one, so it survives power-off, and on a
Pico 2 W can set it from the internet with `ntp`. Set the time with the
shell's `date` and `time` commands.

New programs should read it with `picocalc_shell_time()` from the shell's
`include/picocalc_shell.h` (linking `hardware_powman`):

```c
#include "picocalc_shell.h"

int64_t utc_ms;
int offset_min;
if (picocalc_shell_time(&utc_ms, &offset_min)) {
    time_t local = utc_ms / 1000 + offset_min * 60;
    /* ... */
}
```

[`picocalc_clock.h`](picocalc_clock.h) is kept for programs written for
this repository's earlier clock: the same `picocalc_clock_load()`,
`picocalc_clock_save()` and `picocalc_clock_is_set()`, now on top of the
shell's clock (and `picocalc_clock_load()` sets `TZ` from the shell's time
zone too). It needs shell v1.1's `picocalc_shell.h` on the include path and
`hardware_powman` linked.

The `settime` program that used to be here has been retired: the shell's
`date`, `time` and `ntp` do its job. Its last version is in this
repository's history, at release v1.0.

Since v0.17 the text starter dates files from the shell's clock itself,
when it's set, with nothing to set up.

## FatFs

Since v0.17 the text starter reads the card with FatFs (`f_open`,
`f_read` and the rest, from `drivers/filesystem.h`). Everything here that
touches the card uses it, so programs need v0.17 or later.
[`fatfs/fatfs.cmake`](fatfs/fatfs.cmake) builds it in two lines:

```cmake
include(path/to/picocalc-extras/fatfs/fatfs.cmake)
picocalc_fatfs(path/to/picocalc-text-starter)
target_link_libraries(myprogram picocalc_fs ...)
```

Programs on an older text starter can stay on picocalc-extras v1.3.

## Screenshots

[`screenshot/`](screenshot/) adds Shift+F5 screenshots to PDCurses
programs built on the PicoCalc backend. See its [README](screenshot/README.md).

## The picker and the decoders

**[HOWTO.md](HOWTO.md)** walks through using them in your own program, with
a small working example in [`examples/showpic`](examples/showpic/) that
lists the pictures on the card and shows the one you pick. The details are in
[picker/README.md](picker/README.md) and [images/README.md](images/README.md).
Both build into a Pico SDK program with one `include()` in its CMakeLists.txt.
The decoders need `git submodule update --init` first, for PNGdec.

## Status

These are test builds from one user and one PicoCalc. Reports and
improvements are welcome.
