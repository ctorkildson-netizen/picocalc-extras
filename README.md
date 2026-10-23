# picocalc-extras

Small pieces for programs on the ClockworkPi **PicoCalc** with an RP2350
(Pico 2 / Pico 2 W) and UF2 Loader: a clock that every program can share,
a program that sets it from the internet, Shift+F5 screenshots for
PDCurses programs, a file picker, and JPEG/PNG/BMP decoders.

They came out of porting Empire, Colossal Cave Adventure and VirtualT
(a TRS-80 Model 100 emulator) to the PicoCalc, and of writing its
[picture viewer](https://github.com/ctorkildson-netizen/picocalc-viewer).

| Piece | What it is |
|---|---|
| [`picocalc_clock.h`](picocalc_clock.h) | One header: read or set a time of day that survives switching programs |
| [`settime/`](settime/) | A small program that sets that clock from NTP over WiFi, then returns to the shell |
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

The PicoCalc has no battery-backed clock. But the RP2350's always-on
(POWMAN) timer runs from the crystal and **keeps running through the soft
reboot UF2 Loader does when it switches from one program to the next**. Only
switching the power off clears it.

So one program can set the time once after power-on, and every program
started after it can read it. That was tested on a Pico 2 W: the time was
set over NTP, then the PicoCalc went to the shell, into Adventure, back to
the shell and into VirtualT, which found the time still there.

```c
#include "picocalc_clock.h"

/* at startup: set this program's time()/localtime() from the shared clock */
if (picocalc_clock_load()) {
    /* the real time is known */
}

/* after learning the time some other way (NTP, the user typing it): */
picocalc_clock_save();
```

`picocalc_clock_is_set()` says whether the shared clock holds a real time
without changing anything. The clock holds UTC; set the `TZ` environment
variable (a POSIX TZ string) to get local time from `localtime()`.

The convention that makes it trustworthy: **only real times go in** (from
NTP, or typed by the user), never a guess, so any program reading it can
rely on it. The header needs the Pico SDK's `pico_aon_timer` library.

## settime

[`settime/`](settime/) holds the source, a ready-built
[`settime.uf2`](settime/settime.uf2), and [SETTIME.TXT](settime/SETTIME.TXT)
for users. In short:

- Copy `settime.uf2` into `pico2-apps` and run it after switching the
  PicoCalc on. It joins WiFi, gets the time from NTP, puts it in the
  shared clock and returns to the shell.
- If the clock is already set, it shows the time and returns at once
  (N fetches it again anyway).
- Settings live in `/settime.cfg`, or `/virtualt/virtualt.cfg` if that's
  missing (same format):

  ```
  ssid=YourNetwork
  password=YourWiFiPassword
  tz=PST8PDT,M3.2.0,M11.1.0
  ntp=pool.ntp.org
  ```

- Needs a **Pico 2 W** and 2.4 GHz WPA2 WiFi. The file is about 350 KB of
  flash, almost all of it the CYW43 WiFi chip's firmware.

To build: `git submodule update --init`, then build the `settime/`
directory with the Pico SDK 2.x (including its `lib/cyw43-driver` and
`lib/lwip` submodules), for `PICO_BOARD=pico2_w`.

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
