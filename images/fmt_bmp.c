/* fmt_bmp.c -- Windows BMP: uncompressed 1, 4, 8 (with palette), 16, 24 and
 * 32 bits per pixel, top-down or bottom-up.  The simplest format here, and
 * a good model for writing another. */
#include <stdlib.h>
#include <string.h>
#include "format.h"

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }

static bool probe(const uint8_t *h, size_t n)
{
	return n >= 2 && h[0] == 'B' && h[1] == 'M';
}

static const char *decode(img_src_t *src, img_sink_t *sink)
{
	uint8_t hdr[54];
	if (src_read(src, hdr, sizeof hdr) != sizeof hdr) return "The file ends early";
	uint32_t data = le32(hdr + 10), hsize = le32(hdr + 14);
	int32_t w = (int32_t)le32(hdr + 18), h = (int32_t)le32(hdr + 22);
	int bpp = le16(hdr + 28);
	uint32_t comp = le32(hdr + 30), ncolours = le32(hdr + 46);
	bool top_down = h < 0;
	if (top_down) h = -h;
	if (hsize < 40 || w <= 0 || h <= 0 || w > 16384 || h > 16384)
		return "This BMP is damaged";
	/* 3 = BI_BITFIELDS, accepted for the usual 16- and 32-bit layouts */
	if (comp != 0 && !(comp == 3 && (bpp == 16 || bpp == 32)))
		return "Compressed BMPs aren't supported";
	if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
		return "This kind of BMP isn't supported";

	uint16_t pal[256];
	if (bpp <= 8) {
		int n = ncolours ? (int)ncolours : 1 << bpp;
		if (n > 256) n = 256;
		src_seek(src, 14 + hsize);
		for (int i = 0; i < n; i++) {
			uint8_t q[4];
			if (src_read(src, q, 4) != 4) return "The file ends early";
			pal[i] = rgb565(q[2], q[1], q[0]);
		}
	}
	bool rgb555 = true;			/* 16 bit: 5-5-5 unless the masks say 5-6-5 */
	if (comp == 3 && bpp == 16) {
		uint8_t m[4];
		src_seek(src, 14 + 40);
		if (src_read(src, m, 4) == 4 && le32(m) == 0xF800) rgb555 = false;
	}

	uint32_t stride = ((uint32_t)w * bpp + 31) / 32 * 4;
	uint8_t *row = malloc(stride);
	uint16_t *out = malloc((size_t)w * 2);
	if (!row || !out) { free(row); free(out); return "Not enough memory"; }
	sink->begin(sink, w, h, w, h);
	const char *err = NULL;
	for (int32_t i = 0; i < h; i++) {
		int32_t y = top_down ? i : h - 1 - i;
		if (sink->need_row && !sink->need_row(sink, y)) continue;
		if (!src_seek(src, data + (uint32_t)i * stride) || src_read(src, row, stride) != stride) {
			err = "The file ends early";
			break;
		}
		for (int32_t x = 0; x < w; x++) {
			uint16_t c;
			switch (bpp) {
			case 1:  c = pal[(row[x >> 3] >> (7 - (x & 7))) & 1]; break;
			case 4:  c = pal[(row[x >> 1] >> ((x & 1) ? 0 : 4)) & 15]; break;
			case 8:  c = pal[row[x]]; break;
			case 16: {
				uint16_t v = le16(row + x * 2);
				c = rgb555 ? (uint16_t)(((v & 0x7FE0) << 1) | (v & 0x1F)) : v;
				break;
			}
			case 24: c = rgb565(row[x * 3 + 2], row[x * 3 + 1], row[x * 3]); break;
			default: c = rgb565(row[x * 4 + 2], row[x * 4 + 1], row[x * 4]); break;
			}
			out[x] = c;
		}
		if (!sink->pixels(sink, 0, y, w, 1, out)) break;
	}
	free(row);
	free(out);
	return err;
}

static const char *const extensions[] = { "bmp", "dib", NULL };

const image_format_t fmt_bmp = { "BMP", extensions, probe, decode };
