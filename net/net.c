/* net.c -- Wi-Fi and name lookups: see net.h (TCP is in tcp.c, ping in
 * ping.c).
 *
 * The network is joined at the first need and stays joined after a
 * hang-up, so connecting again is quick. Calls into lwIP are made inside
 * cyw43_arch_lwip_begin/end. From calcterm (MIT).
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/udp.h"
#include "drivers/filesystem.h"
#include "ntp.h"
#include "net.h"

static bool radio_on;
static bool go_on(net_progress progress, const char *what, void *ctx) { return !progress || progress(what, ctx); }
static char address[16];

/* ------------------------------------------------------------ lwIP's side */

static volatile int dns_state;		/* 0 waiting, 1 found, -1 not */
static ip_addr_t dns_ip;

static void on_dns(const char *name, const ip_addr_t *ip, void *arg)
{
	(void)name; (void)arg;
	if (ip) { dns_ip = *ip; dns_state = 1; }
	else dns_state = -1;
}

/* ------------------------------------------------------------ joining */

static ntp_config_t wc;

/* /ntp/ntp.cfg into wc; false, with why, if it isn't there or has a mistake */
static bool read_cfg(char *why, size_t n)
{
	FIL f;
	if (f_open(&f, NTP_CFG_PATH, FA_READ) != FR_OK) {
		snprintf(why, n, "No %s: the Wi-Fi network goes there (see the README)", NTP_CFG_PATH);
		return false;
	}
	static char text[NTP_CFG_BYTES];
	UINT got = 0;
	f_read(&f, text, sizeof text - 1, &got);
	f_close(&f);
	unsigned line = 0;
	ntp_cfg_status_t st = ntp_config_parse(&wc, text, got, &line);
	if (st == NTP_CFG_OK) return true;
	if (line) snprintf(why, n, "%s line %u: %s", NTP_CFG_PATH, line, ntp_cfg_status_str(st));
	else snprintf(why, n, "%s: %s", NTP_CFG_PATH, ntp_cfg_status_str(st));
	return false;
}

bool net_radio(void)
{
	if (radio_on) return true;
	char why[80];
	bool have = read_cfg(why, sizeof why);		/* (for Wi-Fi's country; worldwide without) */
	uint32_t country = have && wc.country[0] ? CYW43_COUNTRY(wc.country[0], wc.country[1], 0) : CYW43_COUNTRY_WORLDWIDE;
	if (cyw43_arch_init_with_country(country)) return false;
	radio_on = true;
	return true;
}

static bool wifi_on;			/* the radio's Wi-Fi side switched on (at the first connection) */

