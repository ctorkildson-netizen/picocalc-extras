/* btpair -- pairs a Bluetooth keyboard with the PicoCalc (Pico 2 W), step
 * by step, and lets you try it out. Programs built with the shared
 * Bluetooth code (bluetooth/) then connect to it by themselves.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Chris Torkildson. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "pico/stdlib.h"
#include "drivers/lcd.h"
#include "drivers/font.h"
#include "drivers/keyboard.h"
#include "drivers/southbridge.h"
#include "drivers/fat32.h"
#include "picocalc_shell.h"
#include "btkbd.h"

#define VERSION "1.2"
#define COLS 40
#define SCREEN_ROWS 32
#define C_TEXT 0xC618		/* light grey */
#define C_TITLE 0xFFE0		/* yellow */
#define C_DIM 0x8410		/* grey */
#define C_KEY 0xFFFF		/* white: keys to press */
#define C_CODE 0x07FF		/* cyan, and big: the pairing code */
#define C_GOOD 0x07E0		/* green */

/* ------------------------------------------------------------ the screen */

static char shown[SCREEN_ROWS][COLS + 1];	/* what's on it, for F5 and for drawing only what changed */
static uint16_t shown_colour[SCREEN_ROWS];
static char want[SCREEN_ROWS][COLS + 1];
static uint16_t want_colour[SCREEN_ROWS];

static void line(int row, uint16_t colour, const char *s)
{
	if (row < 0 || row >= SCREEN_ROWS) return;
	snprintf(want[row], COLS + 1, "%-40.40s", s);
	want_colour[row] = colour;
}

static void clear(void)
{
	for (int r = 0; r < SCREEN_ROWS; r++) line(r, C_TEXT, "");
}

static void show(void)
{
	for (int r = 0; r < SCREEN_ROWS; r++) {
		if (!strcmp(shown[r], want[r]) && shown_colour[r] == want_colour[r]) continue;
		lcd_set_foreground(want_colour[r]);
		lcd_putstr(0, (uint8_t)r, want[r]);
		memcpy(shown[r], want[r], COLS + 1);
		shown_colour[r] = want_colour[r];
	}
}

/* text wrapped onto rows from row; returns the row after it */
static int para(int row, uint16_t colour, const char *s)
{
	char buf[COLS + 1];
	while (*s && row < SCREEN_ROWS) {
		int n = (int)strlen(s);
		if (n > COLS) {
			n = COLS;
			while (n > 0 && s[n] != ' ') n--;
			if (n == 0) n = COLS;
		}
		memcpy(buf, s, n);
		buf[n] = 0;
		line(row++, colour, buf);
		s += n;
		while (*s == ' ') s++;
	}
	return row;
}

/* F5: the screen as text, in /screenshots/btpair-NNN.txt, and the Bluetooth
   log since starting in btpair-NNN.pklg (Wireshark opens it) */
static char note[COLS + 1];
static void screenshot(void)
{
	fat32_file_t f;
	char path[48];
	if (fat32_dir_create(&f, "/screenshots") == FAT32_OK) fat32_close(&f);
	for (int n = 1; n < 1000; n++) {
		snprintf(path, sizeof path, "/screenshots/btpair-%03d.txt", n);
		if (fat32_open(&f, path) == FAT32_OK) { fat32_close(&f); continue; }
		if (fat32_create(&f, path) != FAT32_OK) break;
		for (int r = 0; r < SCREEN_ROWS; r++) {
			char buf[COLS + 3];
			int l = snprintf(buf, sizeof buf, "%s", shown[r]);
			while (l > 0 && buf[l - 1] == ' ') l--;
			buf[l++] = '\r', buf[l++] = '\n';
			size_t put;
			fat32_write(&f, buf, (size_t)l, &put);
		}
		fat32_close(&f);
		/* and the Bluetooth log beside it, for Wireshark */
		size_t len;
		uint32_t lost;
		const uint8_t *log = btkbd_log(&len, &lost);
		if (!log || !len) { snprintf(note, sizeof note, "Saved %s", path + 13); return; }
		size_t at = strlen(path) - 4;
		snprintf(path + at, sizeof path - at, ".pklg");
		bool ok = fat32_create(&f, path) == FAT32_OK;
		for (size_t done = 0; ok && done < len; ) {
			size_t n = len - done > 4096 ? 4096 : len - done, put = 0;
			ok = fat32_write(&f, log + done, n, &put) == FAT32_OK && put == n;
			done += n;
		}
		if (ok) fat32_close(&f);
		path[at] = 0;
		if (ok) snprintf(note, sizeof note, "Saved %s .txt .pklg %uK%s", path + 13, (unsigned)((len + 1023) / 1024), lost ? " FULL" : "");
		else snprintf(note, sizeof note, "Saved %s.txt; the log failed", path + 13);
		return;
	}
	snprintf(note, sizeof note, "Couldn't save the screen");
}

