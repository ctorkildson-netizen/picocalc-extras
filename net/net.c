/* net.c -- Wi-Fi, a TCP connection and name lookups: see net.h.
 *
 * lwIP runs in the background (a low-priority interrupt), so data keeps
 * arriving while the program draws or moves a file; what comes in waits in
 * a 16 KB ring, and the TCP window is opened again only as the program
 * reads it, so a fast host is held back rather than anything being dropped.
 * Calls into lwIP from here are made inside cyw43_arch_lwip_begin/end.
 *
 * The network is joined at the first need and stays joined after a
 * hang-up, so connecting again is quick. From calcterm (MIT).
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/tcp.h"
#include "drivers/filesystem.h"
#include "ntp.h"
#include "net.h"

#define RING 16384			/* a power of two, over TCP_WND */
#define CONNECT_MS 15000

static uint8_t ring[RING];
static volatile uint32_t head, tail;	/* head: lwIP's; tail: ours */
static uint32_t unacked;		/* read, not yet told to TCP */
static struct tcp_pcb *volatile pcb;
static volatile bool connected, host_closed;
static volatile err_t failure;		/* from the error callback or the connect */
static bool radio_on;
static bool go_on(net_progress progress, const char *what, void *ctx) { return !progress || progress(what, ctx); }
static char address[16];

/* ------------------------------------------------------------ lwIP's side */

static err_t on_recv(void *arg, struct tcp_pcb *p, struct pbuf *b, err_t err)
{
	(void)arg; (void)err;
	if (!b) {				/* the host closed it */
		host_closed = true;
		return ERR_OK;
	}
	if (b->tot_len > RING - (head - tail)) return ERR_MEM;	/* (lwIP keeps it, and tries again) */
	for (struct pbuf *q = b; q; q = q->next) {
		const uint8_t *d = q->payload;
		for (uint16_t i = 0; i < q->len; i++) ring[(head + i) & (RING - 1)] = d[i];
		head += q->len;
	}
	(void)p;
	pbuf_free(b);
	return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
	(void)arg;
	pcb = NULL;				/* lwIP has freed it */
	failure = err;
	host_closed = true;
}

static err_t on_connected(void *arg, struct tcp_pcb *p, err_t err)
{
	(void)arg; (void)p;
	if (err == ERR_OK) connected = true;
	else failure = err;
	return ERR_OK;
}

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

bool net_connect(const char *host, uint16_t port, net_progress progress, void *ctx, char *why, size_t n)
{
	net_close();
	if (!net_join(progress, ctx, why, n)) return false;

	char msg[96];
	ip_addr_t ip;
	if (!net_lookup(host, &ip, progress, ctx, why, n)) return false;

	head = tail = unacked = 0;
	connected = host_closed = false;
	failure = ERR_OK;
	cyw43_arch_lwip_begin();
	struct tcp_pcb *p = tcp_new_ip_type(IPADDR_TYPE_V4);
	if (p) {
		tcp_arg(p, NULL);
		tcp_recv(p, on_recv);
		tcp_err(p, on_err);
		tcp_nagle_disable(p);		/* keys go as they're typed */
		ip_set_option(p, SOF_KEEPALIVE);
		pcb = p;
		if (tcp_connect(p, &ip, port, on_connected) != ERR_OK) { tcp_abort(p); pcb = NULL; }
	}
	cyw43_arch_lwip_end();
	if (!pcb) { snprintf(why, n, "Out of memory for a connection"); return false; }

	snprintf(msg, sizeof msg, "Connecting to %.40s, port %u...", ipaddr_ntoa(&ip), port);
	absolute_time_t until = make_timeout_time_ms(CONNECT_MS);
	while (!connected && pcb && failure == ERR_OK && !time_reached(until)) {
		if (!go_on(progress, msg, ctx)) { net_close(); snprintf(why, n, "Stopped."); return false; }
		sleep_ms(20);
	}
	if (connected && pcb) return true;
	err_t f = failure;
	net_close();
	if (f == ERR_RST) snprintf(why, n, "%.40s refused the connection (is anything listening on that port?)", host);
	else snprintf(why, n, "No answer from %.40s, port %u", host, port);
	return false;
}

void net_close(void)
{
	connected = false;
	head = tail = unacked = 0;
	if (!radio_on) return;			/* (nothing to close, and no lwIP to lock yet) */
	cyw43_arch_lwip_begin();
	struct tcp_pcb *p = pcb;
	pcb = NULL;
	if (p) {
		tcp_arg(p, NULL);
		tcp_recv(p, NULL);
		tcp_err(p, NULL);
		if (tcp_close(p) != ERR_OK) tcp_abort(p);
	}
	cyw43_arch_lwip_end();
}

bool net_open(void)
{
	return (connected && pcb && !host_closed) || head != tail;
}

bool net_closed_by_host(void)
{
	return connected && host_closed && head == tail;
}

size_t net_waiting(void)
{
	return head - tail;
}

size_t net_read(uint8_t *buf, size_t max)
{
	size_t k = 0;
	uint32_t h = head;
	while (k < max && tail != h) buf[k++] = ring[tail++ & (RING - 1)];
	unacked += k;
	if (radio_on && (unacked >= 512 || (unacked && head == tail))) {	/* open the window again */
		cyw43_arch_lwip_begin();
		if (pcb) tcp_recved(pcb, (u16_t)unacked);
		cyw43_arch_lwip_end();
		unacked = 0;
	}
	return k;
}

void net_write(const uint8_t *s, size_t n)
{
	absolute_time_t until = make_timeout_time_ms(10000);
	while (n && pcb && !time_reached(until)) {
		cyw43_arch_lwip_begin();
		size_t m = 0;
		if (pcb) {
			size_t room = tcp_sndbuf(pcb);
			m = n < room ? n : room;
			if (m && tcp_write(pcb, s, (u16_t)m, TCP_WRITE_FLAG_COPY) != ERR_OK) m = 0;
			tcp_output(pcb);
		}
		cyw43_arch_lwip_end();
		if (m) { s += m; n -= m; until = make_timeout_time_ms(10000); }
		else sleep_us(500);
	}
}

void net_leave(void)
{
	if (!wifi_on) return;			/* (the radio may be on for Bluetooth alone) */
	net_close();
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
