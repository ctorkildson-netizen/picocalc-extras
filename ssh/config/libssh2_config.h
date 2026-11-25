/* libssh2_config.h -- libssh2 on the PicoCalc (RP2350, Pico SDK, newlib, no
 * OS, no sockets). libssh2 never touches a socket here: sshnet.c gives it
 * send and receive callbacks over net/'s connections. */
#define HAVE_UNISTD_H
#define HAVE_INTTYPES_H
#define HAVE_SYS_TIME_H
#define HAVE_GETTIMEOFDAY
#define HAVE_STRTOLL
#define HAVE_SNPRINTF
#define HAVE_SYS_UIO_H		/* (config/sys: struct iovec) */
#define HAVE_SYS_SOCKET_H	/* (config/sys: recv, send) */
#define LIBSSH2_MBEDTLS 1
