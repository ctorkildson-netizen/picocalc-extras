/* picocalc_clock.h -- a time of day shared by every program on a PicoCalc.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Chris Torkildson.
 *
 * The PicoCalc has no battery-backed clock, but the RP2350's always-on
 * (POWMAN) timer keeps running, from the crystal, through the soft reboot
 * that UF2 Loader does when it switches from one program to the next. It
 * only stops when the power is switched off. So one program (settime, or
 * VirtualT) can set the time once after power-on, and every program
 * started after that can read it.
 *
 *     #include "picocalc_clock.h"
 *     ...
 *     if (picocalc_clock_load())    // sets this program's time() etc.
 *         ...the time is known...
 *
 *     // after learning the time some other way (NTP, the user typing it):
 *     picocalc_clock_save();        // share the current time() with the
 *                                   // programs that run after this one
 *
 * Only real times should go into the timer -- never a guess -- so that a
 * program reading it can trust it. Times are UTC (seconds since 1970);
 * set the TZ environment variable for local time.
 *
 * Needs the Pico SDK's pico_aon_timer library (RP2350 only).
 */
#ifndef PICOCALC_CLOCK_H
#define PICOCALC_CLOCK_H

#include <stdbool.h>
#include <sys/time.h>
#include <time.h>
#include "pico/aon_timer.h"

/* Anything earlier than this (Sept 2020) is the timer counting from
   power-on rather than a real date. */
#define PICOCALC_CLOCK_VALID_AFTER 1600000000

/* True if the always-on timer holds a real time; fills *ts if not NULL. */
static inline bool picocalc_clock_is_set(struct timespec *ts)
{
    struct timespec t;
    if (!aon_timer_is_running() || !aon_timer_get_time(&t))
        return false;
    if (t.tv_sec < PICOCALC_CLOCK_VALID_AFTER)
        return false;
    if (ts)
        *ts = t;
    return true;
}

/* Set this program's clock (time(), gettimeofday(), localtime()) from the
   shared clock. Returns false, changing nothing, if it isn't set. */
static inline bool picocalc_clock_load(void)
{
    struct timespec ts;
    if (!picocalc_clock_is_set(&ts))
        return false;
    struct timeval tv = { .tv_sec = ts.tv_sec, .tv_usec = ts.tv_nsec / 1000 };
    settimeofday(&tv, NULL);
    return true;
}

/* Copy this program's current time into the shared clock. Returns false
   if this program doesn't know a real time itself. */
static inline bool picocalc_clock_save(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tv.tv_sec < PICOCALC_CLOCK_VALID_AFTER)
        return false;
    struct timespec ts = { .tv_sec = tv.tv_sec, .tv_nsec = tv.tv_usec * 1000 };
    return aon_timer_is_running() ? aon_timer_set_time(&ts) : aon_timer_start(&ts);
}

#endif /* PICOCALC_CLOCK_H */
