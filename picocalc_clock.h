/* picocalc_clock.h -- the time of day, for programs written for the older
 * shared clock in this repository.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Chris Torkildson.
 *
 * The shared clock now lives in Blair Leduc's PicoCalc shell (v1.1 and
 * later): the shell keeps the time in the RP2350's always-on timer, with a
 * record of it that tells a program whether it is set and what the local
 * time zone is, and in a DS3231 when there is one. New programs should use
 * picocalc_shell_time() from the shell's picocalc_shell.h directly.
 *
 * This header keeps the three functions older programs here used, on top
 * of the shell's clock:
 *
 *     #include "picocalc_clock.h"
 *     ...
 *     if (picocalc_clock_load())    // sets this program's time(), and TZ
 *         ...the time is known...
 *
 *     picocalc_clock_save();        // after learning the time some other way
 *
 * Needs picocalc-shell v1.1's include/picocalc_shell.h on the include path,
 * and hardware_powman linked.
 */
#ifndef PICOCALC_CLOCK_H
#define PICOCALC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "picocalc_shell.h"

#ifndef PICOCALC_SHELL_HAS_TIME
#error "picocalc_clock.h needs picocalc-shell v1.1 or later, and hardware_powman linked"
#endif

/* Anything earlier than this (Sept 2020) can't be a real date. */
#define PICOCALC_CLOCK_VALID_AFTER 1600000000

/* True if the shell's clock is set; fills *ts (UTC) if not NULL. */
static inline bool picocalc_clock_is_set(struct timespec *ts)
{
    int64_t ms;
    int offset;
    if (!picocalc_shell_time(&ms, &offset))
        return false;
    if (ts) {
        ts->tv_sec = (time_t)(ms / 1000);
        ts->tv_nsec = (long)(ms % 1000) * 1000000L;
    }
    return true;
}

/* Set this program's clock (time(), gettimeofday()) from the shell's, and,
   unless the program has set TZ itself, its time zone from the shell's, so
   localtime() gives local time. Returns false, changing nothing, if the
   clock isn't set. */
static inline bool picocalc_clock_load(void)
{
    int64_t ms;
    int offset;
    if (!picocalc_shell_time(&ms, &offset))
        return false;
    struct timeval tv = { .tv_sec = (time_t)(ms / 1000), .tv_usec = (long)(ms % 1000) * 1000L };
    settimeofday(&tv, NULL);
    if (!getenv("TZ")) {
        /* POSIX TZ counts the other way: 7 hours behind UTC is "+7" */
        int west = -offset, a = west < 0 ? -west : west;
        char tz[24];
        snprintf(tz, sizeof tz, "LOC%c%d:%02d", west < 0 ? '-' : '+', a / 60, a % 60);
        setenv("TZ", tz, 1);
        tzset();
    }
    return true;
}

/* Put this program's current time into the shell's clock, for the programs
   after it (the shell writes the DS3231, if there is one, when it comes
   back). Returns false if this program doesn't know a real time itself. */
static inline bool picocalc_clock_save(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tv.tv_sec < PICOCALC_CLOCK_VALID_AFTER)
        return false;
    picocalc_shell_set_time((int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000);
    return true;
}

#endif /* PICOCALC_CLOCK_H */