/* ------------------------------------------------------------ trying it out */

#define TRY_ROWS 6
static char tried[TRY_ROWS][COLS + 1];
static int t_row, t_col;
static bool t_ctrl, t_shift;

static void try_newline(void)
{
	if (t_row < TRY_ROWS - 1) t_row++;
	else memmove(tried[0], tried[1], sizeof tried - sizeof tried[0]), memset(tried[TRY_ROWS - 1], 0, COLS + 1);
	t_col = 0;
}

static void try_put(const char *s)
{
	for (; *s; s++) {
		if (t_col >= COLS) try_newline();
		tried[t_row][t_col++] = *s;
		tried[t_row][t_col] = 0;
	}
}

static const char *key_name(uint8_t c)
{
	static char b[6];
	switch (c) {
	case KEY_ESC: return "Esc";
	case KEY_UP: return "Up";
	case KEY_DOWN: return "Down";
	case KEY_LEFT: return "Left";
	case KEY_RIGHT: return "Right";
	case KEY_HOME: return "Home";
	case KEY_END: return "End";
	case KEY_PAGE_UP: return "PgUp";
	case KEY_PAGE_DOWN: return "PgDn";
	case KEY_DEL: return "Del";
	case KEY_INSERT: return "Ins";
	case KEY_TAB: return "Tab";
	case KEY_CAPS_LOCK: return "Caps";
	case KEY_BREAK: return "Break";
	case KEY_F10: return "F10";
	}
	if (c >= KEY_F1 && c <= KEY_F9) { snprintf(b, sizeof b, "F%d", c - KEY_F1 + 1); return b; }
	snprintf(b, sizeof b, "%02X", c);
	return b;
}

static void tried_key(uint16_t e)
{
	uint8_t st = e >> 8, c = e & 0xFF;
	bool down = st == KEY_STATE_PRESSED;
	if (c == KEY_MOD_CTRL) { t_ctrl = down; return; }
	if (c == KEY_MOD_SHL || c == KEY_MOD_SHR) { t_shift = down; return; }
	if (c == KEY_MOD_ALT || !down) return;
	if (c == KEY_ENTER) { try_newline(); return; }
	if (c == KEY_BACKSPACE) { if (t_col) tried[t_row][--t_col] = 0; return; }
	char b[12];
	if (c >= 'a' && c <= 'z') {
		if (t_ctrl) snprintf(b, sizeof b, "^%c", c - 32);
		else snprintf(b, sizeof b, "%c", t_shift ? c - 32 : c);
	} else if (c >= 0x20 && c < 0x7F) {
		snprintf(b, sizeof b, "%c", c);
	} else {
		snprintf(b, sizeof b, "<%s>", key_name(c));
	}
	try_put(b);
}

/* ------------------------------------------------------------ the pages */

enum { HOME, PREPARE, SEARCH, PAIRING } page = HOME;
static char message[80];		/* a line about what just happened */
static uint16_t message_colour = C_GOOD;
static char chosen[40];			/* the keyboard being paired */
static bool details;

static void say(uint16_t colour, const char *m)
{
	snprintf(message, sizeof message, "%s", m);
	message_colour = colour;
}

static void keys(int row, const char *const *k)
{
	for (; *k; k++) line(row++, C_KEY, *k);
}

