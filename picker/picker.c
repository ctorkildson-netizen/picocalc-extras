/* picker.c -- see picker.h
 *
 * Screen (320 x 320, rows of 10 pixels):
 *   banner across the top (or a title line)
 *   the folder's path
 *   the list, 24 columns of the 8x10 font, with scroll marks   | preview
 *                                                               | name, size, date
 *   key help
 *   count of files                                    battery
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "drivers/lcd.h"
#include "drivers/font.h"
#include "drivers/keyboard.h"
#include "drivers/southbridge.h"
#include "drivers/fat32.h"
#include "picker.h"
#include "icons.h"

#define MAX_ENTRIES PICKER_MAX_FILES

typedef struct {
	char *name;
	bool dir;
	uint32_t size;
	uint16_t date, time;
} entry_t;

static entry_t ents[MAX_ENTRIES];
static int n_ents, n_dirs;		/* folders come first: ents[0 .. n_dirs-1] */
static bool truncated;

#define C_TEXT   0xFFFF
#define C_DIM    0x8410
#define C_TITLE  0x07E0
#define C_DIR    0x07FF
#define C_PATH   0xFFE0
#define C_PANEL  0x10A2			/* the preview square before anything is drawn */

/* layout, in 10-pixel rows; the list uses the 8x10 font */
static int list_top, list_rows;
#define LIST_COLS 24			/* 192 pixels */
#define MARK_COL  24			/* scroll marks */
#define PANEL_X   202
#define PANEL_SIZE 116
#define HELP_ROW  30
#define FOOT_ROW  31

static int cmp(const void *a, const void *b)
{
	const entry_t *x = a, *y = b;
	if (x->dir != y->dir) return x->dir ? -1 : 1;
	if (x->dir && strcmp(x->name, "..") == 0) return -1;
	if (y->dir && strcmp(y->name, "..") == 0) return 1;
	return strcasecmp(x->name, y->name);
}

static void free_list(void)
{
	for (int i = 0; i < n_ents; i++) free(ents[i].name);
	n_ents = n_dirs = 0;
}

/* returns false if the folder can't be opened */
static bool load(const char *dir, bool (*filter)(const char *))
{
	free_list();
	truncated = false;
	fat32_file_t d;
	if (fat32_open(&d, dir) != FAT32_OK) return false;
	if (strcmp(dir, "/") != 0) {
		ents[n_ents] = (entry_t){ strdup(".."), true, 0, 0, 0 };
		n_ents++;
	}
	fat32_entry_t e;
	for (;;) {
		if (fat32_dir_read(&d, &e) != FAT32_OK || !e.filename[0]) break;
		if (e.attr & (FAT32_ATTR_VOLUME_ID | FAT32_ATTR_HIDDEN | FAT32_ATTR_SYSTEM)) continue;
		if (e.filename[0] == '.') continue;	/* ".", "..", and macOS "._x" files */
		bool isdir = (e.attr & FAT32_ATTR_DIRECTORY) != 0;
		if (!isdir && filter && !filter(e.filename)) continue;
		if (n_ents == MAX_ENTRIES) { truncated = true; break; }
		char *name = strdup(e.filename);
		if (!name) { truncated = true; break; }
		ents[n_ents++] = (entry_t){ name, isdir, e.size, e.date, e.time };
	}
	fat32_close(&d);
	qsort(ents, n_ents, sizeof ents[0], cmp);
	for (n_dirs = 0; n_dirs < n_ents && ents[n_dirs].dir; n_dirs++) ;
	return true;
}

bool picker_load(const char *dir, bool (*filter)(const char *)) { return load(dir, filter); }

int picker_file_count(void) { return n_ents - n_dirs; }
const char *picker_file_name(int i) { return ents[n_dirs + i].name; }

int picker_file_index(const char *name)
{
	for (int i = n_dirs; i < n_ents; i++)
		if (strcmp(ents[i].name, name) == 0) return i - n_dirs;
	return -1;
}

