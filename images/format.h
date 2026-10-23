/* format.h -- the interface between a program and its picture formats.
 *
 * A format is one C file that fills in an image_format_t and is listed in
 * formats.c.  To add one:
 *   1. write fmt_xxx.c with a probe() and a decode() (see fmt_bmp.c, the
 *      simplest, as a model),
 *   2. add its image_format_t to the table in formats.c,
 *   3. add the file to images.cmake (and host/Makefile, to test it).
 *
 * decode() reads the file through the img_src_t functions only, and hands
 * pixels over through the img_sink_t, so a format never touches the SD card
 * or the display itself.  The program supplies the four src_ functions;
 * canvas.h is a ready-made sink that fits the picture into a frame.
 */
#ifndef PICOCALC_IMAGES_FORMAT_H
#define PICOCALC_IMAGES_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the picture file, opened by the program */
typedef struct img_src img_src_t;
size_t src_read(img_src_t *src, void *buf, size_t n);	/* bytes read; 0 at the end */
bool src_seek(img_src_t *src, uint32_t pos);		/* from the start of the file */
uint32_t src_tell(img_src_t *src);
uint32_t src_size(img_src_t *src);

/* where the pixels go */
typedef struct img_sink img_sink_t;
struct img_sink {
	/* The size the picture will appear on screen (it is fitted into
	   max_w x max_h, keeping its shape).  A format that can decode at a
	   reduced size (JPEG's 1/2, 1/4, 1/8) should pick the smallest that is
	   still at least this big. */
	int (*fit_w)(img_sink_t *, int w, int h);
	int (*fit_h)(img_sink_t *, int w, int h);

	/* Call once, before any pixels: the size of the picture as this
	   format will deliver it (after any reduction of its own), and the
	   picture's real size. */
	void (*begin)(img_sink_t *, int w, int h, int full_w, int full_h);

	/* A block of pixels, RGB565, row by row, w x h, at (x, y) in the
	   delivered picture.  Return false to stop decoding (the user gave up). */
	bool (*pixels)(img_sink_t *, int x, int y, int w, int h, const uint16_t *rgb565);

	/* After begin(): false if row y of the delivered picture won't be seen
	   (it falls between screen rows), so a format that reads row by row
	   can skip it. */
	bool (*need_row)(img_sink_t *, int y);

	/* Optional, before or after begin(): the picture should be turned this
	   many degrees clockwise (0, 90, 180, 270) to stand upright, as a
	   camera's EXIF orientation tag says. */
	void (*rotate)(img_sink_t *, int degrees);

	void *priv;
};

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
	return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

typedef struct {
	const char *name;			/* "JPEG" */
	const char *const *extensions;		/* lower case, no dot, NULL at the end */
	/* true if the first bytes of the file look like this format */
	bool (*probe)(const uint8_t *head, size_t n);
	/* NULL on success, or a short message for the screen */
	const char *(*decode)(img_src_t *src, img_sink_t *sink);
} image_format_t;

/* formats.c */
extern const image_format_t *const image_formats[];
const image_format_t *format_for_name(const char *filename);
const image_format_t *format_for_head(const uint8_t *head, size_t n);

#endif
