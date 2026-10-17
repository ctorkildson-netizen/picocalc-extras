/* pdcshot.c -- Shift+F5 (F10) screenshots for PDCurses programs on the
 * PicoCalc, as a 320x320 24-bit BMP in /screenshots on the SD card.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Chris Torkildson.
 *
 * Needs: the PicoCalc PDCurses backend's pdcpicocalc.h, Blair Leduc's
 * picocalc-text-starter drivers (font, fat32), and picocalc_clock.h (for
 * date-stamped names). See README.md for the two-line keyboard hook.
 */
#include <time.h>
#include "picocalc_clock.h"
#include <stdio.h>
#include <string.h>

#include "pdcpicocalc.h"
#include <drivers/font.h>
#include <drivers/fat32.h>

/* Each program sets PICOCALC_APP_NAME in its CMakeLists.txt, so files
   from different programs never collide: /screenshots/empire-001.bmp,
   /screenshots/advent-001.bmp, ... */
#ifndef PICOCALC_APP_NAME
#define PICOCALC_APP_NAME "screen"
#endif

/* PicoCalc port: F10 screenshot.

   The LCD can't be read back, but curses keeps its own copy of what's on
   the screen (curscr: every character plus its attributes). This renders
   that copy with the same font and the same colour mapping the LCD driver
   uses into a 320x320 24-bit BMP in /screenshots on the SD card -- so the image matches the LCD pixel for pixel
   (except the cursor, which isn't drawn).

   Called from PDC_get_key() in pdckbd.c, i.e. from the program's own
   context, never from the keyboard timer interrupt. */

extern const font_t *font;              /* the LCD driver's current font */

static void _rgb(short c, uint8_t *bgr)
{
    uint8_t r = 255, g = 255, b = 255;  /* COLOR_WHITE and anything unknown */

    switch (c)
    {
    case COLOR_BLACK:   r = 0;   g = 0;   b = 0;   break;
    case COLOR_RED:     r = 255; g = 0;   b = 0;   break;
    case COLOR_GREEN:   r = 0;   g = 255; b = 0;   break;
    case COLOR_YELLOW:  r = 255; g = 255; b = 0;   break;
    case COLOR_BLUE:    r = 0;   g = 0;   b = 255; break;
    case COLOR_MAGENTA: r = 255; g = 0;   b = 255; break;
    case COLOR_CYAN:    r = 0;   g = 255; b = 255; break;
    }
    bgr[0] = b; bgr[1] = g; bgr[2] = r; /* BMP pixel order */
}

static void _put32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

/* Is glyph pixel (px, py) of character c lit?  Mirrors lcd_putc(). */
static bool _lit(uint8_t c, int px, int py, bool bold, bool underline)
{
    uint8_t row = font->glyphs[c * GLYPH_HEIGHT + py];

    if (py == GLYPH_HEIGHT - 1 && underline)
        return true;

    if (font->width == 8)
    {
        uint8_t mask = 0x80 >> px;
        if (row & mask)
            return true;
        /* bold: also light a pixel whose left neighbour is lit (not on the underline row) */
        return bold && py < GLYPH_HEIGHT - 1 && px > 0 && (row & (mask << 1));
    }
    return (row & (0x10 >> px)) != 0;
}

bool PDC_picocalc_screenshot(void)
{
    static int next = 1;
    char name[64];
    FILE *f = NULL;

    if (!SP || !curscr)
        return false;

    /* make sure /screenshots exists (FILE_EXISTS is fine) */
    {
        fat32_file_t dir;
        fat32_error_t err = fat32_dir_create(&dir, "/screenshots");
        if (err == FAT32_OK)
            fat32_close(&dir);
        else if (err != FAT32_ERROR_FILE_EXISTS)
            return false;
    }

    /* name it by date and time when the shared clock is set (see
       pcclock.c), e.g. empire-2026-10-17_133045.bmp; otherwise the
       next unused number, empire-001.bmp */
    time_t now = time(NULL);
    if (now >= PICOCALC_CLOCK_VALID_AFTER)
    {
        struct tm lt;
        char stamp[24];
        localtime_r(&now, &lt);
        strftime(stamp, sizeof stamp, "%Y-%m-%d_%H%M%S", &lt);
        snprintf(name, sizeof name, "/screenshots/%s-%s.bmp", PICOCALC_APP_NAME, stamp);
        /* two in the same second: -2, -3, ... rather than overwrite */
        for (int n = 2; n < 100; n++)
        {
            FILE *t = fopen(name, "rb");
            if (!t)
                break;
            fclose(t);
            snprintf(name, sizeof name, "/screenshots/%s-%s-%d.bmp", PICOCALC_APP_NAME, stamp, n);
        }
    }
    else
    {
        for (; next < 1000; next++)
        {
            snprintf(name, sizeof name, "/screenshots/%s-%03d.bmp",
                     PICOCALC_APP_NAME, next);
            FILE *t = fopen(name, "rb");
            if (!t)
                break;
            fclose(t);
        }
        if (next >= 1000)
            return false;
        next++;
    }
    if (!(f = fopen(name, "wb")))
        return false;

    const int cw = font->width, ch = GLYPH_HEIGHT;
    const int cols = curscr->_maxx, rows = curscr->_maxy;
    const int W = 320, H = 320;
    const uint32_t rowbytes = W * 3;            /* 960: already a multiple of 4 */
    const uint32_t size = 54 + rowbytes * H;

    uint8_t hdr[54] = { 'B', 'M' };
    _put32(hdr + 2, size);
    _put32(hdr + 10, 54);
    _put32(hdr + 14, 40);
    _put32(hdr + 18, W);
    _put32(hdr + 22, H);                        /* positive: bottom-up rows */
    hdr[26] = 1;                                /* planes */
    hdr[28] = 24;                               /* bits per pixel */
    _put32(hdr + 34, rowbytes * H);
    _put32(hdr + 38, 2835);                     /* 72 dpi */
    _put32(hdr + 42, 2835);
    bool ok = fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr;

    static uint8_t line[320 * 3];
    for (int y = H - 1; ok && y >= 0; y--)
    {
        int r = y / ch, py = y % ch;
        for (int x = 0; x < W; x++)
        {
            int c = x / cw, px = x % cw;
            uint8_t *out = &line[x * 3];

            if (r >= rows || c >= cols)
            {
                out[0] = out[1] = out[2] = 0;
                continue;
            }

            chtype cell = curscr->_y[r][c];
            chtype attr = cell & A_ATTRIBUTES;
            short fg = COLOR_WHITE, bg = COLOR_BLACK;
            short pair = PAIR_NUMBER(attr);
            if (pair && SP->color_started)
                pair_content(pair, &fg, &bg);
            if (attr & A_REVERSE)
            {
                short t = fg; fg = bg; bg = t;
            }

            bool lit = _lit(cell & 0x7F, px, py,
                            (attr & A_BOLD) != 0, (attr & A_UNDERLINE) != 0);
            _rgb(lit ? fg : bg, out);
        }
        ok = fwrite(line, 1, rowbytes, f) == rowbytes;
    }

    fclose(f);
    return ok;
}