/* text at a column, padded to width columns, in the current font */
static void put(int col, int row, int width, uint16_t fg, bool rev, const char *s)
{
	char b[80];
	if (width > (int)sizeof b - 1) width = sizeof b - 1;
	snprintf(b, sizeof b, "%-*.*s", width, width, s);
	lcd_set_reverse(rev);
	lcd_set_foreground(fg);
	lcd_putstr(col, row, b);
	lcd_set_reverse(false);
}

/* the end of a long string, so the interesting part shows */
static const char *tail(const char *s, int room)
{
	int n = strlen(s);
	return n <= room ? s : s + n - room;
}

static void draw_row(int first, int r, int sel)
{
	char b[64];
	int i = first + r;
	lcd_set_font(&font_8x10);
	if (i >= n_ents) {
		put(0, list_top + r, LIST_COLS, C_TEXT, false, "");
		return;
	}
	const entry_t *e = &ents[i];
	int room = LIST_COLS - 1 - (e->dir ? 1 : 0);
	if ((int)strlen(e->name) > room)
		snprintf(b, sizeof b, " %.*s~%s", room - 1, e->name, e->dir ? "/" : "");
	else
		snprintf(b, sizeof b, " %s%s", e->name, e->dir ? "/" : "");
	put(0, list_top + r, LIST_COLS, e->dir ? C_DIR : C_TEXT, i == sel, b);
}

static void draw_marks(int first)
{
	lcd_set_font(&font_8x10);
	lcd_set_foreground(C_DIM);
	lcd_putc(MARK_COL, list_top, first > 0 ? 0x81 : ' ');
	lcd_putc(MARK_COL, list_top + list_rows - 1, first + list_rows < n_ents ? 0x80 : ' ');
}

static void draw_list(int first, int sel)
{
	for (int r = 0; r < list_rows; r++) draw_row(first, r, sel);
	draw_marks(first);
}

static void draw_footer(const picker_style_t *st)
{
	char b[80];
	lcd_set_font(&font_5x10);
	int n = picker_file_count();
	snprintf(b, sizeof b, "%d %s%s", n, n == 1 ? st->noun : st->nouns,
	         truncated ? " (list cut short)" : "");
	put(0, FOOT_ROW, 44, C_DIM, false, b);
	snprintf(b, sizeof b, "Battery %u%%", sb_read_battery() & 0x7F);
	put(64 - 12, FOOT_ROW, 12, C_DIM, false, b);
}

static void fmt_size(char *b, size_t n, uint32_t size)
{
	if (size < 1024) snprintf(b, n, "%lu bytes", (unsigned long)size);
	else if (size < 1024 * 1024) snprintf(b, n, "%lu KB", (unsigned long)((size + 512) / 1024));
	else snprintf(b, n, "%lu.%lu MB", (unsigned long)(size / 1048576),
	              (unsigned long)(size % 1048576 * 10 / 1048576));
}

/* the panel's text rows, below the preview (5x10 font, 23 columns from col 40) */
#define PANEL_COL 40
#define PANEL_TEXT_ROW ((list_top * 10 + PANEL_SIZE) / 10 + 1)

static void clear_panel(void)
{
	lcd_solid_rectangle(C_PANEL, PANEL_X, list_top * 10, PANEL_SIZE, PANEL_SIZE);
	lcd_set_font(&font_5x10);
	for (int r = PANEL_TEXT_ROW; r < HELP_ROW; r++) put(PANEL_COL, r, 24, C_TEXT, false, "");
}

static void draw_panel_text(int sel)
{
	char b[64];
	lcd_set_font(&font_5x10);
	int r = PANEL_TEXT_ROW;
	for (; r < HELP_ROW; r++) put(PANEL_COL, r, 24, C_TEXT, false, "");
	r = PANEL_TEXT_ROW;
	if (sel >= n_ents) return;
	const entry_t *e = &ents[sel];
	/* the name, over up to three lines */
	const char *s = e->name;
	for (int k = 0; k < 3 && *s; k++, s += strlen(s) < 23 ? strlen(s) : 23)
		put(PANEL_COL, r++, 24, e->dir ? C_DIR : C_TEXT, false, s);
	if (e->dir) return;
	fmt_size(b, sizeof b, e->size);
	put(PANEL_COL, r++, 24, C_DIM, false, b);
	if (e->date) {
		snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", 1980 + (e->date >> 9),
		         (e->date >> 5) & 15, e->date & 31, e->time >> 11, (e->time >> 5) & 63);
		put(PANEL_COL, r++, 24, C_DIM, false, b);
	}
}

