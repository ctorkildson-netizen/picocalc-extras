/* settime -- set the PicoCalc's shared clock from an NTP server.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Chris Torkildson.
 *
 * Needs a Pico 2 W (RP2350W) for WiFi. Reads ssid / password / tz / ntp
 * from /settime.cfg, or failing that /virtualt/virtualt.cfg (same format:
 * key=value per line, # comments). Puts the time into the RP2350's
 * always-on timer (see picocalc_clock.h), where every program started
 * after this one can read it until the power is switched off, then goes
 * back to the shell.
 *
 * If the clock is already set it just shows the time and returns at once,
 * so it is cheap to run from AUTOEXEC.BAT or /etc/profile. Press N while
 * it shows "already set" to fetch the time again anyway.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "lwip/apps/sntp.h"

#include "drivers/lcd.h"
#include "drivers/southbridge.h"
#include "drivers/fat32.h"
#include "drivers/keyboard.h"
#include "drivers/font.h"
#include "picocalc_shell.h"
#include "picocalc_clock.h"

#define C_WHITE 0xFFFF
#define C_LIT   0xFFE0
#define C_OK    0x07E0
#define C_ERR   0xF81F

static char cfg_ssid[64], cfg_pw[80], cfg_tz[64] = "PST8PDT,M3.2.0,M11.1.0", cfg_ntp[64] = "pool.ntp.org";
static int row = 2;

static void say(uint16_t colour, const char *s)
{
	char line[41];
	snprintf(line, sizeof line, "%-40s", s);
	lcd_set_foreground(colour);
	lcd_putstr(0, row++, line);
	lcd_set_foreground(C_WHITE);
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
	return s;
}

static int load_cfg(const char *path)
{
	static char buf[2048];
	fat32_file_t f;
	size_t n = 0;
	if (fat32_open(&f, path) != FAT32_OK)
		return 0;
	fat32_read(&f, buf, sizeof buf - 1, &n);
	fat32_close(&f);
	buf[n] = 0;
	for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n"))
	{
		char *l = trim(line), *eq = strchr(l, '=');
		if (*l == '#' || !eq)
			continue;
		*eq = 0;
		char *k = trim(l), *v = trim(eq + 1);
		char *dst = !strcasecmp(k, "ssid") ? cfg_ssid : !strcasecmp(k, "password") ? cfg_pw :
		            !strcasecmp(k, "tz") ? cfg_tz : !strcasecmp(k, "ntp") ? cfg_ntp : NULL;
		size_t cap = dst == cfg_pw ? sizeof cfg_pw : 64;
		if (dst) { strncpy(dst, v, cap - 1); dst[cap - 1] = 0; }
	}
	return 1;
}

static void show_time(uint16_t colour, const char *prefix)
{
	char t[48], l[48];
	time_t now = time(NULL);
	struct tm lt;
	localtime_r(&now, &lt);
	strftime(t, sizeof t, "%a %b %d %Y  %H:%M:%S", &lt);
	snprintf(l, sizeof l, "%s%s", prefix, t);
	say(colour, l);
}

/* wait up to ms for a key press; returns its code, or 0 */
static int wait_key(int ms)
{
	absolute_time_t until = make_timeout_time_ms(ms);
	while (absolute_time_diff_us(get_absolute_time(), until) > 0)
	{
		uint16_t ev = sb_read_keyboard();
		if ((ev >> 8) == KEY_STATE_PRESSED)
			return ev & 0xFF;
		sleep_ms(20);
	}
	return 0;
}

static void leave(void)
{
	lcd_clear_screen();
	picocalc_shell_exit();          /* back to /BOOT2350.UF2 */
	watchdog_reboot(0, 0, 0);       /* no card: plain reboot */
	for (;;) tight_loop_contents();
}

static volatile int sntp_done;
void vt_sntp_set_time(unsigned long sec)
{
	struct timeval tv = { .tv_sec = (time_t)sec, .tv_usec = 0 };
	settimeofday(&tv, NULL);
	sntp_done = 1;
}

static int fetch_ntp(void)
{
	char msg[64];
	if (!cfg_ssid[0])
	{
		say(C_ERR, "No ssid in /settime.cfg or");
		say(C_ERR, "/virtualt/virtualt.cfg");
		return 0;
	}
	snprintf(msg, sizeof msg, "WiFi: joining %.24s...", cfg_ssid);
	say(C_WHITE, msg);
	if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA) != 0)
	{
		say(C_ERR, "WiFi chip didn't start (not a Pico 2 W?)");
		return 0;
	}
	cyw43_arch_enable_sta_mode();
	int ok = 0;
	/* joins sometimes fail once (router slow to answer right after the
	   radio powers up): try up to three times */
	int err = -1;
	for (int attempt = 1; attempt <= 3 && err; attempt++)
	{
		if (attempt > 1)
		{
			snprintf(msg, sizeof msg, "WiFi: retrying (%d of 3)...", attempt);
			say(C_WHITE, msg);
		}
		err = cyw43_arch_wifi_connect_timeout_ms(cfg_ssid, cfg_pw, CYW43_AUTH_WPA2_AES_PSK, 15000);
	}
	if (err)
	{
		snprintf(msg, sizeof msg, "WiFi: couldn't join (%d)", err);
		say(C_ERR, msg);
	}
	else
	{
		snprintf(msg, sizeof msg, "NTP: asking %.28s...", cfg_ntp);
		say(C_WHITE, msg);
		sntp_done = 0;
		sntp_setoperatingmode(SNTP_OPMODE_POLL);
		sntp_setservername(0, cfg_ntp);
		sntp_init();
		absolute_time_t until = make_timeout_time_ms(10000);
		while (!sntp_done && absolute_time_diff_us(get_absolute_time(), until) > 0)
		{
			cyw43_arch_poll();
			sleep_ms(5);
		}
		sntp_stop();
		if (sntp_done)
			ok = picocalc_clock_save();
		else
			say(C_ERR, "NTP: no answer");
	}
	cyw43_arch_deinit();
	return ok;
}

int main(void)
{
	sb_init();
	lcd_init();
	fat32_init();
	lcd_enable_cursor(false);
	lcd_set_font(&font_8x10);
	lcd_clear_screen();
	lcd_set_foreground(C_LIT);
	lcd_putstr(0, 0, "settime -- PicoCalc clock from NTP");
	lcd_set_foreground(C_WHITE);

	if (!load_cfg("/settime.cfg"))
		load_cfg("/virtualt/virtualt.cfg");
	setenv("TZ", cfg_tz, 1);
	tzset();

	if (picocalc_clock_load())
	{
		show_time(C_OK, "Already set: ");
		say(C_WHITE, "");
		say(C_WHITE, "N: fetch again   other key: done");
		int k = wait_key(1500);
		if (k != 'n' && k != 'N')
			leave();
		row++;
	}

	if (fetch_ntp())
	{
		say(C_WHITE, "");
		show_time(C_OK, "Set: ");
		wait_key(1500);
		leave();
	}

	say(C_WHITE, "");
	say(C_WHITE, "Clock not set. Press any key.");
	while (!wait_key(1000))
		;
	leave();
}
