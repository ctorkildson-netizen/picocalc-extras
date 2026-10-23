/* canvas.h -- an img_sink_t that fits a picture into a w x h RGB565 frame,
 * keeping its shape, centred on black. */
#ifndef PICOCALC_IMAGES_CANVAS_H
#define PICOCALC_IMAGES_CANVAS_H

#include "format.h"

typedef struct {
	img_sink_t sink;
	uint16_t *fb;			/* fb_w * fb_h pixels */
	int fb_w, fb_h;
	int src_w, src_h;		/* the picture as the format delivers it */
	int full_w, full_h;		/* its real size */
	int disp_w, disp_h, off_x, off_y;	/* where it lands in fb */
	bool (*cancel)(void);		/* polled while decoding; true stops it */
	unsigned blocks;
	int rotate;			/* degrees clockwise the picture asks for */
	bool cancelled;			/* decoding was stopped by cancel() */
} canvas_t;

void canvas_init(canvas_t *c, uint16_t *fb, int w, int h, bool (*cancel)(void));
void canvas_clear(canvas_t *c);

/* turn the frame 90 degrees clockwise (the frame must be square) */
void canvas_turn(canvas_t *c);

/* the fitted size of a w x h picture */
void canvas_fit(const canvas_t *c, int w, int h, int *dw, int *dh);

#endif
