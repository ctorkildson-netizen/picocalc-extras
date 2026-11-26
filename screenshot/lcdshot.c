/* lcdshot.c -- a screenshot of whatever is on the PicoCalc's LCD, for any
 * program on Blair Leduc's picocalc-text-starter drivers (v0.17 or later,
 * which can read the LCD back: lcd_unblit()). See lcdshot.h and README.md.
 *
 * The LCD's frame memory is read back a row at a time and written as a
 * 320x320 24-bit BMP to /screenshots on the SD card, named after the program
 * and, when the program has picocalc-shell and its clock is set, the date
 * and time:
 * /screenshots/ftp-2026-11-23_114502.bmp (or ftp-001.bmp without a clock).
 * MIT, like the rest of picocalc-extras.
 */
#include <stdio.h>
#include <string.h>
#include "ff.h"
#include "drivers/lcd.h"

/* The shell's clock, for the date in the file's name -- if the program has
   Blair Leduc's picocalc-shell at all. Declared weak, so a program without
   it (an RP2040 build, say) links anyway, and its screenshots are numbered. */
bool picocalc_shell_time(int64_t *utc_ms, int *offset_min) __attribute__((weak));
#include "lcdshot.h"

#define W 320
#define H 320

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static bool exists(const char *path)
{
	FILINFO fi;
	return f_stat(path, &fi) == FR_OK;
}

/* the file's name: dated when the shell's clock is set, else numbered */
static bool name_for(const char *app, char *out, size_t n)
{
	int64_t utc_ms;
	int offset_min;
	if (picocalc_shell_time && picocalc_shell_time(&utc_ms, &offset_min)) {
		int64_t s = utc_ms / 1000 + (int64_t)offset_min * 60;
		int64_t days = s / 86400, sod = s % 86400;
		/* days since 1970-01-01 to a date (Howard Hinnant's civil_from_days) */
		int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
		unsigned doe = (unsigned)(z - era * 146097);
		unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
		int y = (int)(yoe + era * 400);
		unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
		unsigned d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
		if (m <= 2) y++;
		char stamp[24];
		snprintf(stamp, sizeof stamp, "%04d-%02u-%02u_%02u%02u%02u", y, m, d,
			 (unsigned)(sod / 3600), (unsigned)(sod / 60 % 60), (unsigned)(sod % 60));
		snprintf(out, n, "/screenshots/%s-%s.bmp", app, stamp);
		for (int k = 2; exists(out) && k < 100; k++)	/* two in one second */
			snprintf(out, n, "/screenshots/%s-%s-%d.bmp", app, stamp, k);
		return !exists(out);
	}
	for (int k = 1; k < 1000; k++) {
		snprintf(out, n, "/screenshots/%s-%03d.bmp", app, k);
		if (!exists(out)) return true;
	}
	return false;
}

bool lcd_screenshot(const char *app, char *saved, size_t saved_size)
{
	char path[80];
	FRESULT e = f_mkdir("/screenshots");
	if (e != FR_OK && e != FR_EXIST) return false;
	if (!name_for(app && *app ? app : "screen", path, sizeof path)) return false;
	FIL f;
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;

	const uint32_t rowbytes = W * 3;		/* 960: a multiple of 4 already */
	uint8_t hdr[54] = { 'B', 'M' };
	put32(hdr + 2, 54 + rowbytes * H);
	put32(hdr + 10, 54);
	put32(hdr + 14, 40);
	put32(hdr + 18, W);
	put32(hdr + 22, H);			/* positive: rows bottom-up */
	hdr[26] = 1;				/* planes */
	hdr[28] = 24;				/* bits per pixel */
	put32(hdr + 34, rowbytes * H);
	put32(hdr + 38, 2835);			/* 72 dpi */
	put32(hdr + 42, 2835);
	UINT put;
	bool ok = f_write(&f, hdr, sizeof hdr, &put) == FR_OK && put == sizeof hdr;

	static uint16_t px[W];
	static uint8_t line[W * 3];
	for (int y = H - 1; ok && y >= 0; y--) {
		lcd_unblit(px, 0, (uint16_t)y, W, 1);
		for (int x = 0; x < W; x++) {		/* RGB565 to BMP's blue, green, red */
			uint16_t c = px[x];
			uint8_t r = (uint8_t)((c >> 11) & 31), g = (uint8_t)((c >> 5) & 63), b = (uint8_t)(c & 31);
			line[x * 3 + 0] = (uint8_t)((b << 3) | (b >> 2));
			line[x * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
			line[x * 3 + 2] = (uint8_t)((r << 3) | (r >> 2));
		}
		ok = f_write(&f, line, rowbytes, &put) == FR_OK && put == rowbytes;
	}
	f_close(&f);
	if (!ok) { f_unlink(path); return false; }
	if (saved) snprintf(saved, saved_size, "%s", path);
	return true;
}