bool net_join(net_progress progress, void *ctx, char *why, size_t n)
{
	if (wifi_on && cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) return true;
	if (!read_cfg(why, n)) return false;
	if (!radio_on) {
		if (!go_on(progress, "Starting Wi-Fi...", ctx)) { snprintf(why, n, "Stopped."); return false; }
		if (!net_radio()) {
			snprintf(why, n, "No Wi-Fi on this board (it needs a Pico 2 W)");
			return false;
		}
	}
	if (!wifi_on) {
		cyw43_arch_enable_sta_mode();
		wifi_on = true;
	}

	char msg[64];
	snprintf(msg, sizeof msg, "Joining %.32s...", wc.ssid);
	const char *pw = wc.password[0] ? wc.password : NULL;
	uint32_t auth = pw ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
	if (cyw43_arch_wifi_connect_async(wc.ssid, pw, auth)) {
		snprintf(why, n, "Couldn't start joining %.32s", wc.ssid);
		return false;
	}
	unsigned secs = wc.timeout_s ? wc.timeout_s : 20;
	absolute_time_t until = make_timeout_time_ms(secs * 1000);
	int link = CYW43_LINK_DOWN;
	while (!time_reached(until)) {
		if (!go_on(progress, msg, ctx)) { snprintf(why, n, "Stopped."); return false; }
		link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
		if (link == CYW43_LINK_UP) {
			snprintf(address, sizeof address, "%s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
			return true;
		}
		if (link == CYW43_LINK_BADAUTH) { snprintf(why, n, "Wrong password for %.32s", wc.ssid); return false; }
		/* not found yet: ask again, as cyw43_arch_wifi_connect_timeout_ms() does */
		if (link == CYW43_LINK_NONET || link == CYW43_LINK_FAIL)
			cyw43_arch_wifi_connect_async(wc.ssid, pw, auth);
		sleep_ms(100);
	}
	if (link == CYW43_LINK_JOIN || link == CYW43_LINK_NOIP)
		snprintf(why, n, "Joined %.32s, but got no address (DHCP)", wc.ssid);
	else
		snprintf(why, n, "Couldn't find or join %.32s", wc.ssid);
	return false;
}

/* ------------------------------------------------------------ the program's side */

bool net_lookup(const char *host, ip_addr_t *ip, net_progress progress, void *ctx, char *why, size_t n)
{
	if (ipaddr_aton(host, ip)) return true;
	if (!net_join(progress, ctx, why, n)) return false;
	char msg[80];
	snprintf(msg, sizeof msg, "Looking up %.60s...", host);
	dns_state = 0;
	cyw43_arch_lwip_begin();
	err_t e = dns_gethostbyname(host, &dns_ip, on_dns, NULL);
	cyw43_arch_lwip_end();
	if (e == ERR_OK) dns_state = 1;
	else if (e != ERR_INPROGRESS) dns_state = -1;
	absolute_time_t until = make_timeout_time_ms(10000);
	while (dns_state == 0 && !time_reached(until)) {
		if (!go_on(progress, msg, ctx)) { snprintf(why, n, "Stopped."); return false; }
		sleep_ms(50);
	}
	if (dns_state != 1) { snprintf(why, n, "Can't find %.50s (try its address)", host); return false; }
	*ip = dns_ip;
	return true;
}

void net_leave(void)
{
	if (!wifi_on) return;			/* (the radio may be on for Bluetooth alone) */
	net_close_all();
	/* tell the router we've gone: without it, it keeps the PicoCalc's old
	   connection for a minute or so, and turns away the next program that
	   joins (the dots, then "retrying") until that runs out */
	cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
	sleep_ms(200);				/* (time for the message to go) */
	wifi_on = false;
}

const char *net_address(void)
{
	return address;
}

/* ------------------------------------------------------------ the time (SNTP) */

static volatile int64_t sntp_ms;	/* the answer, or 0 while waiting */
static volatile bool sntp_got;

static void on_sntp(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
	(void)arg; (void)pcb; (void)addr; (void)port;
	uint8_t r[48];
	if (p->tot_len >= 48 && pbuf_copy_partial(p, r, 48, 0) == 48) {
		uint8_t mode = r[0] & 7, stratum = r[1];
		uint32_t secs = (uint32_t)r[40] << 24 | (uint32_t)r[41] << 16 | (uint32_t)r[42] << 8 | r[43];
		uint32_t frac = (uint32_t)r[44] << 24 | (uint32_t)r[45] << 16 | (uint32_t)r[46] << 8 | r[47];
		/* a server's answer (mode 4), not a "kiss of death" (stratum 0), with a time */
		if (mode == 4 && stratum != 0 && secs) {
			/* NTP counts from 1900; 2,208,988,800 s to 1970. (Era 0 runs to 2036.) */
			sntp_ms = ((int64_t)secs - 2208988800LL) * 1000 + (int64_t)(((uint64_t)frac * 1000) >> 32);
			sntp_got = true;
		}
	}
	pbuf_free(p);
}

bool net_time(int64_t *utc_ms, net_progress progress, void *ctx, char *why, size_t n)
{
	if (!net_join(progress, ctx, why, n)) return false;
	const char *server = wc.server[0] ? wc.server : "pool.ntp.org";
	ip_addr_t ip;
	if (!net_lookup(server, &ip, progress, ctx, why, n)) return false;
	char msg[80];
	snprintf(msg, sizeof msg, "Asking %.50s the time...", server);
	for (int attempt = 0; attempt < 3; attempt++) {
		cyw43_arch_lwip_begin();
		struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_V4);
		struct pbuf *p = pcb ? pbuf_alloc(PBUF_TRANSPORT, 48, PBUF_RAM) : NULL;
		if (p) {
			memset(p->payload, 0, 48);
			((uint8_t *)p->payload)[0] = 0x23;	/* no leap warning, version 4, client */
			sntp_got = false;
			udp_recv(pcb, on_sntp, NULL);
			udp_sendto(pcb, p, &ip, 123);
			pbuf_free(p);
		}
		cyw43_arch_lwip_end();
		if (!pcb) { snprintf(why, n, "Out of memory asking the time"); return false; }
		absolute_time_t until = make_timeout_time_ms(3000);
		while (!sntp_got && !time_reached(until)) {
			if (progress && !progress(msg, ctx)) break;
			sleep_ms(10);
		}
		cyw43_arch_lwip_begin();
		udp_remove(pcb);
		cyw43_arch_lwip_end();
		if (sntp_got) { *utc_ms = sntp_ms; return true; }
	}
	snprintf(why, n, "No time from %.50s", server);
	return false;
}
