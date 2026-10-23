# images

Picture decoders for the PicoCalc (or any small RP2350 program): JPEG, PNG
and BMP, fitted to a frame of any size. They came out of the
[picture viewer](https://github.com/ctorkildson-netizen/picocalc-viewer).

| Format | What it reads | Doesn't read |
|---|---|---|
| JPEG | baseline JPEGs (what cameras and phones write), with the EXIF orientation tag | progressive JPEGs |
| PNG | 1 to 8 bits per channel, palette, grey, colour, see-through (put on black) | interlaced or 16-bit PNGs |
| BMP | 1, 4, 8, 16, 24 and 32 bits, uncompressed and bit-field | RLE-compressed BMPs |

The decoders don't touch the SD card or the screen. A program hands one a
file to read, through four functions it writes itself, and a *sink* to take
the pixels. `canvas.h` is a ready-made sink: it fits the picture into an
RGB565 frame (keeping its shape, centred on black) and can rotate it 90
degrees.

```c
#include "format.h"
#include "canvas.h"

/* yours: read the open file, however your program does that */
struct img_src { /* your file handle */ };
size_t src_read(img_src_t *s, void *buf, size_t n);
bool src_seek(img_src_t *s, uint32_t pos);
uint32_t src_tell(img_src_t *s);
uint32_t src_size(img_src_t *s);

static uint16_t frame[320 * 320];
canvas_t c;
canvas_init(&c, frame, 320, 320, NULL);    /* or a function that says "stop" */
canvas_clear(&c);
const image_format_t *fmt = format_for_head(first_bytes, n);   /* or format_for_name() */
const char *err = fmt->decode(&src, &c.sink);   /* NULL, or a message to show */
for (int r = c.rotate; r > 0; r -= 90) canvas_turn(&c);   /* as the camera asks */
/* frame now holds the picture */
```

Reading a big file in small pieces is slow with the text-starter FAT32
driver, so the viewer reads through a 16 KB buffer (see its `main.c`).

## Speed and memory

On a PicoCalc at 250 MHz, a 4000x3000 camera JPEG fits to 320x320 in
2.5 to 3 seconds: JPEGs can be decoded at 1/2, 1/4 or 1/8 size, and the decoder
picks the smallest that still fills the frame. PNGs can't, so every row
of the picture is unpacked: a 640x480 PNG takes about half a second, and a
4096x3072 one 11 to 12 seconds.

A JPEG needs about 12 KB while decoding, a PNG about 80 KB (most of it
PNGdec's decompression window and two rows of the picture), both taken
with `malloc()` and given back afterwards. PNGs can be up to 4096 pixels
wide with see-through colour, a little wider without.

## Adding a format

One C file: a `probe()` that recognises the file's first bytes, and a
`decode()` that reads it and hands rows or blocks of pixels to the sink.
`fmt_bmp.c` is the simplest one to copy. Then add it to the table in
`formats.c`, to `images.cmake`, and to `host/Makefile`. See
[format.h](format.h).

## Testing on a PC

`host/` decodes a picture on a PC exactly as the PicoCalc does and writes
the 320x320 screen as a PPM file:

```
cd host && make
./host_test picture.jpg screen.ppm
```

## Building

`git submodule update --init` (for PNGdec), then with the Pico SDK and CMake:

```cmake
include(path/to/picocalc-extras/images/images.cmake)
target_link_libraries(myprogram picocalc_images ...)
```

## Whose code

- JPEG: ChaN's TJpgDec, a copy with two settings changed: see
  [tjpgd/README.txt](tjpgd/README.txt).
- PNG: Larry Bank's [PNGdec](https://github.com/bitbank2/PNGdec) (Apache
  2.0, with zlib's inflate under the zlib licence), a submodule in
  `modules/PNGdec`. It's pinned to a commit just after its 1.1.7 release:
  that commit corrects 1.1.7's check on how wide a picture may be.
- The rest: MIT, like the rest of picocalc-extras.
