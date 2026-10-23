/* fmt_png.c -- PNG, with Larry Bank's PNGdec.  Up to 4096 pixels wide,
 * 1 to 8 bits per channel, not interlaced; see-through parts go on black.
 *
 * A PNG can't be decoded at a reduced size the way a JPEG can, so every row
 * is unpacked (each depends on the one before), but only the rows the screen
 * shows are turned into colours. */
#include <stdlib.h>
#include <string.h>
#include "format.h"

#define __MCUXPRESSO			/* PNGdec: plain C, no Arduino */
/* room for two rows of the widest picture, plus PNGdec lining each one up
   on 16 bytes (its own size check leaves that out, so ours below adds it) */
#define PNG_WIDEST 4096
#define PNG_MAX_BUFFERED_PIXELS ((PNG_WIDEST * 4 + 1) * 2 + 32)
#include "PNGdec.h"
#include "png.inl"

typedef struct {
	img_src_t *src;
	img_sink_t *sink;
	uint16_t *row;
	bool stopped;
} job_t;

static int32_t png_read(PNGFILE *f, uint8_t *buf, int32_t n)
{
	job_t *job = f->fHandle;
	int32_t got = (int32_t)src_read(job->src, buf, (size_t)n);
	f->iPos = (int32_t)src_tell(job->src);
	return got;
}

static int32_t png_seek(PNGFILE *f, int32_t pos)
{
	job_t *job = f->fHandle;
	if (!src_seek(job->src, (uint32_t)pos)) return -1;
	f->iPos = pos;
	return pos;
}

static int png_draw(PNGDRAW *d)
{
	job_t *job = d->pUser;
	if (job->sink->need_row && !job->sink->need_row(job->sink, d->y)) return 1;
	PNGRGB565(d, job->row, PNG_RGB565_LITTLE_ENDIAN, 0x000000, d->iHasAlpha);
	if (!job->sink->pixels(job->sink, 0, d->y, d->iWidth, 1, job->row)) {
		job->stopped = true;
		return 0;
	}
	return 1;
}

static bool probe(const uint8_t *h, size_t n)
{
	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	return n >= 8 && memcmp(h, sig, 8) == 0;
}

static const char *decode(img_src_t *src, img_sink_t *sink)
{
	PNGIMAGE *png = calloc(1, sizeof *png);
	if (!png) return "Not enough memory for a PNG";
	job_t job = { src, sink, NULL, false };
	png->pfnRead = png_read;
	png->pfnSeek = png_seek;
	png->pfnDraw = png_draw;
	png->PNGFile.fHandle = &job;
	png->PNGFile.iSize = (int32_t)src_size(src);
	const char *err = NULL;
	int r = PNGInit(png);
	if (r == PNG_UNSUPPORTED_FEATURE)
		err = png->iInterlaced ? "Interlaced PNGs aren't supported"
		                       : "16-bit PNGs aren't supported";
	else if (r == PNG_TOO_BIG || (r == PNG_SUCCESS && (png->iPitch + 1) * 2 + 32 > PNG_MAX_BUFFERED_PIXELS))
		err = "This PNG is too wide";
	else if (r != PNG_SUCCESS) err = "This PNG is damaged";
	else if (!(job.row = malloc((size_t)png->iWidth * 2))) err = "Not enough memory for this PNG";
	else {
		sink->begin(sink, png->iWidth, png->iHeight, png->iWidth, png->iHeight);
		if (!src_seek(src, 0)) err = "Can't read this file";
		else {
			png->PNGFile.iPos = 0;
			DecodePNG(png, &job, 0);
			if (!job.stopped && png->iError != PNG_SUCCESS)
				err = png->iError == PNG_MEM_ERROR ? "Not enough memory for this PNG"
				                                   : "This PNG is damaged";
		}
	}
	free(job.row);
	free(png);
	return err;
}

static const char *const extensions[] = { "png", NULL };

const image_format_t fmt_png = { "PNG", extensions, probe, decode };
