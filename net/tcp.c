/* tcp.c -- TCP connections, outgoing and incoming, for net.h.
 *
 * A small pool of connections (PICOCALC_NET_CONNS, 2 unless the program
 * says otherwise), each with its own 16 KB receive ring. lwIP runs in the
 * background, so data keeps arriving while the program is busy; it waits in
 * the ring, and the TCP window opens again only as the program reads it, so
 * a fast host is held back rather than anything being dropped.
 *
 * Listening: net_listen() takes a port; connections that arrive are given
 * a free connection from the pool at once, and wait in the listener's queue
 * until net_accept() collects them. With no connection free, the newcomer
 * is turned away (the program sees nothing).
 *
 * The older one-connection calls (net_connect(), net_read() ...) work on a
 * connection of their own from the same pool, so programs written for them
 * carry on unchanged. All calls into lwIP are inside cyw43_arch_lwip_begin/end.
 * MIT, like the rest of picocalc-extras.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include "net.h"

#ifndef PICOCALC_NET_CONNS
#define PICOCALC_NET_CONNS 2
#endif
#define RING 16384			/* a power of two, over TCP_WND */
#define CONNECT_MS 15000
#define QUEUE 4				/* connections waiting to be accepted */
#define LISTENERS 4

#if TCP_WND >= RING
#error "TCP_WND must be under net/'s 16 KB receive ring (net/config/lwipopts.h has 8 * TCP_MSS)"
#endif

struct net_conn {
	struct tcp_pcb *volatile pcb;
	volatile uint32_t head, tail;	/* head: lwIP's; tail: ours */
	uint32_t unacked;		/* read, not yet told to TCP */
	volatile bool used, connected, host_closed;
	volatile err_t failure;
	uint8_t ring[RING];
};

struct net_listener {
	struct tcp_pcb *pcb;
	uint16_t port;
	bool used;
	net_conn *volatile queue[QUEUE];
	volatile unsigned qin, qout;
};

static net_conn conns[PICOCALC_NET_CONNS];
static net_listener listeners[LISTENERS];

/* ------------------------------------------------------------ lwIP's side */

static err_t on_recv(void *arg, struct tcp_pcb *p, struct pbuf *b, err_t err)
{
	net_conn *c = arg;
	(void)p; (void)err;
	if (!c) { if (b) pbuf_free(b); return ERR_OK; }
	if (!b) {			/* the host closed it */
		c->host_closed = true;
		return ERR_OK;
	}
	if (b->tot_len > RING - (c->head - c->tail)) return ERR_MEM;	/* (lwIP keeps it, and tries again) */
	for (struct pbuf *q = b; q; q = q->next) {
		const uint8_t *d = q->payload;
		for (uint16_t i = 0; i < q->len; i++) c->ring[(c->head + i) & (RING - 1)] = d[i];
		c->head += q->len;
	}
	pbuf_free(b);
	return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
	net_conn *c = arg;
	if (!c) return;
	c->pcb = NULL;			/* lwIP has freed it */
	c->failure = err;
	c->host_closed = true;
}

static err_t on_connected(void *arg, struct tcp_pcb *p, err_t err)
{
	net_conn *c = arg;
	(void)p;
	if (err == ERR_OK) c->connected = true;
	else c->failure = err;
	return ERR_OK;
}

/* (with lwIP locked) a free connection from the pool, emptied */
static net_conn *take(void)
{
	for (int i = 0; i < PICOCALC_NET_CONNS; i++) {
		net_conn *c = &conns[i];
		if (c->used) continue;
		c->used = true;
		c->pcb = NULL;
		c->head = c->tail = c->unacked = 0;
		c->connected = c->host_closed = false;
		c->failure = ERR_OK;
		return c;
	}
	return NULL;
}

static void attach(net_conn *c, struct tcp_pcb *p)
{
	c->pcb = p;
	tcp_arg(p, c);
	tcp_recv(p, on_recv);
	tcp_err(p, on_err);
	tcp_nagle_disable(p);		/* small writes (keys, replies) go at once */
	ip_set_option(p, SOF_KEEPALIVE);
}