static void draw(void)
{
	clear();
	char b[COLS + 1];
	line(0, C_TITLE, "Bluetooth keyboard        btpair " VERSION);
	btkbd_state_t st = btkbd_state();
	const char *name = btkbd_name();

	if (page == HOME) {
		if (st == BTKBD_OFF) {
			para(2, C_TEXT, "Bluetooth didn't start. This needs a PicoCalc with a Pico 2 W (the one with the radio).");
		} else if (!name[0]) {
			line(2, C_TEXT, "No Bluetooth keyboard is paired yet.");
			para(4, C_TEXT, "Pair one here once, and programs that know about Bluetooth keyboards will connect to it by themselves.");
		} else {
			snprintf(b, sizeof b, "Keyboard: %.30s", name);
			line(2, C_TEXT, b);
			if (st == BTKBD_CONNECTED) {
				line(3, C_GOOD, "Connected");
				line(5, C_TEXT, "Type on it to try it out:");
				for (int r = 0; r < TRY_ROWS; r++) line(6 + r, C_KEY, tried[r]);
				unsigned pr; int px, py, pw, pb;
				btkbd_pointer(&pr, &px, &py, &pw, &pb);
				if (pr) {
					snprintf(b, sizeof b, "Trackpad: %u reports", pr);
					line(7 + TRY_ROWS, C_TEXT, b);
					snprintf(b, sizeof b, "  x %d  y %d  wheel %d  buttons %d%d%d", px, py, pw, pb & 1, (pb >> 1) & 1, (pb >> 2) & 1);
					line(8 + TRY_ROWS, C_KEY, b);
				} else {
					line(7 + TRY_ROWS, C_DIM, "Trackpad: nothing from it yet");
				}
			} else if (st == BTKBD_STARTING) {
				line(3, C_DIM, "Starting Bluetooth...");
			} else {
				line(3, C_DIM, "Not connected");
				para(5, C_TEXT, "Press a key on the keyboard to wake it up: it connects in a few seconds. Check that it's switched on, and on the right channel if it has more than one.");
			}
		}
		static const char *const k_paired[] = {
			"  P    pair a different keyboard",
			"  F    forget this keyboard",
			"  Esc  finished: back to the shell", NULL };
		static const char *const k_none[] = {
			"  P    pair a keyboard",
			"  Esc  back to the shell", NULL };
		if (st != BTKBD_OFF) keys(24, name[0] ? k_paired : k_none);
		else line(24, C_KEY, "  Esc  back to the shell");
	} else if (page == PREPARE) {
		line(2, C_TITLE, "Pairing, step 1 of 3");
		int r = para(4, C_TEXT, "Put the Bluetooth keyboard in pairing mode. On most keyboards you hold down its Bluetooth or connect key for a few seconds, until its light blinks quickly.");
		r = para(r + 1, C_DIM, "(A Logitech K380: hold one of the 1, 2, 3 keys. Its manual will say, if yours is different.)");
		line(r + 1, C_TEXT, "Then press Enter here to look for it.");
		static const char *const k[] = { "  Enter  look for keyboards", "  Esc    cancel", NULL };
		keys(24, k);
	} else if (page == SEARCH) {
		line(2, C_TITLE, "Pairing, step 2 of 3");
		int n = btkbd_found();
		if (st == BTKBD_SEARCHING) {
			line(4, C_TEXT, "Looking for keyboards (10 seconds)...");
		} else if (n) {
			line(4, C_TEXT, "Press the number of your keyboard:");
		} else {
			para(4, C_TEXT, "No keyboards found. Is its light blinking quickly? Put it in pairing mode again, then press S.");
		}
		for (int i = 0; i < n && i < 8; i++) {
			snprintf(b, sizeof b, "  %d  %.34s%s", i + 1, btkbd_found_name(i), btkbd_found_le(i) ? "  (BLE)" : "");
			line(7 + i, C_KEY, b);
		}
		static const char *const k[] = { "  1-8  pair with that keyboard", "  S    look again", "  Esc  cancel", NULL };
		keys(24, k);
	} else {	/* PAIRING */
		line(2, C_TITLE, "Pairing, step 3 of 3");
		snprintf(b, sizeof b, "Pairing with %.27s...", chosen);
		line(4, C_TEXT, b);
		const char *code = btkbd_code();
		if (code) {
			para(6, C_TEXT, "Type this number on the Bluetooth keyboard, then press Enter on the Bluetooth keyboard:");
			char spaced[COLS + 1] = "      ";
			for (const char *p = code; *p && strlen(spaced) < COLS - 2; p++) {
				size_t l = strlen(spaced);
				spaced[l] = *p, spaced[l + 1] = ' ', spaced[l + 2] = 0;
			}
			line(10, C_CODE, spaced);
		} else {
			line(6, C_DIM, "Connecting...");
		}
		line(24, C_KEY, "  Esc  cancel");
	}

	if (message[0]) para(19, message_colour, message);
	if (details) {
		if (note[0]) line(23, C_DIM, note);		/* (what F5 did) */
		line(28, C_DIM, btkbd_status());
		line(29, C_DIM, btkbd_diag());
		line(30, C_DIM, btkbd_trace_line(0));
		line(31, C_DIM, btkbd_trace_line(1));
		if (page != PAIRING)	/* (there, the code to type goes in the middle) */
			for (int i = 0; i < 10; i++) if (btkbd_smp(i)[0]) line(9 + i, C_DIM, btkbd_smp(i));
	} else {
		line(30, C_DIM, note[0] ? note : "  D  details   F5  save screen and log");
	}
}

