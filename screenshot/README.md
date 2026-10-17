# Screenshots for PDCurses on the PicoCalc

`pdcshot.c` adds **Shift+F5 screenshots** to a PDCurses program using the
PicoCalc backend built on Blair Leduc's
[picocalc-text-starter](https://github.com/BlairLeduc/picocalc-text-starter)
drivers (as in picocalc-rogue, and the Empire and Adventure ports).

## What it does

- The LCD can't be read back, so it redraws curses' own copy of the screen
  (`curscr`: every character and its attributes) using the LCD driver's
  current font and the same colour mapping, into a 320x320 24-bit BMP.
  The result matches the screen pixel for pixel, except that the cursor
  isn't drawn.
- Files go to `/screenshots` on the SD card, which is created if needed.
- Names come from a `PICOCALC_APP_NAME` define, so programs don't
  overwrite each other's files:
  - when the shared clock is set (see [`picocalc_clock.h`](../picocalc_clock.h)),
    `empire-2026-10-17_133045.bmp`, with `-2`, `-3` ... added for more
    than one in the same second;
  - otherwise the next free number: `empire-001.bmp`.
- A beep confirms the file was written.

## Adding it to a program

1. Add `pdcshot.c` to the program's sources next to the backend's
   `pdc*.c` files, and put `picocalc_clock.h` on the include path (and
   link `pico_aon_timer`).
2. Give the program a name for its files, in its CMakeLists.txt:

   ```cmake
   target_compile_definitions(myprogram PRIVATE PICOCALC_APP_NAME=\"myprog\")
   ```

3. Hook F10 in the backend's `pdckbd.c`. The keyboard BIOS turns
   Shift+F5 into F10. This version swallows it in `PDC_check_key()`, so
   it works for programs that poll with `nodelay()`/`timeout()` as well
   as ones that block, and it always runs in the program's own context,
   never in the keyboard timer interrupt:

   ```c
   bool PDC_picocalc_screenshot(void);   /* pdcshot.c */

   bool PDC_check_key(void)
   {
       while (rx_head != rx_tail && !flush && rx_buffer[rx_tail] == KEY_F(10))
       {
           rx_tail = (rx_tail + 1) & (KBD_BUFFER_SIZE - 1);
           if (PDC_picocalc_screenshot())
               PDC_beep();
       }

       return rx_head != rx_tail && !flush;
   }
   ```

   (Adjust the buffer names to match your backend's key buffer.)

The file dates on the card are left blank: the text-starter FAT32 driver
doesn't stamp dates yet, so the date-and-time name carries that
information instead.

## A related keyboard fix

With the v1.7 keyboard BIOS, Shift is applied by the BIOS itself (Shift+/
already arrives as `?`, Shift+F5 as F10), but the BIOS *also* reports Shift
as held. A key table whose "shifted" column is 0 for most keys then drops
F6-F10, Del and others while Shift is down. The fix is to fall back to the
normal code when the shifted (or control, or alt) entry is 0:

```c
if (shifted && key_table[i].shifted)
    return key_table[i].shifted;
if (control && key_table[i].control)
    return key_table[i].control;
if (alt && key_table[i].alt)
    return key_table[i].alt;
return key_table[i].normal;
```
