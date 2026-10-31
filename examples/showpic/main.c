/* showpic -- the smallest useful program built on picocalc-extras' picker and
 * picture decoders: choose a picture from the SD card, see it full screen,
 * any key to choose another, Esc in the list to go back to the shell.
 *
 * HOWTO.md in picocalc-extras walks through it. */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "drivers/lcd.h"
#include "drivers/keyboard.h"
#include "drivers/southbridge.h"
#include "drivers/filesystem.h"
#include "picocalc_shell.h"
#include "picker.h"		/* picocalc-extras/picker */
#include "format.h"		/* picocalc-extras/images */
#include "canvas.h"

volatile bool user_interrupt;	/* the keyboard driver sets it on Brk */

/* ---- 1. The file the decoders read: four functions, over a FatFs file.
   Reads go through a 16 KB buffer, because many small f_read() calls are
   slow. */

#define BUF 16384
static uint8_t buf[BUF];

struct img_src {
	FIL f;
	uint32_t pos, size;		/* where the decoder is; the file's size */
	uint32_t buf_at, buf_len;	/* which part of the file buf holds */
};

size_t src_read(img_src_t *s, void *out, size_t n)
{
	size_t done = 0;
	while (done < n && s->pos < s->size) {
		if (s->pos < s->buf_at || s->pos >= s->buf_at + s->buf_len) {
			UINT got = 0;
			if (f_lseek(&s->f, s->pos) != FR_OK ||
			    f_read(&s->f, buf, BUF, &got) != FR_OK || got == 0)
				break;
			s->buf_at = s->pos;
			s->buf_len = (uint32_t)got;
		}
		size_t k = s->buf_at + s->buf_len - s->pos;
		if (k > n - done) k = n - done;
		memcpy((uint8_t *)out + done, buf + (s->pos - s->buf_at), k);
		done += k;
		s->pos += (uint32_t)k;
	}
	return done;
}

bool src_seek(img_src_t *s, uint32_t pos)
{
	if (pos > s->size) return false;
	s->pos = pos;
	return true;
}

uint32_t src_tell(img_src_t *s) { return s->pos; }
uint32_t src_size(img_src_t *s) { return s->size; }

/* ---- 2. Showing one picture: open it, pick its format, decode it into a
   canvas (a 320x320 frame), stand it up, copy the frame to the screen. */

static uint16_t frame[320 * 320];

static const char *show(const char *path)
{
	img_src_t src = { 0 };
	if (f_open(&src.f, path, FA_READ) != FR_OK) return "Can't open it";
	src.size = (uint32_t)f_size(&src.f);

	uint8_t head[16];
	size_t n = src_read(&src, head, sizeof head);
	const image_format_t *fmt = format_for_head(head, n);	/* by its first bytes */
	const char *err = fmt ? NULL : "Not a picture this knows";

	if (!err) {
		canvas_t c;
		canvas_init(&c, frame, 320, 320, NULL);	/* NULL: no way to stop it */
		canvas_clear(&c);
		src_seek(&src, 0);
		err = fmt->decode(&src, &c.sink);
		if (!err) {
			for (int r = c.rotate; r > 0; r -= 90) canvas_turn(&c);	/* as the camera asks */
			lcd_blit(frame, 0, 0, 320, 320);
		}
	}
	f_close(&src.f);
	return err;
}

/* ---- 3. The list: only pictures, starting in /pictures if there is one. */

static bool is_picture(const char *name) { return format_for_name(name) != NULL; }

int main(void)
{
	sb_init();
	lcd_init();
	lcd_set_font(&font_8x10);
	lcd_enable_cursor(false);
	fs_init();
	keyboard_init();
	keyboard_set_background_poll(true);

	static const picker_style_t style = {
		.title = "Show a picture",
		.filter = is_picture,
		.noun = "picture", .nouns = "pictures",
	};
	char dir[PICKER_PATH] = "/pictures", path[PICKER_PATH];
	DIR d;
	if (f_opendir(&d, dir) == FR_OK) f_closedir(&d);
	else strcpy(dir, "/");

	for (;;) {
		if (picker_run(&style, dir, NULL, path) != PICKER_CHOSEN) {
			lcd_clear_screen();
			picocalc_shell_exit();		/* Esc: back to the shell */
			lcd_putstr(0, 15, "No SD card: switch off to leave.");
			for (;;) tight_loop_contents();
		}
		lcd_clear_screen();
		const char *err = show(path);
		if (err) lcd_putstr(0, 15, err);
		keyboard_get_key();			/* any key: back to the list */
		lcd_clear_screen();
	}
}
