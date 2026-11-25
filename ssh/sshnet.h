/* sshnet.h -- SSH for a PicoCalc program: a libssh2 session over one of
 * net/'s connections (see ssh/README.md).
 *
 *     net_conn *c = net_dial(host, 22, NULL, NULL, why, sizeof why);
 *     LIBSSH2_SESSION *s = ssh_session(c);
 *     while ((r = libssh2_session_handshake(s, 0)) == LIBSSH2_ERROR_EAGAIN)
 *         ...keep the screen and keyboard going...
 *
 * The session is non-blocking: every libssh2 call returns
 * LIBSSH2_ERROR_EAGAIN until it can go on, so the program calls it again
 * from its own loop (SSH_AGAIN() does that for a program with nothing else
 * to do meanwhile). Free the session with libssh2_session_free(), then close
 * the connection.
 */
#ifndef SSHNET_H
#define SSHNET_H

#include <libssh2.h>
#include "net.h"

/* a new session on c (libssh2_init() is done the first time); NULL if out
   of memory */
LIBSSH2_SESSION *ssh_session(net_conn *c);

/* run a libssh2 call until it stops saying "again"; its result */
#define SSH_AGAIN(expr) ({ int _r; while ((_r = (expr)) == LIBSSH2_ERROR_EAGAIN) sleep_ms(1); _r; })

/* what went wrong last, in libssh2's words */
const char *ssh_error(LIBSSH2_SESSION *s);

#endif
