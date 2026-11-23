/* lcdshot.h -- a screenshot of the PicoCalc's LCD as it is, for programs on
 * Blair Leduc's picocalc-text-starter drivers (v0.17 or later). See
 * README.md ("Screenshots for any program").
 *
 *     if (key == KEY_F10)                       // Shift+F5 on the PicoCalc
 *         lcd_screenshot("myprog", saved, sizeof saved);
 *
 * Writes a 320x320 BMP to /screenshots (made if need be): myprog-<date and
 * time>.bmp when the shell's clock is set, else myprog-001.bmp and on.
 * true, with the file's path in saved (if given), when it's written.
 *
 * Call it from the program's own loop, not from an interrupt: it reads the
 * LCD and writes the card. A program that lets the text driver scroll the
 * screen in hardware should know that the text starter's lcd_unblit() reads
 * the frame memory without the scroll offset, so after scrolling the picture
 * comes out rotated; programs that place text themselves aren't affected.
 */
#ifndef LCDSHOT_H
#define LCDSHOT_H

#include <stdbool.h>
#include <stddef.h>

bool lcd_screenshot(const char *app, char *saved, size_t saved_size);

#endif