/* what's happening now that wasn't before */
static void follow(void)
{
	btkbd_state_t st = btkbd_state();
	if (page == PAIRING) {
		bool ours = !strcmp(btkbd_name(), chosen);
		if (st == BTKBD_CONNECTED && ours) {
			page = HOME;
			say(C_GOOD, "Paired and connected. Programs that use Bluetooth keyboards will now find it.");
		} else if (st == BTKBD_WAITING && ours) {
			page = HOME;
			say(C_GOOD, "Paired. Press a key on the keyboard to connect.");
		} else if (st == BTKBD_IDLE || st == BTKBD_WAITING) {
			page = SEARCH;
			say(0xFBE0, "That didn't work. Put the keyboard in pairing mode again, press S, and choose it again.");
		}
	}
}

/* the shell's clock, for dating the files btpair writes (screenshots, the
   pairing): false if the shell hasn't set it */
static bool shell_clock(uint16_t *date, uint16_t *time)
{
#ifdef PICOCALC_SHELL_HAS_TIME
	int64_t ms;
	int offset;
	if (!picocalc_shell_time(&ms, &offset)) return false;
	time_t t = (time_t)(ms / 1000) + (time_t)offset * 60;
	struct tm tm;
	gmtime_r(&t, &tm);
	if (tm.tm_year + 1900 < 1980 || tm.tm_year + 1900 > 2107) return false;
	fat32_pack_time(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, date, time);
	return true;
#else
	(void)date;
	(void)time;
	return false;
#endif
}

int main(void)
{
	sb_init();
	lcd_init();
	lcd_set_font(&font_8x10);
	lcd_enable_cursor(false);
	lcd_set_background(0x0000);
	lcd_clear_screen();
	for (int r = 0; r < SCREEN_ROWS; r++) memset(shown[r], ' ', COLS), shown[r][COLS] = 0;
	fat32_init();
	fat32_set_time_source(shell_clock);
	keyboard_init();
	btkbd_init();
	btkbd_merge(false);	/* the PicoCalc's keys run this; the Bluetooth keyboard's are being tried out */

	absolute_time_t next = get_absolute_time();
	for (;;) {
		btkbd_poll();
		uint16_t e;
		while ((e = btkbd_read())) tried_key(e);

		uint16_t k = sb_read_keyboard();
		if ((k >> 8) == KEY_STATE_PRESSED) {
			uint8_t c = k & 0xFF;
			if (c >= 'A' && c <= 'Z') c += 32;
			note[0] = 0;
			if (c == KEY_F5) {
				draw(); show(); screenshot();
			} else if (c == 'd') {
				details = !details;
			} else if (page == HOME) {
				if (c == KEY_ESC) {
					lcd_clear_screen();
					if (!picocalc_shell_exit()) { lcd_putstr(0, 15, "No SD card: switch off to leave."); for (;;) tight_loop_contents(); }
				} else if (c == 'p' && btkbd_state() != BTKBD_OFF) {
					page = PREPARE;
					message[0] = 0;
				} else if (c == 'f' && btkbd_name()[0]) {
					btkbd_forget();
					say(C_GOOD, "Forgotten. (The keyboard may still remember the PicoCalc: that does no harm.)");
				}
			} else if (page == PREPARE) {
				if (c == KEY_ESC) page = HOME;
				else if (c == KEY_ENTER || c == KEY_RETURN) { page = SEARCH; message[0] = 0; btkbd_search(); }
			} else if (page == SEARCH) {
				if (c == KEY_ESC) { page = HOME; message[0] = 0; }
				else if (c == 's') { message[0] = 0; btkbd_search(); }
				else if (c >= '1' && c <= '8' && c - '1' < btkbd_found()) {
					snprintf(chosen, sizeof chosen, "%s", btkbd_found_name(c - '1'));
					message[0] = 0;
					page = PAIRING;
					btkbd_pair(c - '1');
				}
			} else if (page == PAIRING) {
				if (c == KEY_ESC) { page = SEARCH; say(0xFBE0, "Cancelled."); }
			}
		}
		follow();
		if (time_reached(next)) {
			draw();
			show();
			next = make_timeout_time_ms(100);
		}
		sleep_ms(2);
	}
}
