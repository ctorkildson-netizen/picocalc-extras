/* net.h -- Wi-Fi, TCP connections (out and in), name lookups and ping, for programs
 * on a PicoCalc with a Pico 2 W (picocalc-extras' net/; see its README).
 *
 * The network's name and password come from /ntp/ntp.cfg, the PicoCalc
 * shell's file for its NTP command (read with the shell's own parser), so
 * Wi-Fi is set up in one place on the card:
 *
 *     ssid = MyNetwork
 *     password = secret
 *     country = US
 *
 * Everything here waits (with a progress callback) rather than running in
 * the background; lwIP itself runs in the background, so data arriving
 * between calls isn't lost.
 */
#ifndef PICOCALC_NET_H
#define PICOCALC_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lwip/ip_addr.h"

/* called now and then while waiting, with what's happening ("Joining
   MyNetwork..."); return false to stop. May be NULL. */
typedef bool (*net_progress)(const char *what, void *ctx);

/* start the radio (once), with Wi-Fi's country from /ntp/ntp.cfg. A program
   that also uses picocalc-extras' Bluetooth calls this before btkbd_init(),
   so the two share the radio; otherwise net_join() calls it. */
bool net_radio(void);

/* join the network from /ntp/ntp.cfg, if not joined already; false, with why */
bool net_join(net_progress progress, void *ctx, char *why, size_t why_size);
const char *net_address(void);		/* ours, once joined: "192.168.1.23" */

/* leave the network properly before the program ends: without it the
   router keeps the old connection a minute or so, and turns the next
   program away until it runs out */
void net_leave(void);

/* "192.168.1.50" or "host.example.org" to an address (joining first if a
   name has to be looked up) */
bool net_lookup(const char *host, ip_addr_t *ip, net_progress progress, void *ctx,
		char *why, size_t why_size);

/* ping: one echo request to host, waiting up to timeout_ms for the reply;
   true with the round trip in *ms, or false with why ("No reply ...").
   Needs LWIP_RAW (net/config/lwipopts.h has it). */
bool net_ping(const char *host, unsigned timeout_ms, unsigned *ms, net_progress progress,
	      void *ctx, char *why, size_t why_size);

/* ---- TCP: any number of connections, outgoing or incoming, up to
   PICOCALC_NET_CONNS (2 unless the program sets it in CMake; each takes a
   16 KB receive buffer) */
typedef struct net_conn net_conn;
typedef struct net_listener net_listener;

/* connect (joining first if need be); NULL, with why, if not */
net_conn *net_dial(const char *host, uint16_t port, net_progress progress, void *ctx,
		   char *why, size_t why_size);
size_t net_conn_waiting(net_conn *c);		/* bytes arrived, not yet read */
size_t net_conn_read(net_conn *c, uint8_t *buf, size_t max);	/* what's arrived (0 if nothing yet) */
bool net_conn_write(net_conn *c, const uint8_t *s, size_t n);	/* waits for room; false if it couldn't */
bool net_conn_flush(net_conn *c, unsigned timeout_ms);	/* wait until the other end has it all */
bool net_conn_open(net_conn *c);		/* connected, or closed with data still to read */
bool net_conn_closed_by_host(net_conn *c);	/* the other end hung up (or the line dropped) */
const char *net_conn_peer(net_conn *c);		/* the other end's address */
void net_conn_close(net_conn *c);

/* listen on a port (0: any free one); connections that arrive wait until
   net_accept() takes them. net_accept() doesn't wait: NULL if none yet. */
net_listener *net_listen(uint16_t port, net_progress progress, void *ctx, char *why, size_t why_size);
uint16_t net_listener_port(net_listener *l);	/* the port, if 0 was asked for */
net_conn *net_accept(net_listener *l);
void net_unlisten(net_listener *l);
void net_close_all(void);			/* every connection and listener (net_leave() does this) */

/* ---- one connection at a time, as a serial line would be (the calls
   calcterm uses; they work on a connection from the same pool) */
bool net_connect(const char *host, uint16_t port, net_progress progress, void *ctx,
		 char *why, size_t why_size);
void net_close(void);
bool net_open(void);			/* connected, or closed with data still to read */
bool net_closed_by_host(void);		/* the host hung up (or the line dropped) */
size_t net_waiting(void);		/* bytes arrived, not yet read */
size_t net_read(uint8_t *buf, size_t max);
void net_write(const uint8_t *s, size_t n);

#endif
