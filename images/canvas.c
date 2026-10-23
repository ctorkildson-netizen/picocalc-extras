/* canvas.c -- fit a picture into the frame: each screen pixel takes the
 * picture pixel under its position (nearest neighbour).  Formats that can
 * decode at a reduced size keep the remaining reduction under 2:1. */
#include <string.h>
#include "canvas.h"

void canvas_fit(const canvas_t *c, int w, int h, int *dw, int *dh)
{
	/* the larger of w/fb_w and h/fb_h decides */
	if ((long)w * c->fb_h >= (long)h * c->fb_w) {
		*dw = c->fb_w;
		*dh = (int)(((long)h * c->fb_w + w / 2) / w);
	} else {
		*dh = c->fb_h;
		*dw = (int)(((long)w * c->fb_h + h / 2) / h);
	}
	if (*dw < 1) *dw = 1;
	if (*dh < 1) *dh = 1;
}

static int fit_w(img_sink_t *s, int w, int h)
{
	canvas_t *c = s->priv;
	int dw, dh;
	canvas_fit(c, w, h, &dw, &dh);
	return dw;
}

static int fit_h(img_sink_t *s, int w, int h)
{
	canvas_t *c = s->priv;
	int dw, dh;
	canvas_fit(c, w, h, &dw, &dh);
	return dh;
}

static void begin(img_sink_t *s, int w, int h, int full_w, int full_h)
{
	canvas_t *c = s->priv;
	c->full_w = full_w;
	c->full_h = full_h;
	c->src_w = w;
	c->src_h = h;
	canvas_fit(c, w, h, &c->disp_w, &c->disp_h);
	c->off_x = (c->fb_w - c->disp_w) / 2;
	c->off_y = (c->fb_h - c->disp_h) / 2;
}

/* the first screen position at or after picture position p */
static int to_disp(int p, int src, int disp)
{
	return (int)(((long)p * disp + src - 1) / src);
}

static bool pixels(img_sink_t *s, int x, int y, int w, int h, const uint16_t *px)
{
	canvas_t *c = s->priv;
	if (c->cancel && (++c->blocks & 15) == 0 && c->cancel()) {
		c->cancelled = true;
		return false;
	}
	int dx0 = to_disp(x, c->src_w, c->disp_w), dx1 = to_disp(x + w, c->src_w, c->disp_w);
	int dy0 = to_disp(y, c->src_h, c->disp_h), dy1 = to_disp(y + h, c->src_h, c->disp_h);
	if (dx1 > c->disp_w) dx1 = c->disp_w;
	if (dy1 > c->disp_h) dy1 = c->disp_h;
	for (int dy = dy0; dy < dy1; dy++) {
		int sy = (int)((long)dy * c->src_h / c->disp_h) - y;
		const uint16_t *srow = px + sy * w;
		uint16_t *drow = c->fb + (c->off_y + dy) * c->fb_w + c->off_x;
		for (int dx = dx0; dx < dx1; dx++)
			drow[dx] = srow[(int)((long)dx * c->src_w / c->disp_w) - x];
	}
	return true;
}

static bool need_row(img_sink_t *s, int y)
{
	canvas_t *c = s->priv;
	if (c->disp_h >= c->src_h) return true;
	/* is there a screen row whose picture row is y? */
	int dy = to_disp(y, c->src_h, c->disp_h);
	return dy < c->disp_h && (int)((long)dy * c->src_h / c->disp_h) == y;
}

static void rotate(img_sink_t *s, int degrees)
{
	canvas_t *c = s->priv;
	c->rotate = ((degrees % 360) + 360) % 360;
}

/* a square frame turned in place, four pixels at a time: no second buffer */
void canvas_turn(canvas_t *c)
{
	int n = c->fb_w;
	uint16_t *a = c->fb;
	for (int i = 0; i < n / 2; i++)
		for (int j = i; j < n - 1 - i; j++) {
			uint16_t t = a[i * n + j];
			a[i * n + j] = a[(n - 1 - j) * n + i];
			a[(n - 1 - j) * n + i] = a[(n - 1 - i) * n + (n - 1 - j)];
			a[(n - 1 - i) * n + (n - 1 - j)] = a[j * n + (n - 1 - i)];
			a[j * n + (n - 1 - i)] = t;
		}
	int t = c->disp_w; c->disp_w = c->disp_h; c->disp_h = t;
}

void canvas_clear(canvas_t *c)
{
	memset(c->fb, 0, (size_t)c->fb_w * c->fb_h * 2);
	c->src_w = c->src_h = c->full_w = c->full_h = 0;
	c->rotate = 0;
	c->cancelled = false;
	c->blocks = 0;
}

void canvas_init(canvas_t *c, uint16_t *fb, int w, int h, bool (*cancel)(void))
{
	memset(c, 0, sizeof *c);
	c->fb = fb;
	c->fb_w = w;
	c->fb_h = h;
	c->cancel = cancel;
	c->sink.fit_w = fit_w;
	c->sink.fit_h = fit_h;
	c->sink.begin = begin;
	c->sink.pixels = pixels;
	c->sink.need_row = need_row;
	c->sink.rotate = rotate;
	c->sink.priv = c;
}
