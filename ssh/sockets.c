/* sockets.c -- what libssh2 would use for a socket, on a PicoCalc that has
 * none: its own send and receive (always replaced by sshnet.c's callbacks,
 * so these only fail) and select() (only blocking mode waits with it: say
 * "ready" after a millisecond and let libssh2 try again). */
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include "pico/time.h"

ssize_t recv(int s, void *buf, size_t len, int flags)
{
	(void)s; (void)buf; (void)len; (void)flags;
	errno = ENOTCONN;
	return -1;
}

ssize_t send(int s, const void *buf, size_t len, int flags)
{
	(void)s; (void)buf; (void)len; (void)flags;
	errno = ENOTCONN;
	return -1;
}

int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t)
{
	(void)n; (void)r; (void)w; (void)e; (void)t;
	sleep_ms(1);
	return 1;
}
