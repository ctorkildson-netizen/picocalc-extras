# How to use the picker and the picture decoders

This is a walk through putting [`picker/`](picker/) and [`images/`](images/)
into your own PicoCalc program. The working example is
[`examples/showpic`](examples/showpic/): about 130 lines that list the
pictures on the SD card, show the one you pick full screen, and go back to
the shell on Esc. Build it first, then copy what you need from it.

You need the Pico SDK 2.x, a PicoCalc with a Pico 2 W, UF2 Loader,
and Blair Leduc's
[picocalc-text-starter](https://github.com/BlairLeduc/picocalc-text-starter)
drivers in your program (the picker uses them for the screen, keyboard and
SD card; the decoders don't need them).

- [1. Build the example](#1-build-the-example)
- [2. Add picocalc-extras to your program](#2-add-picocalc-extras-to-your-program)
- [3. A file list with the picker](#3-a-file-list-with-the-picker)
- [4. Showing a picture](#4-showing-a-picture)
- [5. A preview panel](#5-a-preview-panel)
- [6. Your own keys in the list](#6-your-own-keys-in-the-list)
- [7. Next and previous](#7-next-and-previous)
- [8. Testing on a PC](#8-testing-on-a-pc)
- [9. Things to know](#9-things-to-know)

## 1. Build the example

```
git clone --recursive https://github.com/ctorkildson-netizen/picocalc-extras
cd picocalc-extras/examples/showpic
cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build
```

Copy `build/showpic.uf2` into `pico2-apps` on the SD card and run it from
the shell. (Already cloned without `--recursive`? Run
`git submodule update --init --recursive` in picocalc-extras.)

## 2. Add picocalc-extras to your program

As a submodule, pinned to a release:

```
git submodule add https://github.com/ctorkildson-netizen/picocalc-extras modules/picocalc-extras
cd modules/picocalc-extras
git checkout v1.0
git submodule update --init images/modules/PNGdec
```

Then in your `CMakeLists.txt`, after `pico_sdk_init()`:

```cmake
include(${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-extras/picker/picker.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/modules/picocalc-extras/images/images.cmake)

# ... add_executable(myprogram ...) with the text-starter drivers ...

target_compile_definitions(myprogram PRIVATE PICO_MALLOC_PANIC=0)
target_link_libraries(myprogram picocalc_picker picocalc_images
    pico_stdlib hardware_gpio hardware_i2c hardware_spi hardware_pio hardware_watchdog)
```

Two things the include path must give the picker: the text-starter folder
itself, so that `#include "drivers/lcd.h"` works, and its `drivers/`
folder. [The example's CMakeLists.txt](examples/showpic/CMakeLists.txt)
shows both.

`PICO_MALLOC_PANIC=0` makes `malloc()` return NULL when memory runs out,
instead of stopping the program. The decoders check for NULL and give a
message ("Not enough memory for this PNG"), so this is worth having.

You only need the piece you use: `picker` alone, `images` alone, or both.

## 3. A file list with the picker

Describe the list in a `picker_style_t`, then call `picker_run()`:

```c
#include "picker.h"

static bool is_text(const char *name)   /* which files to list */
{
	const char *dot = strrchr(name, '.');
	return dot && strcasecmp(dot, ".txt") == 0;
}

static const picker_style_t style = {
	.title = "Documents",           /* shown across the top */
	.filter = is_text,              /* NULL lists every file */
	.noun = "document", .nouns = "documents",   /* "12 documents" */
};

char dir[PICKER_PATH] = "/", path[PICKER_PATH];
int got = picker_run(&style, dir, NULL, path);
if (got == PICKER_CHOSEN) {
	/* path: the file's full path, e.g. "/letters/aunt.txt" */
} else {
	/* 0: the user pressed Esc */
}
```

- `dir` is where the list starts, and `picker_run()` changes it as the user
  moves around, so the next call opens where they left off.
- The third argument names an entry to put the cursor on first, such as
  the file just closed, or NULL.
- Folders are always listed, first, then the files your filter accepts, in
  name order. Hidden files and names starting with a dot are left out.
- For a picture across the top instead of a title, set `.banner` to RGB565
  pixels and `.banner_w`, `.banner_h` to its size (the viewer's is 320x44).

The picker draws the whole screen and leaves it drawn, so clear it when
you take over.

## 4. Showing a picture

The decoders read the file through four functions **you** write, so they
work with any way of reading files. Over the text-starter's FAT32, with a
16 KB buffer (see [why](#9-things-to-know)), they're the first thing in
[the example's main.c](examples/showpic/main.c): copy them.

Then, to show a picture:

```c
#include "format.h"
#include "canvas.h"

static uint16_t frame[320 * 320];          /* RGB565, 200 KB */

img_src_t src = { 0 };
fat32_open(&src.f, path);                  /* check for errors */
src.size = fat32_size(&src.f);

uint8_t head[16];
size_t n = src_read(&src, head, sizeof head);
const image_format_t *fmt = format_for_head(head, n);   /* NULL: not a picture */

canvas_t c;
canvas_init(&c, frame, 320, 320, NULL);
canvas_clear(&c);
src_seek(&src, 0);
const char *err = fmt->decode(&src, &c.sink);           /* NULL: it worked */
for (int r = c.rotate; r > 0; r -= 90) canvas_turn(&c); /* camera orientation */
lcd_blit(frame, 0, 0, 320, 320);
fat32_close(&src.f);
```

- `format_for_head()` goes by the file's first bytes, which is safer than
  the name. `format_for_name()` goes by the ending (`.jpg`, `.png`, `.bmp`
  and the rest), which is what a list filter wants.
- `decode()` returns NULL, or a short message for the screen such as
  "Interlaced PNGs aren't supported".
- The canvas fits the picture into the frame, keeping its shape, centred
  on black. After decoding, `c.full_w` and `c.full_h` give the picture's
  real size, and `c.disp_w`, `c.disp_h` the size it was drawn at.
- `c.rotate` is how far the camera's orientation tag says to turn it (0,
  90, 180, 270). `canvas_turn()` turns a square frame a quarter turn
  clockwise, in place; it's also how a "rotate" key works.
- The frame can be any size: `canvas_init(&c, thumb, 116, 116, ...)` makes
  a thumbnail.

**Letting the user give up:** the last argument of `canvas_init()` is a
function the canvas calls now and then while decoding. If it returns true,
decoding stops and `c.cancelled` is set (`decode()` still returns NULL).
`keyboard_key_available` is the usual one: a key press stops a slow load.

## 5. A preview panel

The list takes the left 192 pixels of the screen, leaving a square panel
on the right. Give the style a `.preview` function, and when the cursor
has rested on a file for a quarter of a second, the picker calls it to
draw into that panel (for a folder, the picker draws a folder there
itself):

```c
static uint16_t thumb[116 * 116];

static void preview(const char *path, int x, int y, int size)
{
	/* ... open path into src as in section 4 ... */
	canvas_t c;
	canvas_init(&c, thumb, size, size, keyboard_key_available);
	canvas_clear(&c);
	if (!fmt->decode(&src, &c.sink) && !c.cancelled)
		lcd_blit(thumb, x, y, size, size);
	fat32_close(&src.f);
}
```

`size` is 116 today; size your buffer to match. Pass
`keyboard_key_available` as the canvas's give-up function, so that moving
the cursor while a big picture decodes stops at once. The picture viewer
also keeps the last few thumbnails, so moving back and forth is instant.

## 6. Your own keys in the list

List the keys in `.keys`, and a line for each in `.keys_help`, which the
picker adds to its `?` box:

```c
static const char *const my_help[] = {
	"  s              slide show",
	NULL,
};
static const picker_style_t style = {
	/* ... */
	.keys = "sS", .keys_help = my_help,
};

int got = picker_run(&style, dir, NULL, path);
if (got == 's' || got == 'S') {
	if (picker_on_folder()) { /* path is the highlighted folder */ }
	else                    { /* path is the highlighted file */ }
}
```

On the ".." line, or in an empty folder, `path` is the folder being
listed and `picker_on_folder()` is true.

## 7. Next and previous

After `picker_run()` returns, the list it showed is still there to use:

```c
int n = picker_file_count();               /* files only, not folders */
int i = picker_file_index("aunt.txt");     /* -1 if not there */
const char *next = picker_file_name((i + 1) % n);
```

`picker_load(dir, filter)` reads a folder's list without showing it, for
when your program starts somewhere without the list (a slide show at
power-on, say).

## 8. Testing on a PC

[`images/host`](images/host/) builds the decoders on a PC and writes the
screen the PicoCalc would show as a PPM file, which most image viewers open:

```
cd images/host && make
./host_test photo.jpg screen.ppm
```

It prints the picture's size, the size it was decoded at, where it lands
on the screen and how long it took on the PC. That's the quickest way to
try a new format, or a file that won't open.

## 9. Things to know

- **Reading speed.** The text-starter's `fat32_read()` (v0.15) follows the
  file's cluster chain from its start on every call, so a big file read in
  small pieces gets slower the further in it goes. Read in large pieces:
  the 16 KB buffer in the example is enough to make it reasonable. A fix
  has been offered to Blair; the picture viewer uses it already, from
  [this branch](https://github.com/ctorkildson-netizen/picocalc-text-starter/tree/fat32-sequential-read).
- **Time.** At the usual 150 MHz, a 4000x3000 camera JPEG takes about 4
  seconds, a 640x480 PNG under a second, a 4000-pixel PNG over 15. PNGs
  can't be decoded at a reduced size the way JPEGs can. The viewer runs at
  250 MHz for this; see its `main()`.
- **Memory.** A 320x320 frame is 200 KB of the RP2350's 520 KB. While
  decoding, a JPEG takes about 12 KB more with `malloc()`, and a PNG about
  80 KB, given back afterwards. The picker's list of up to 1024 entries
  takes 16 KB, plus the names.
- **What can't be read:** progressive JPEGs, interlaced or 16-bit PNGs,
  compressed BMPs. `decode()` says so in its message.
- **`user_interrupt`.** The text-starter keyboard driver expects your
  program to define `volatile bool user_interrupt;` (it sets it on Brk).
- **Adding a format** is one C file. See the end of
  [images/README.md](images/README.md#adding-a-format).
