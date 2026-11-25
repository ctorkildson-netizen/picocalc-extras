/* sys/socket.h for libssh2 on the PicoCalc (newlib has none). There are no
 * sockets: sshnet.c's callbacks replace libssh2's own send and receive, and
 * ssh/sockets.c has these only so it links (they fail). */
#ifndef PICOCALC_SYS_SOCKET_H
#define PICOCALC_SYS_SOCKET_H
#include <sys/types.h>
typedef int socklen_t;
ssize_t recv(int s, void *buf, size_t len, int flags);
ssize_t send(int s, const void *buf, size_t len, int flags);
#endif
