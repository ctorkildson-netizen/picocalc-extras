/* host_test.c -- decode a picture file the way the PicoCalc does, and write
 * the 320x320 screen it would show as a PPM.   host_test in.jpg out.ppm */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "format.h"
#include "canvas.h"

struct img_src { FILE *f; uint32_t size; };

size_t src_read(img_src_t *s, void *buf, size_t n) { return fread(buf, 1, n, s->f); }
bool src_seek(img_src_t *s, uint32_t pos) { return fseek(s->f, pos, SEEK_SET) == 0; }
uint32_t src_tell(img_src_t *s) { return (uint32_t)ftell(s->f); }
uint32_t src_size(img_src_t *s) { return s->size; }

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: host_test in out.ppm\n"); return 2; }
	img_src_t src;
	src.f = fopen(argv[1], "rb");
	if (!src.f) { perror(argv[1]); return 1; }
	fseek(src.f, 0, SEEK_END);
	src.size = (uint32_t)ftell(src.f);
	uint8_t head[16];
	fseek(src.f, 0, SEEK_SET);
	size_t n = fread(head, 1, sizeof head, src.f);
	const image_format_t *fmt = format_for_head(head, n);
	if (!fmt) { printf("not a known format\n"); return 1; }
	fseek(src.f, 0, SEEK_SET);

	static uint16_t fb[320 * 320];
	canvas_t c;
	canvas_init(&c, fb, 320, 320, NULL);
	canvas_clear(&c);
	clock_t t0 = clock();
	const char *err = fmt->decode(&src, &c.sink);
	for (int r = c.rotate; r > 0; r -= 90) canvas_turn(&c);
	printf("rotate %d\n", c.rotate);
	printf("%s: %s, delivered %dx%d, shown %dx%d at %d,%d, %.0f ms on this PC\n",
	       argv[1], fmt->name, c.src_w, c.src_h, c.disp_w, c.disp_h, c.off_x, c.off_y,
	       (double)(clock() - t0) * 1000 / CLOCKS_PER_SEC);
	if (err) printf("error: %s\n", err);

	FILE *o = fopen(argv[2], "wb");
	fprintf(o, "P6 320 320 255\n");
	for (int i = 0; i < 320 * 320; i++) {
		uint16_t v = fb[i];
		uint8_t r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
		fputc(r, o); fputc(g, o); fputc(b, o);
	}
	fclose(o);
	return err ? 1 : 0;
}
