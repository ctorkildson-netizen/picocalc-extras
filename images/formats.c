/* formats.c -- the table of picture formats.  Add new ones here. */
#include <ctype.h>
#include <string.h>
#include "format.h"

extern const image_format_t fmt_jpeg;
extern const image_format_t fmt_bmp;
extern const image_format_t fmt_png;

const image_format_t *const image_formats[] = {
	&fmt_jpeg,
	&fmt_bmp,
	&fmt_png,
	NULL
};

static bool same_ext(const char *a, const char *b)
{
	while (*a && *b)
		if (tolower((unsigned char)*a++) != *b++) return false;
	return *a == 0 && *b == 0;
}

const image_format_t *format_for_name(const char *filename)
{
	const char *dot = strrchr(filename, '.');
	if (!dot) return NULL;
	for (int i = 0; image_formats[i]; i++)
		for (int e = 0; image_formats[i]->extensions[e]; e++)
			if (same_ext(dot + 1, image_formats[i]->extensions[e]))
				return image_formats[i];
	return NULL;
}

const image_format_t *format_for_head(const uint8_t *head, size_t n)
{
	for (int i = 0; image_formats[i]; i++)
		if (image_formats[i]->probe(head, n))
			return image_formats[i];
	return NULL;
}