static err_t on_accept(void *arg, struct tcp_pcb *p, err_t err)
{
	net_listener *l = arg;
	if (err != ERR_OK || !p) return ERR_VAL;
	net_conn *c = (l->qin - l->qout < QUEUE) ? take() : NULL;
	if (!c) { tcp_abort(p); return ERR_ABRT; }	/* full: turned away */
	attach(c, p);
	c->connected = true;
	l->queue[l->qin % QUEUE] = c;
	l->qin++;
	return ERR_OK;
}

/* ------------------------------------------------------------ connections */

net_conn *net_dial(const char *host, uint16_t port, net_progress progress, void *ctx, char *why, size_t n)
{
	if (!net_join(progress, ctx, why, n)) return NULL;
	ip_addr_t ip;
	if (!net_lookup(host, &ip, progress, ctx, why, n)) return NULL;

	cyw43_arch_lwip_begin();
	net_conn *c = take();
	struct tcp_pcb *p = c ? tcp_new_ip_type(IPADDR_TYPE_V4) : NULL;
	if (p) {
		attach(c, p);
		if (tcp_connect(p, &ip, port, on_connected) != ERR_OK) { tcp_abort(p); c->pcb = NULL; }
	}
	bool ok = c && c->pcb;
	if (c && !ok) c->used = false;
	cyw43_arch_lwip_end();
	if (!c) { snprintf(why, n, "No connection free (all %d in use)", PICOCALC_NET_CONNS); return NULL; }
	if (!ok) { snprintf(why, n, "Out of memory for a connection"); return NULL; }

	char msg[96];
	snprintf(msg, sizeof msg, "Connecting to %.40s, port %u...", ipaddr_ntoa(&ip), port);
	absolute_time_t until = make_timeout_time_ms(CONNECT_MS);
	while (!c->connected && c->pcb && c->failure == ERR_OK && !time_reached(until)) {
		if (progress && !progress(msg, ctx)) { net_conn_close(c); snprintf(why, n, "Stopped."); return NULL; }
		sleep_ms(20);
	}
	if (c->connected && c->pcb) return c;
	err_t f = c->failure;
	net_conn_close(c);
	if (f == ERR_RST) snprintf(why, n, "%.40s refused the connection (is anything listening on that port?)", host);
	else snprintf(why, n, "No answer from %.40s, port %u", host, port);
	return NULL;
}

void net_conn_close(net_conn *c)
{
	if (!c || !c->used) return;
	cyw43_arch_lwip_begin();
	struct tcp_pcb *p = c->pcb;
	c->pcb = NULL;
	if (p) {
		tcp_arg(p, NULL);
		tcp_recv(p, NULL);
		tcp_err(p, NULL);
		if (tcp_close(p) != ERR_OK) tcp_abort(p);
	}
	c->connected = false;
	c->head = c->tail = c->unacked = 0;
	c->used = false;
	cyw43_arch_lwip_end();
}

bool net_conn_open(net_conn *c)
{
	return c && c->used && ((c->connected && c->pcb && !c->host_closed) || c->head != c->tail);
}

bool net_conn_closed_by_host(net_conn *c)
{
	return c && c->used && c->connected && c->host_closed && c->head == c->tail;
}

size_t net_conn_waiting(net_conn *c)
{
	return c && c->used ? c->head - c->tail : 0;
}

size_t net_conn_read(net_conn *c, uint8_t *buf, size_t max)
{
	if (!c || !c->used) return 0;
	size_t k = 0;
	uint32_t h = c->head;
	while (k < max && c->tail != h) buf[k++] = c->ring[c->tail++ & (RING - 1)];
	c->unacked += k;
	if (c->unacked >= 512 || (c->unacked && c->head == c->tail)) {	/* open the window again */
		cyw43_arch_lwip_begin();
		if (c->pcb) tcp_recved(c->pcb, (u16_t)c->unacked);
		cyw43_arch_lwip_end();
		c->unacked = 0;
	}
	return k;
}

bool net_conn_write(net_conn *c, const uint8_t *s, size_t n)
{
	if (!c || !c->used) return false;
	absolute_time_t until = make_timeout_time_ms(10000);
	while (n && c->pcb && !time_reached(until)) {
		cyw43_arch_lwip_begin();
		size_t m = 0;
		if (c->pcb) {
			size_t room = tcp_sndbuf(c->pcb);
			m = n < room ? n : room;
			if (m > 0xffff) m = 0xffff;
			if (m && tcp_write(c->pcb, s, (u16_t)m, TCP_WRITE_FLAG_COPY) != ERR_OK) m = 0;
			tcp_output(c->pcb);
		}
		cyw43_arch_lwip_end();
		if (m) { s += m; n -= m; until = make_timeout_time_ms(10000); }
		else sleep_us(500);
	}
	return n == 0;
}

