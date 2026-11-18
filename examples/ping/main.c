/* ping -- pings a host from the PicoCalc over Wi-Fi.
 *
 * An example of picocalc-extras' net/: join the network from /ntp/ntp.cfg,
 * then ping whatever's typed, four times, as ping does on a PC. The
 * screen and keyboard are Blair Leduc's picocalc-text-starter drivers,
 * through printf() and getchar(). MIT.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "drivers/picocalc.h"
#include "picocalc_shell.h"
#include "net.h"

/* the text starter's keyboard driver sets this on Brk; this program doesn't use it */
volatile bool user_interrupt = false;

static bool dots(const char *what, void *ctx)
{
	/* a line per new message, then a dot a second while it lasts */
	static char last[80];
	static absolute_time_t next;
	(void)ctx;
	if (strcmp(what, last)) {
		snprintf(last, sizeof last, "%s", what);
		printf("\n%s", what);
		next = make_timeout_time_ms(1000);
	} else if (time_reached(next)) {
		printf(".");
		next = make_timeout_time_ms(1000);
	}
	return true;
}

static void readline(char *buf, size_t size)
{
	size_t n = 0;
	for (;;) {
		int c = getchar();
		if (c == '\r' || c == '\n') { printf("\n"); break; }
		if ((c == 0x08 || c == 0x7f) && n) { n--; printf("\b \b"); }
		else if (c >= 0x20 && c < 0x7f && n < size - 1) { buf[n++] = (char)c; putchar(c); }
	}
	buf[n] = 0;
}

int main(void)
{
	picocalc_init();
	printf("ping (picocalc-extras net/)\n");

	char why[96];
	if (!net_join(dots, NULL, why, sizeof why)) {
		printf("\n%s\n\nPress a key to go back.", why);
		getchar();
		picocalc_shell_exit();
		return 0;
	}
	printf("\nJoined; this PicoCalc is %s\n", net_address());

	char host[64] = "", line[64];
	for (;;) {
		if (host[0]) printf("\nEnter: %s again, q: quit\n", host);
		else printf("\nAn address or name to ping, q: quit\n");
		printf("> ");
		readline(line, sizeof line);
		if (!strcmp(line, "q") || !strcmp(line, "Q")) break;
		if (line[0]) snprintf(host, sizeof host, "%s", line);
		if (!host[0]) continue;
		int ok = 0;
		unsigned total = 0;
		for (int i = 0; i < 4; i++) {
			unsigned ms;
			if (net_ping(host, 2000, &ms, NULL, NULL, why, sizeof why)) {
				printf("Reply from %s: %u ms\n", host, ms);
				ok++;
				total += ms;
			} else printf("%s\n", why);
			sleep_ms(500);
		}
		printf("%d of 4 answered", ok);
		if (ok) printf(", %u ms on average", total / ok);
		printf("\n");
	}
	net_leave();			/* so the next program joins at once */
	picocalc_shell_exit();
	return 0;
}
