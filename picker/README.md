# picker

A file picker for PicoCalc programs built on Blair Leduc's
[picocalc-text-starter](https://github.com/BlairLeduc/picocalc-text-starter)
drivers. It came out of the [picture viewer](https://github.com/ctorkildson-netizen/picocalc-viewer).

It lists a folder on the SD card (folders first, then the files your filter
accepts, in name order), scrolls, moves in and out of folders, and returns
the file chosen. You can give it:

- a banner across the top (any RGB565 image), or just a title;
- a filter, so only your program's files are listed;
- a preview panel on the right, which it calls your code to draw into when
  the cursor rests on a file (the viewer draws a thumbnail there);
- extra keys of your own, each with a line in its `?` help box (the viewer
  uses `s` for a slide show of the folder).

Keys: Up/Down, PgUp/PgDn and Home/End move; Enter opens (Right opens
folders too);
Backspace or Left goes up a folder; `?` shows the keys; Esc gives up.

```c
#include "picker.h"

static bool is_text(const char *name) { /* true for .txt, say */ }

static const picker_style_t style = {
	.title = "Documents", .filter = is_text,
	.noun = "document", .nouns = "documents",
};

char dir[PICKER_PATH] = "/", path[PICKER_PATH];
if (picker_run(&style, dir, NULL, path) == PICKER_CHOSEN) {
	/* path is the file's full path; dir is the folder it's in */
}
```

After `picker_run()`, `picker_file_count()`, `picker_file_name()` and
`picker_file_index()` give the files of the folder last shown, in order, for
a program's own "next" and "previous". `picker_load()` reads a folder's list
without showing it. See [picker.h](picker.h) for the details.

A folder lists up to 1024 entries. Names starting with a dot (macOS `._`
files, for one) and hidden or system files are left out.

## Building

With the Pico SDK and CMake:

```cmake
include(path/to/picocalc-extras/picker/picker.cmake)
target_link_libraries(myprogram picocalc_picker ...)
```

The program provides the text-starter drivers itself, with its include path
set so that `drivers/lcd.h` and the rest are found.

`icons.h` (the folder picture) is made by `make_icons.py` with Python and
Pillow; it only needs remaking to change the icon.
