/* fmt_jpeg.c -- JPEG, with ChaN's TJpgDec.  Baseline JPEGs only (what
 * cameras and phones write); progressive ones get a message instead. */
#include <stdlib.h>
#include <string.h>
#include "format.h"
#include "tjpgd.h"

typedef struct {
	img_src_t *src;
	img_sink_t *sink;
	bool stopped;
} job_t;

static size_t in_func(JDEC *jd, uint8_t *buf, size_t n)
{
	job_t *job = jd->device;
	if (buf) return src_read(job->src, buf, n);
	/* skip n bytes */
	uint32_t pos = src_tell(job->src) + n;
	if (pos > src_size(job->src)) pos = src_size(job->src);
	uint32_t was = src_tell(job->src);
	return src_seek(job->src, pos) ? pos - was : 0;
}

static int out_func(JDEC *jd, void *bitmap, JRECT *r)
{
	job_t *job = jd->device;
	if (!job->sink->pixels(job->sink, r->left, r->top, r->right - r->left + 1,
	                       r->bottom - r->top + 1, bitmap)) {
		job->stopped = true;
		return 0;
	}
	return 1;
}

/* The EXIF orientation tag, as degrees clockwise to turn (0 if none).
   Looks through the segments before the picture data for APP1 "Exif". */
static int exif_rotation(img_src_t *src)
{
	uint8_t b[4];
	if (src_read(src, b, 2) != 2 || b[0] != 0xFF || b[1] != 0xD8) return 0;
	for (int seg = 0; seg < 16; seg++) {
		if (src_read(src, b, 4) != 4 || b[0] != 0xFF) return 0;
		unsigned len = (unsigned)b[2] << 8 | b[3];
		if (b[1] == 0xDA || len < 2) return 0;		/* the picture data starts */
		uint32_t next = src_tell(src) + len - 2;
		if (b[1] == 0xE1 && len >= 16) {
			enum { MAX = 1024 };
			uint8_t *e = malloc(MAX);
			if (!e) return 0;
			unsigned n = src_read(src, e, len - 2 < MAX ? len - 2 : MAX);
			int deg = 0;
			bool exif = n >= 14 && memcmp(e, "Exif\0\0", 6) == 0;
			if (exif) {
				const uint8_t *t = e + 6;		/* the TIFF header */
				unsigned tn = n - 6;
				bool le = t[0] == 'I';
				#define U16(p) (le ? (p)[0] | (p)[1] << 8 : (p)[0] << 8 | (p)[1])
				#define U32(p) (le ? (uint32_t)(p)[0] | (p)[1] << 8 | (p)[2] << 16 | (uint32_t)(p)[3] << 24 \
				               : (uint32_t)(p)[0] << 24 | (p)[1] << 16 | (p)[2] << 8 | (p)[3])
				uint32_t ifd = U32(t + 4);
				if (ifd + 2 <= tn) {
					unsigned count = U16(t + ifd);
					for (unsigned i = 0; i < count && ifd + 2 + 12 * (i + 1) <= tn; i++) {
						const uint8_t *en = t + ifd + 2 + 12 * i;
						if (U16(en) == 0x0112) {
							switch (U16(en + 8)) {
							case 3: case 4: deg = 180; break;
							case 6: case 5: deg = 90; break;
							case 8: case 7: deg = 270; break;
							}
							break;
						}
					}
				}
				#undef U16
				#undef U32
			}
			free(e);
			if (exif) return deg;		/* the first EXIF block decides */
		}
		if (!src_seek(src, next)) return 0;
	}
	return 0;
}

static bool probe(const uint8_t *h, size_t n)
{
	return n >= 3 && h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF;
}

static const char *decode(img_src_t *src, img_sink_t *sink)
{
	enum { POOL = 3500 + 6144 + JD_SZBUF + 256 };
	if (sink->rotate) sink->rotate(sink, exif_rotation(src));
	if (!src_seek(src, 0)) return "Can't read this file";
	void *pool = malloc(POOL);
	if (!pool) return "Not enough memory";
	JDEC jd;
	job_t job = { src, sink, false };
	JRESULT r = jd_prepare(&jd, in_func, pool, POOL, &job);
	if (r == JDR_OK) {
		jd.swap = 0;
		/* the smallest reduction (1/1 .. 1/8) still at least screen size */
		int want = sink->fit_w(sink, jd.width, jd.height);
		uint8_t scale = 0;
		while (scale < 3 && (jd.width >> (scale + 1)) >= want) scale++;
		sink->begin(sink, (jd.width + (1 << scale) - 1) >> scale,
		            (jd.height + (1 << scale) - 1) >> scale, jd.width, jd.height);
		r = jd_decomp(&jd, out_func, scale);
	}
	free(pool);
	if (job.stopped || r == JDR_OK) return NULL;
	switch (r) {
	case JDR_FMT3: return "This kind of JPEG isn't supported (progressive?)";
	case JDR_MEM1: case JDR_MEM2: return "Not enough memory for this JPEG";
	case JDR_INP: return "The file ends early";
	default: return "This JPEG is damaged";
	}
}

static const char *const extensions[] = { "jpg", "jpeg", "jpe", "jfif", NULL };

const image_format_t fmt_jpeg = { "JPEG", extensions, probe, decode };