static void draw_frame(const picker_style_t *st, const char *dir)
{
	char b[80];
	lcd_clear_screen();
	int top_px;
	if (st->banner) {
		lcd_blit(st->banner, (320 - st->banner_w) / 2, 0, st->banner_w, st->banner_h);
		top_px = st->banner_h;
	} else {
		lcd_set_font(&font_8x10);
		put(0, 0, 40, C_TITLE, false, st->title ? st->title : "");
		top_px = 10;
	}
	int path_row = (top_px + 9) / 10;
	list_top = path_row + 1;
	list_rows = HELP_ROW - 1 - list_top;
	lcd_set_font(&font_5x10);
	snprintf(b, sizeof b, "%s", tail(dir, 64));
	put(0, path_row, 64, C_PATH, false, b);
	put(0, HELP_ROW, 64, C_DIM, false, "Up/Down: move  Enter: open  Bksp: up a folder  Esc: quit  ?: help");
}

static const char *const *help_extra;	/* the program's own keys */
static bool on_folder;

bool picker_on_folder(void) { return on_folder; }

/* the list's keys, in a box; any key puts the list back */
static void show_help(void)
{
	static const char *const lines[] = {
		"",
		"  Up / Down      move",
		"  PgUp / PgDn    a page at a time",
		"  Home / End     top / bottom",
		"  Enter          open",
		"  Backspace      up a folder",
		"  ? or h         this help",
		"  Esc            quit",
	};
	static const char *const tail[] = { "", "  (any key to go on)", "" };
	const char *all[24];
	int n = 0;
	for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++) all[n++] = lines[i];
	for (int i = 0; help_extra && help_extra[i] && n < 21; i++) all[n++] = help_extra[i];
	for (int i = 0; i < 3; i++) all[n++] = tail[i];
	int top = (32 - n) / 2;
	lcd_set_font(&font_8x10);
	lcd_solid_rectangle(0x0010, 16, top * 10 - 4, 288, n * 10 + 8);
	lcd_set_background(0x0010);
	for (int i = 0; i < n; i++) {
		lcd_set_foreground(i == n - 2 ? C_DIM : C_TEXT);
		lcd_putstr(2, top + i, all[i]);
	}
	lcd_set_background(0x0000);
	keyboard_get_key();
}

/* go up one level: "/a/b" -> "/a", remembering "b" */
static void up(char *dir, char *came_from)
{
	char *slash = strrchr(dir, '/');
	if (!slash) return;
	strcpy(came_from, slash + 1);
	if (slash == dir) dir[1] = 0;
	else *slash = 0;
}

/* wait up to ms for a key; true if one came */
static bool key_within(uint32_t ms)
{
	absolute_time_t until = make_timeout_time_ms(ms);
	while (!time_reached(until))
		if (keyboard_key_available()) return true;
	return keyboard_key_available();
}

