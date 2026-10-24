# picocalc-extras

Small pieces for programs on the ClockworkPi **PicoCalc** with an RP2350
(Pico 2 / Pico 2 W) and UF2 Loader: Shift+F5 screenshots for PDCurses
programs, a file picker, JPEG/PNG/BMP decoders, and a header for the
shell's clock.

They came out of porting Empire, Colossal Cave Adventure and VirtualT
(a TRS-80 Model 100 emulator) to the PicoCalc, and of writing its
[picture viewer](https://github.com/ctorkildson-netizen/picocalc-viewer).

| Piece | What it is |
|---|---|
| [`picocalc_clock.h`](picocalc_clock.h) | The shell's clock (shell v1.1), for programs written for the earlier shared clock here |
| [`screenshot/`](screenshot/) | Shift+F5 screenshots (BMP) for PDCurses programs on the PicoCalc |
| [`nes/`](nes/) | A ready-made build of the ShapoNES NES emulator for the PicoCalc |
| [`picker/`](picker/) | A file picker: lists a folder, moves around the card, returns the file chosen, with an optional banner and preview panel |
| [`images/`](images/) | JPEG, PNG and BMP decoders that fit a picture to a frame, plus a PC harness for testing them |
| [`HOWTO.md`](HOWTO.md) | How to use the picker and the decoders in your own program, with an example |

Everything here is MIT-licensed (see [LICENSE](LICENSE)), except the build in
`nes/`, which is Shapoco's ShapoNES, shared with his permission, and the
JPEG and PNG decoders that `images/` uses, which keep their own licences
(see [its README](images/README.md#whose-code)).

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

To have files dated, programs using the text-starter's FAT32 driver can
pass the shell's time to it with `fat32_set_time_source()`, from
[this branch](https://github.com/ctorkildson-netizen/picocalc-text-starter/tree/fat32-timestamps)
until it reaches the text-starter itself.

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
