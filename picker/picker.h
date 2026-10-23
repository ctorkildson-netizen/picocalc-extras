/* picker.h -- a file picker for the PicoCalc: lists a folder (sub-folders
 * first, then the files a filter accepts), scrolls, opens folders, and
 * returns the file chosen.  Optional banner across the top, and an optional
 * preview panel on the right that the program draws into.  Uses Blair
 * Leduc's text-starter drivers.
 *
 * Keys: Up/Down, PgUp/PgDn, Home/End move; Enter opens; Backspace or Left
 * goes up a folder; Esc gives up.
 */
#ifndef PICKER_H
#define PICKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PICKER_PATH 260
#define PICKER_MAX_FILES 1024	/* entries in a folder's list, folders included */

typedef struct {
	const char *title;		/* shown when there is no banner */
	const uint16_t *banner;		/* RGB565, banner_w x banner_h, or NULL */
	int banner_w, banner_h;
	bool (*filter)(const char *name);	/* files to list (folders always are) */
	const char *noun, *nouns;	/* "picture", "pictures" */
	/* Called when the cursor rests on a file: draw a preview of path in
	   the square at (x, y), size x size pixels.  Should give up quickly
	   if a key is pressed (keyboard_key_available()).  May be NULL. */
	void (*preview)(const char *path, int x, int y, int size);
	/* Other keys the program wants from the list, such as "sS", and a
	   line for each in the help box (NULL-terminated).  May be NULL. */
	const char *keys;
	const char *const *keys_help;
} picker_style_t;

/* Lists dir (an absolute path, updated as the user moves around) and waits.
   Returns PICKER_CHOSEN with the chosen file's full path in path, 0 on Esc,
   or one of style->keys, with the highlighted entry's full path in path
   (dir itself for ".." or an empty folder); picker_on_folder() then says
   whether that is a folder.  start names an entry to put the cursor on
   first (or NULL). */
#define PICKER_CHOSEN 1
int picker_run(const picker_style_t *style, char *dir, const char *start, char *path);
bool picker_on_folder(void);

/* Reads dir's list without showing it (false if it can't be opened), so
   the picker_file_ calls work before picker_run has been used. */
bool picker_load(const char *dir, bool (*filter)(const char *name));

/* The files (not folders) of the folder last shown, in the order shown:
   for "next" and "previous" in a viewer. */
int picker_file_count(void);
const char *picker_file_name(int i);
int picker_file_index(const char *name);	/* -1 if not there */

#endif
