/* btpair -- pairs a Bluetooth keyboard and mouse with the PicoCalc (Pico 2
 * W), step by step, and lets you try them out. Programs built with the shared
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
#include "drivers/filesystem.h"
#include "picocalc_shell.h"
#include "btkbd.h"

#define VERSION "2.1"
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
	FIL f;
	char path[48];
	f_mkdir("/screenshots");			/* (FR_EXIST if it's there) */
	for (int n = 1; n < 1000; n++) {
		snprintf(path, sizeof path, "/screenshots/btpair-%03d.txt", n);
		FRESULT fr = f_open(&f, path, FA_WRITE | FA_CREATE_NEW);
		if (fr == FR_EXIST) continue;
		if (fr != FR_OK) break;
		for (int r = 0; r < SCREEN_ROWS; r++) {
			char buf[COLS + 3];
			int l = snprintf(buf, sizeof buf, "%s", shown[r]);
			while (l > 0 && buf[l - 1] == ' ') l--;
			buf[l++] = '\r', buf[l++] = '\n';
			UINT put;
			f_write(&f, buf, (UINT)l, &put);
		}
		f_close(&f);
		/* and the Bluetooth log beside it, for Wireshark */
		size_t len;
		uint32_t lost;
		const uint8_t *log = btkbd_log(&len, &lost);
		if (!log || !len) { snprintf(note, sizeof note, "Saved %s", path + 13); return; }
		size_t at = strlen(path) - 4;
		snprintf(path + at, sizeof path - at, ".pklg");
		bool ok = f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK;
		for (size_t done = 0; ok && done < len; ) {
			UINT n = (UINT)(len - done > 4096 ? 4096 : len - done), put = 0;
			ok = f_write(&f, log + done, n, &put) == FR_OK && put == n;
			done += n;
		}
		if (f_close(&f) != FR_OK) ok = false;
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

enum { HOME, PREPARE, SEARCH, PAIRING, FORGET } page = HOME;
static btkbd_device_t dev;		/* what's being paired: the keyboard or the mouse */
static char message[160];		/* a line or two about what just happened */
static uint16_t message_colour = C_GOOD;
static char chosen[40];			/* the one being paired */
static bool details;

static const char *dev_word(void) { return dev == BTKBD_MOUSE ? "mouse" : "keyboard"; }

static void say(uint16_t colour, const char *m)
{
	snprintf(message, sizeof message, "%s", m);
	message_colour = colour;
}

static void keys(int row, const char *const *k)
{
	for (; *k; k++) line(row++, C_KEY, *k);
}

static void forget(btkbd_device_t d)
{
	btkbd_forget_device(d);
	say(C_GOOD, d == BTKBD_MOUSE ? "Mouse forgotten. (It may still remember the PicoCalc: that does no harm.)"
	                             : "Keyboard forgotten. (It may still remember the PicoCalc: that does no harm.)");
}

/* "Keyboard: K380" and how it is, on two rows */
static void device_rows(int row, btkbd_device_t d, const char *what)
{
	char b[COLS + 1];
	const char *name = btkbd_device_name(d);
	btkbd_state_t st = btkbd_device_state(d);
	if (!name[0]) {
		snprintf(b, sizeof b, "%s: none paired", what);
		line(row, C_DIM, b);
		return;
	}
	snprintf(b, sizeof b, "%s: %.30s", what, name);
	line(row, C_TEXT, b);
	if (st == BTKBD_CONNECTED) line(row + 1, C_GOOD, "  Connected");
	else if (st == BTKBD_STARTING) line(row + 1, C_DIM, "  Starting Bluetooth...");
	else line(row + 1, C_DIM, d == BTKBD_MOUSE ? "  Not connected: move it or click to wake it"
	                                           : "  Not connected: press a key to wake it");
}

static void draw(void)
{
	clear();
	char b[COLS + 1];
	line(0, C_TITLE, "Bluetooth keyboard, mouse   btpair " VERSION);
	btkbd_state_t st = btkbd_state();

	if (page == HOME || page == FORGET) {
		bool k = btkbd_device_name(BTKBD_KEYBOARD)[0], m = btkbd_device_name(BTKBD_MOUSE)[0];
		if (st == BTKBD_OFF) {
			para(2, C_TEXT, "Bluetooth didn't start. This needs a PicoCalc with a Pico 2 W (the one with the radio).");
		} else if (!k && !m) {
			line(2, C_TEXT, "No Bluetooth keyboard or mouse is paired.");
			para(4, C_TEXT, "Pair them here once, and programs that know about Bluetooth will connect to them by themselves.");
		} else {
			device_rows(2, BTKBD_KEYBOARD, "Keyboard");
			device_rows(4, BTKBD_MOUSE, "Mouse");
			if (st == BTKBD_CONNECTED) {
				line(7, C_TEXT, "Type on it to try it out:");
				for (int r = 0; r < TRY_ROWS; r++) line(8 + r, C_KEY, tried[r]);
			} else if (k && st != BTKBD_STARTING) {
				para(7, C_DIM, "Check that the keyboard is switched on, and on the right channel if it has more than one.");
			}
			unsigned pr; int px, py, pw, pb;
			btkbd_pointer(&pr, &px, &py, &pw, &pb);
			if (pr) {
				snprintf(b, sizeof b, "Pointer: %u reports", pr);
				line(15, C_TEXT, b);
				snprintf(b, sizeof b, "  x %d  y %d  wheel %d  buttons %d%d%d", px, py, pw, pb & 1, (pb >> 1) & 1, (pb >> 2) & 1);
				line(16, C_KEY, b);
			} else if (btkbd_device_state(BTKBD_MOUSE) == BTKBD_CONNECTED) {
				line(15, C_TEXT, "Move the mouse to try it out");
			}
		}
		if (page == FORGET) {
			static const char *const k_which[] = {
				"  K    forget the keyboard",
				"  M    forget the mouse",
				"  Esc  forget neither", NULL };
			keys(24, k_which);
		} else if (st != BTKBD_OFF) {
			static const char *const k_both[] = {
				"  K    pair a keyboard",
				"  M    pair a mouse",
				"  F    forget one",
				"  Esc  finished: back to the shell", NULL };
			static const char *const k_none[] = {
				"  K    pair a keyboard",
				"  M    pair a mouse",
				"  Esc  back to the shell", NULL };
			keys(24, k || m ? k_both : k_none);
		} else {
			line(24, C_KEY, "  Esc  back to the shell");
		}
	} else if (page == PREPARE) {
		snprintf(b, sizeof b, "Pairing a %s, step 1 of 3", dev_word());
		line(2, C_TITLE, b);
		int r;
		if (dev == BTKBD_MOUSE) {
			r = para(4, C_TEXT, "Put the Bluetooth mouse in pairing mode. On most mice you hold down a button underneath (or a channel button) for a few seconds, until its light blinks quickly.");
		} else {
			r = para(4, C_TEXT, "Put the Bluetooth keyboard in pairing mode. On most keyboards you hold down its Bluetooth or connect key for a few seconds, until its light blinks quickly.");
			r = para(r + 1, C_DIM, "(A Logitech K380: hold one of the 1, 2, 3 keys. Its manual will say, if yours is different.)");
		}
		line(r + 1, C_TEXT, "Then press Enter here to look for it.");
		static const char *const k[] = { "  Enter  look for it", "  Esc    cancel", NULL };
		keys(24, k);
	} else if (page == SEARCH) {
		snprintf(b, sizeof b, "Pairing a %s, step 2 of 3", dev_word());
		line(2, C_TITLE, b);
		int n = btkbd_found();
		if (st == BTKBD_SEARCHING) {
			line(4, C_TEXT, "Looking (10 seconds)...");
		} else if (n) {
			snprintf(b, sizeof b, "Press the number of your %s:", dev_word());
			line(4, C_TEXT, b);
		} else {
			snprintf(b, sizeof b, "Nothing found. Is the %s's light blinking quickly? Put it in pairing mode again, then press S.", dev_word());
			para(4, C_TEXT, b);
		}
		for (int i = 0; i < n && i < 8; i++) {
			int kind = btkbd_found_kind(i);
			const char *tag = kind == BTKBD_MOUSE ? (btkbd_found_le(i) ? "  (mouse, BLE)" : "  (mouse)")
			                : kind == BTKBD_KEYBOARD ? (btkbd_found_le(i) ? "  (keyboard, BLE)" : "  (keyboard)")
			                : btkbd_found_le(i) ? "  (BLE)" : "";
			snprintf(b, sizeof b, "  %d  %.*s%s", i + 1, 35 - (int)strlen(tag), btkbd_found_name(i), tag);
			line(7 + i, C_KEY, b);
		}
		static const char *const k[] = { "  1-8  pair with that one", "  S    look again", "  Esc  cancel", NULL };
		keys(24, k);
	} else {	/* PAIRING */
		snprintf(b, sizeof b, "Pairing a %s, step 3 of 3", dev_word());
		line(2, C_TITLE, b);
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
	btkbd_state_t st = btkbd_device_state(dev);
	if (page == PAIRING) {
		bool ours = !strcmp(btkbd_device_name(dev), chosen);
		if (st == BTKBD_CONNECTED && ours) {
			page = HOME;
			say(C_GOOD, dev == BTKBD_MOUSE ? "Paired and connected. Programs that use a Bluetooth mouse will now find it."
			                               : "Paired and connected. Programs that use Bluetooth keyboards will now find it.");
		} else if (st == BTKBD_WAITING && ours) {
			page = HOME;
			say(C_GOOD, dev == BTKBD_MOUSE ? "Paired. Move the mouse or click to connect." : "Paired. Press a key on the keyboard to connect.");
		} else if (st == BTKBD_IDLE || st == BTKBD_WAITING || st == BTKBD_CONNECTED) {
			page = SEARCH;
			snprintf(message, sizeof message, "That didn't work. Put the %s in pairing mode again, press S, and choose it again.", dev_word());
			message_colour = 0xFBE0;
		}
	}
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
	fs_init();				/* (files are dated by the shell's clock) */
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
				} else if ((c == 'k' || c == 'p' || c == 'm') && btkbd_state() != BTKBD_OFF) {
					dev = c == 'm' ? BTKBD_MOUSE : BTKBD_KEYBOARD;
					page = PREPARE;
					message[0] = 0;
				} else if (c == 'f') {
					bool k = btkbd_device_name(BTKBD_KEYBOARD)[0], m = btkbd_device_name(BTKBD_MOUSE)[0];
					if (k && m) page = FORGET;
					else if (k || m) forget(k ? BTKBD_KEYBOARD : BTKBD_MOUSE);
				}
			} else if (page == FORGET) {
				if (c == 'k') forget(BTKBD_KEYBOARD);
				else if (c == 'm') forget(BTKBD_MOUSE);
				page = HOME;
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
					btkbd_pair_as(c - '1', dev);
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