int picker_run(const picker_style_t *st, char *dir, const char *start, char *path)
{
	help_extra = st->keys_help;
	char focus[FAT32_MAX_FILENAME_LEN + 1] = "";
	char full[PICKER_PATH];
	if (start) snprintf(focus, sizeof focus, "%s", start);
	for (;;) {
		if (!load(dir, st->filter)) {
			strcpy(dir, "/");
			if (!load(dir, st->filter)) {
				lcd_clear_screen();
				lcd_set_font(&font_8x10);
				put(0, 15, 40, 0xF800, false, "Can't read the SD card.  Esc to quit.");
				while ((uint8_t)keyboard_get_key() != KEY_ESC) ;
				return 0;
			}
		}
		draw_frame(st, dir);
		int sel = 0, first = 0;
		for (int i = 0; focus[0] && i < n_ents; i++)
			if (strcmp(ents[i].name, focus) == 0) { sel = i; break; }
		focus[0] = 0;
		if (sel >= list_rows) first = sel - list_rows / 2;
		if (first > n_ents - list_rows) first = n_ents - list_rows;
		if (first < 0) first = 0;
		draw_list(first, sel);
		draw_footer(st);
		if (n_ents == 0) {
			lcd_set_font(&font_8x10);
			put(0, list_top, LIST_COLS, C_DIM, false, " (nothing here)");
		}

		bool reload = false, panel = true;
		while (!reload) {
			/* the panel for the highlighted entry, when it changes */
			if (st->preview && panel) {
				panel = false;
				clear_panel();
				draw_panel_text(sel);
				if (sel < n_ents && ents[sel].dir)
					lcd_blit(folder_icon, PANEL_X + (PANEL_SIZE - FOLDER_W) / 2,
					         list_top * 10 + (PANEL_SIZE - FOLDER_H) / 2, FOLDER_W, FOLDER_H);
				if (sel < n_ents && !ents[sel].dir && !key_within(250)) {
					snprintf(full, sizeof full, "%s%s%s", dir, strcmp(dir, "/") ? "/" : "",
					         ents[sel].name);
					st->preview(full, PANEL_X, list_top * 10, PANEL_SIZE);
				}
			}
			int old_sel = sel, old_first = first;
			uint8_t k = (uint8_t)keyboard_get_key();
			if (k && st->keys && strchr(st->keys, k)) {
				bool real = n_ents && strcmp(ents[sel].name, "..") != 0;
				on_folder = !real || ents[sel].dir;
				if (real) snprintf(path, PICKER_PATH, "%s%s%s", dir,
				                   strcmp(dir, "/") ? "/" : "", ents[sel].name);
				else snprintf(path, PICKER_PATH, "%s", dir);
				return k;
			}
			switch (k) {
			case KEY_UP:	if (sel > 0) sel--; break;
			case KEY_DOWN:	if (sel < n_ents - 1) sel++; break;
			case KEY_PAGE_UP:	sel = sel > list_rows ? sel - list_rows : 0; break;
			case KEY_PAGE_DOWN:	sel = sel + list_rows < n_ents ? sel + list_rows : n_ents - 1; break;
			case KEY_HOME:	sel = 0; break;
			case KEY_END:	sel = n_ents ? n_ents - 1 : 0; break;
			case KEY_ESC:
				return 0;
			case '?': case 'h': case 'H':
				show_help();
				snprintf(focus, sizeof focus, "%s", n_ents ? ents[sel].name : "");
				reload = true;		/* redraw it all */
				break;
			case KEY_BACKSPACE: case KEY_LEFT:
				if (strcmp(dir, "/") != 0) { up(dir, focus); reload = true; }
				break;
			case KEY_RETURN: case KEY_ENTER: case KEY_RIGHT:
				if (n_ents == 0) break;
				if (ents[sel].dir) {
					if (strcmp(ents[sel].name, "..") == 0) up(dir, focus);
					else if (strlen(dir) + strlen(ents[sel].name) + 2 < PICKER_PATH) {
						if (strcmp(dir, "/") != 0) strcat(dir, "/");
						strcat(dir, ents[sel].name);
					}
					reload = true;
				} else if (k != KEY_RIGHT) {
					snprintf(path, PICKER_PATH, "%s%s%s", dir,
					         strcmp(dir, "/") ? "/" : "", ents[sel].name);
					return PICKER_CHOSEN;
				}
				break;
			}
			if (reload || n_ents == 0) continue;
			if (sel < first) first = sel;
			if (sel >= first + list_rows) first = sel - list_rows + 1;
			if (first != old_first) {
				draw_list(first, sel);
			} else if (sel != old_sel) {
				draw_row(first, old_sel - first, sel);
				draw_row(first, sel - first, sel);
			}
			if (sel != old_sel) panel = true;
			draw_footer(st);
		}
	}
}
