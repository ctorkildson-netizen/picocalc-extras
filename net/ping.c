/* ping.c -- ICMP echo (ping) for net.h, on an lwIP raw socket.
 *
 * One request at a time: a raw ICMP pcb is opened for it, the reply with
 * our id and sequence number is taken (anything else is left for lwIP),
 * and the pcb is closed again. MIT, like the rest of picocalc-extras.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip4.h"
#include "net.h"

#if !LWIP_RAW
#error "net_ping() needs LWIP_RAW 1 in lwipopts.h (net/config/lwipopts.h has it)"
#endif

#define PING_ID 0xCA1C
#define PING_DATA 32

static volatile bool got;
static uint16_t seq;
#ifdef NET_PING_DEBUG
static volatile unsigned seen; static volatile uint8_t l_type; static volatile uint16_t l_id, l_seq;
static err_t l_err;
#endif

static u8_t on_reply(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *from)
{
	(void)arg; (void)pcb; (void)from;
	/* the IP header comes first; its length is in its first byte */
	if (p->tot_len < IP_HLEN + sizeof(struct icmp_echo_hdr)) return 0;
	uint8_t vhl;
	pbuf_copy_partial(p, &vhl, 1, 0);
	u16_t hl = (u16_t)((vhl & 0x0f) * 4);
	struct icmp_echo_hdr e;
	if (pbuf_copy_partial(p, &e, sizeof e, hl) != sizeof e) return 0;
#ifdef NET_PING_DEBUG
	seen++; l_type = e.type; l_id = lwip_ntohs(e.id); l_seq = lwip_ntohs(e.seqno);
#endif
	if (e.type != ICMP_ER || e.id != lwip_htons(PING_ID) || e.seqno != lwip_htons(seq)) return 0;
	got = true;
	pbuf_free(p);
	return 1;			/* ours: eaten */
}

static bool send_one(struct raw_pcb *pcb, const ip_addr_t *ip)
{
	size_t len = sizeof(struct icmp_echo_hdr) + PING_DATA;
	struct pbuf *p = pbuf_alloc(PBUF_IP, (u16_t)len, PBUF_RAM);
	if (!p) return false;
	struct icmp_echo_hdr *e = (struct icmp_echo_hdr *)p->payload;
	ICMPH_TYPE_SET(e, ICMP_ECHO);
	ICMPH_CODE_SET(e, 0);
	e->chksum = 0;
	e->id = lwip_htons(PING_ID);
	e->seqno = lwip_htons(seq);
	uint8_t *d = (uint8_t *)(e + 1);
	for (int i = 0; i < PING_DATA; i++) d[i] = (uint8_t)('a' + i % 26);
	e->chksum = inet_chksum(e, (u16_t)len);
	err_t r = raw_sendto(pcb, p, ip);
#ifdef NET_PING_DEBUG
	l_err = r;
#endif
	pbuf_free(p);
	return r == ERR_OK;
}

bool net_ping(const char *host, unsigned timeout_ms, unsigned *ms, net_progress progress,
	      void *ctx, char *why, size_t n)
{
	if (!net_join(progress, ctx, why, n)) return false;
	ip_addr_t ip;
	if (!net_lookup(host, &ip, progress, ctx, why, n)) return false;

	cyw43_arch_lwip_begin();
	struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
	bool sent = false;
	if (pcb) {
		raw_recv(pcb, on_reply, NULL);
		raw_bind(pcb, IP_ADDR_ANY);
		seq++;
		got = false;
		sent = send_one(pcb, &ip);
	}
	cyw43_arch_lwip_end();
	if (!pcb) { snprintf(why, n, "Out of memory for a ping"); return false; }

	absolute_time_t start = get_absolute_time();
	absolute_time_t until = delayed_by_ms(start, timeout_ms);
	char msg[64];
	snprintf(msg, sizeof msg, "Pinging %.40s...", ipaddr_ntoa(&ip));
	while (sent && !got && !time_reached(until)) {
		if (progress && !progress(msg, ctx)) break;
		sleep_ms(1);
	}
	unsigned took = (unsigned)(absolute_time_diff_us(start, get_absolute_time()) / 1000);

	cyw43_arch_lwip_begin();
	raw_remove(pcb);
	cyw43_arch_lwip_end();

	if (got) { if (ms) *ms = took; return true; }
	if (!sent) snprintf(why, n, "Couldn't send to %.40s", ipaddr_ntoa(&ip));
	else snprintf(why, n, "No reply from %.40s in %u ms", ipaddr_ntoa(&ip), timeout_ms);
#ifdef NET_PING_DEBUG
	size_t k = strlen(why);
	snprintf(why + k, n - k, " [sent %d seq %u; ICMP in %u, last type %u id %04x seq %u]",
		 (int)l_err, seq, seen, l_type, l_id, l_seq);
#endif
	return false;
}
