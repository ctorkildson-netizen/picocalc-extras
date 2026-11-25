/* sshprobe -- SSH from the PicoCalc, the smallest way: an example of
 * picocalc-extras' ssh/ (libssh2 and Mbed TLS over net/, Pico 2 W).
 *
 * Joins Wi-Fi (/ntp/ntp.cfg), logs in to the machine in /ssh/probe.cfg,
 *
 *     host = 192.168.1.20
 *     port = 22
 *     user = chris
 *     password = something
 *     command = uname -a
 *
 * runs the command, and shows what came back, how long each step took, and
 * the most memory libssh2 and Mbed TLS held at once. Esc quits.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include "pico/stdlib.h"
#include "drivers/picocalc.h"
#include "drivers/keyboard.h"
#include "ff.h"
#include "picocalc_shell.h"
#include "net.h"
#include "sshnet.h"

volatile bool user_interrupt = false;

#define DIM "\033[2m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define PLAIN "\033[0m"

static char host[64], user[33], password[65], command[128], port_s[8];

static bool cfg_value(const char *text, const char *key, char *out, size_t size)
{
	size_t kl = strlen(key);
	for (const char *l = text; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		while (*l == ' ' || *l == '\t') l++;
		if (strncmp(l, key, kl)) continue;
		const char *v = l + kl;
		while (*v == ' ' || *v == '\t') v++;
		if (*v != '=') continue;
		v++;
		while (*v == ' ' || *v == '\t') v++;
		size_t n = 0;
		while (v[n] && v[n] != '\n' && v[n] != '\r' && v[n] != '#') n++;
		while (n && (v[n - 1] == ' ' || v[n - 1] == '\t')) n--;
		snprintf(out, size, "%.*s", (int)n, v);
		return true;
	}
	return false;
}

/* memory: the most libssh2 and Mbed TLS held at once */
static size_t base_heap, peak;
static void sample(void)
{
	struct mallinfo m = mallinfo();
	size_t u = (size_t)m.uordblks > base_heap ? (size_t)m.uordblks - base_heap : 0;
	if (u > peak) peak = u;
}

#define AGAIN(expr) ({ int _r; while ((_r = (expr)) == LIBSSH2_ERROR_EAGAIN) { sample(); sleep_ms(1); } sample(); _r; })

static uint32_t t0;
static void step(const char *what) { t0 = to_ms_since_boot(get_absolute_time()); printf("%s... ", what); }
static void done(bool ok) { printf("%s" DIM " %lu ms" PLAIN "\n", ok ? GREEN "ok" PLAIN : RED "failed" PLAIN,
			      (unsigned long)(to_ms_since_boot(get_absolute_time()) - t0)); }

static void why_ssh(LIBSSH2_SESSION *s) { printf(RED "  %s" PLAIN "\n", ssh_error(s)); }

static bool dots(const char *what, void *ctx) { (void)ctx; (void)what; return true; }

static int run(void)
{
	static char text[1024];
	FIL f;
	UINT got = 0;
	if (f_open(&f, "/ssh/probe.cfg", FA_READ) != FR_OK) { printf(RED "No /ssh/probe.cfg" PLAIN "\n"); return 1; }
	f_read(&f, text, sizeof text - 1, &got);
	f_close(&f);
	text[got] = 0;
	cfg_value(text, "host", host, sizeof host);
	cfg_value(text, "user", user, sizeof user);
	cfg_value(text, "password", password, sizeof password);
	if (!cfg_value(text, "command", command, sizeof command)) snprintf(command, sizeof command, "uname -a");
	if (!cfg_value(text, "port", port_s, sizeof port_s)) snprintf(port_s, sizeof port_s, "22");
	if (!host[0] || !user[0]) { printf(RED "probe.cfg needs host and user" PLAIN "\n"); return 1; }

	char why[96];
	step("Wi-Fi");
	bool ok = net_join(dots, NULL, why, sizeof why);
	done(ok);
	if (!ok) { printf(RED "  %s" PLAIN "\n", why); return 1; }
	printf(DIM "  this PicoCalc: %s" PLAIN "\n", net_address());

	step("Connecting");
	net_conn *conn = net_dial(host, (uint16_t)atoi(port_s), NULL, NULL, why, sizeof why);
	done(conn != NULL);
	if (!conn) { printf(RED "  %s" PLAIN "\n", why); return 1; }

	base_heap = (size_t)mallinfo().uordblks;
	LIBSSH2_SESSION *s = ssh_session(conn);
	if (!s) { printf(RED "Out of memory" PLAIN "\n"); return 1; }

	step("Handshake");
	int r = AGAIN(libssh2_session_handshake(s, 0));
	done(r == 0);
	if (r) { why_ssh(s); return 1; }
	printf(DIM "  %s, %s\n  %s, %s" PLAIN "\n",
	       libssh2_session_methods(s, LIBSSH2_METHOD_KEX), libssh2_session_methods(s, LIBSSH2_METHOD_HOSTKEY),
	       libssh2_session_methods(s, LIBSSH2_METHOD_CRYPT_CS), libssh2_session_methods(s, LIBSSH2_METHOD_MAC_CS));
	const unsigned char *fp = (const unsigned char *)libssh2_hostkey_hash(s, LIBSSH2_HOSTKEY_HASH_SHA256);
	printf(DIM "  host key SHA256 ");
	for (int i = 0; fp && i < 8; i++) printf("%02x", fp[i]);
	printf("..." PLAIN "\n");

	step("Password");
	r = AGAIN(libssh2_userauth_password(s, user, password));
	done(r == 0);
	if (r) { why_ssh(s); return 1; }

	LIBSSH2_CHANNEL *ch;
	step("Command");
	while (!(ch = libssh2_channel_open_session(s)) && libssh2_session_last_errno(s) == LIBSSH2_ERROR_EAGAIN) { sample(); sleep_ms(1); }
	r = ch ? AGAIN(libssh2_channel_exec(ch, command)) : -1;
	done(r == 0);
	if (r) { why_ssh(s); return 1; }
	static char buf[512];
	for (int idle = 0; idle < 5000 && !libssh2_channel_eof(ch); ) {
		ssize_t n = libssh2_channel_read(ch, buf, sizeof buf);
		sample();
		if (n > 0) { fwrite(buf, 1, (size_t)n, stdout); idle = 0; }
		else if (n == LIBSSH2_ERROR_EAGAIN || n == 0) { sleep_ms(1); idle++; }
		else break;
	}
	AGAIN(libssh2_channel_close(ch));
	libssh2_channel_free(ch);
	AGAIN(libssh2_session_disconnect(s, "bye"));
	libssh2_session_free(s);
	net_conn_close(conn);
	printf(GREEN "Done." PLAIN DIM " Most memory at once: %u KB" PLAIN "\n", (unsigned)(peak / 1024));
	return 0;
}

int main(void)
{
	picocalc_init();
	printf("\033[2J\033[HSSH probe " "1.0" "\n\n");
	run();
	printf(DIM "\nEsc: quit" PLAIN);
	for (;;) {
		int k = keyboard_get_key();
		if (k == KEY_ESC || k == 0x1b) break;
	}
	net_leave();
	picocalc_shell_exit();
}
