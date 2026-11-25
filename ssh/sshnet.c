/* sshnet.c -- libssh2 over net/'s connections (see sshnet.h). libssh2 asks
 * for bytes through its send and receive callbacks; the connection rides in
 * the session's "abstract" pointer, so a program can have several. */
#include <errno.h>
#include "pico/stdlib.h"
#include "sshnet.h"
#include "mbedtls/build_info.h"

#ifndef MBEDTLS_ENTROPY_HARDWARE_ALT
#error "Mbed TLS isn't using ssh/config/mbedtls_config.h (put ssh/config ahead of Mbed TLS's include folder)"
#endif

static ssize_t send_cb(libssh2_socket_t s, const void *b, size_t n, int flags, void **abstract)
{
	(void)s; (void)flags;
	net_conn *c = *abstract;
	return net_conn_write(c, b, n) ? (ssize_t)n : -ECONNRESET;
}

static ssize_t recv_cb(libssh2_socket_t s, void *b, size_t n, int flags, void **abstract)
{
	(void)s; (void)flags;
	net_conn *c = *abstract;
	size_t k = net_conn_read(c, b, n);
	if (k) return (ssize_t)k;
	if (net_conn_closed_by_host(c) || !net_conn_open(c)) return 0;	/* the end */
	return -EAGAIN;
}

LIBSSH2_SESSION *ssh_session(net_conn *c)
{
	static bool ready;
	if (!ready) {
		if (libssh2_init(0)) return NULL;
		ready = true;
	}
	LIBSSH2_SESSION *s = libssh2_session_init_ex(NULL, NULL, NULL, c);
	if (!s) return NULL;
	libssh2_session_callback_set2(s, LIBSSH2_CALLBACK_SEND, (libssh2_cb_generic *)send_cb);
	libssh2_session_callback_set2(s, LIBSSH2_CALLBACK_RECV, (libssh2_cb_generic *)recv_cb);
	libssh2_session_set_blocking(s, 0);
	return s;
}

const char *ssh_error(LIBSSH2_SESSION *s)
{
	char *m = NULL;
	if (!s) return "no session (out of memory?)";
	libssh2_session_last_error(s, &m, NULL, 0);
	return m && *m ? m : "unknown";
}