bool net_conn_flush(net_conn *c, unsigned timeout_ms)
{
	if (!c || !c->used) return false;
	absolute_time_t until = make_timeout_time_ms(timeout_ms);
	for (;;) {
		cyw43_arch_lwip_begin();
		bool done = !c->pcb || (tcp_sndqueuelen(c->pcb) == 0 && c->pcb->unacked == NULL);
		cyw43_arch_lwip_end();
		if (done) return c->pcb != NULL;
		if (time_reached(until)) return false;
		sleep_ms(5);
	}
}

const char *net_conn_peer(net_conn *c)
{
	static char a[16];
	a[0] = 0;
	if (c && c->pcb) snprintf(a, sizeof a, "%s", ipaddr_ntoa(&c->pcb->remote_ip));
	return a;
}

/* ------------------------------------------------------------ listening */

net_listener *net_listen(uint16_t port, net_progress progress, void *ctx, char *why, size_t n)
{
	if (!net_join(progress, ctx, why, n)) return NULL;
	net_listener *l = NULL;
	for (int i = 0; i < LISTENERS; i++) if (!listeners[i].used) { l = &listeners[i]; break; }
	if (!l) { snprintf(why, n, "Too many ports listened on"); return NULL; }

	cyw43_arch_lwip_begin();
	struct tcp_pcb *p = tcp_new_ip_type(IPADDR_TYPE_V4);
	err_t e = p ? tcp_bind(p, IP_ANY_TYPE, port) : ERR_MEM;
	struct tcp_pcb *lp = NULL;
	if (e == ERR_OK) {
		lp = tcp_listen_with_backlog(p, 2);
		if (!lp) tcp_close(p);
	} else if (p) tcp_close(p);
	if (lp) {
		memset(l, 0, sizeof *l);
		l->used = true;
		l->pcb = lp;
		l->port = lp->local_port;
		tcp_arg(lp, l);
		tcp_accept(lp, on_accept);
	}
	cyw43_arch_lwip_end();
	if (lp) return l;
	if (e == ERR_USE) snprintf(why, n, "Port %u is already in use", port);
	else snprintf(why, n, "Couldn't listen on port %u", port);
	return NULL;
}

uint16_t net_listener_port(net_listener *l)
{
	return l && l->used ? l->port : 0;
}

net_conn *net_accept(net_listener *l)
{
	if (!l || !l->used || l->qin == l->qout) return NULL;
	net_conn *c = l->queue[l->qout % QUEUE];
	l->qout++;
	return c;
}

void net_unlisten(net_listener *l)
{
	if (!l || !l->used) return;
	cyw43_arch_lwip_begin();
	if (l->pcb) { tcp_arg(l->pcb, NULL); tcp_accept(l->pcb, NULL); tcp_close(l->pcb); }
	l->pcb = NULL;
	cyw43_arch_lwip_end();
	while (l->qin != l->qout) net_conn_close(net_accept(l));	/* never collected */
	l->used = false;
}

void net_close_all(void)
{
	for (int i = 0; i < LISTENERS; i++) net_unlisten(&listeners[i]);
	for (int i = 0; i < PICOCALC_NET_CONNS; i++) net_conn_close(&conns[i]);
}

/* ------------------------------------------------------------ one connection, as before */

static net_conn *only;

bool net_connect(const char *host, uint16_t port, net_progress progress, void *ctx, char *why, size_t n)
{
	net_close();
	only = net_dial(host, port, progress, ctx, why, n);
	return only != NULL;
}

void net_close(void) { net_conn_close(only); only = NULL; }
bool net_open(void) { return net_conn_open(only); }
bool net_closed_by_host(void) { return net_conn_closed_by_host(only); }
size_t net_waiting(void) { return net_conn_waiting(only); }
size_t net_read(uint8_t *buf, size_t max) { return net_conn_read(only, buf, max); }
void net_write(const uint8_t *s, size_t n) { net_conn_write(only, s, n); }
