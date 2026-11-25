# ssh/ -- SSH for PicoCalc programs (Pico 2 W)

[libssh2](https://libssh2.org) 1.11.1 with Mbed TLS (the copy in the Pico
SDK), running over [net/](../net/)'s connections. libssh2 never sees a
socket: its sends and receives go through `net_conn_write()` and
`net_conn_read()`, so it gets on with Wi-Fi the same way telnet does.

On a PicoCalc logging in to a Linux machine on the home network, the
handshake (key exchange and checking the host's signature, the heavy part)
takes about 350 ms, a password login 25 ms, and libssh2 and Mbed TLS hold
about 80 KB of memory at most.

## Using it in a program

The program has libssh2 as a submodule of its own, pinned to the
`libssh2-1.11.1` tag, and brings it in after net/:

```cmake
include(path/to/picocalc-extras/fatfs/fatfs.cmake)
picocalc_fatfs(path/to/picocalc-text-starter)
include(path/to/picocalc-extras/net/net.cmake)
picocalc_net(path/to/picocalc-shell)
include(path/to/picocalc-extras/ssh/ssh.cmake)
picocalc_ssh(path/to/libssh2)
target_link_libraries(myprogram picocalc_ssh ...)
```

```c
#include "sshnet.h"

net_conn *c = net_dial("192.168.1.20", 22, NULL, NULL, why, sizeof why);
LIBSSH2_SESSION *s = ssh_session(c);              // non-blocking, over c

int r;
while ((r = libssh2_session_handshake(s, 0)) == LIBSSH2_ERROR_EAGAIN)
    ;                                            // keep the screen going here
if (r) printf("%s\n", ssh_error(s));

// the host key, to remember the first time and compare after
const char *fp = libssh2_hostkey_hash(s, LIBSSH2_HOSTKEY_HASH_SHA256);

r = SSH_AGAIN(libssh2_userauth_password(s, "chris", password));
LIBSSH2_CHANNEL *ch = ...libssh2_channel_open_session(s)...
SSH_AGAIN(libssh2_channel_request_pty_ex(ch, "vt100", 5, NULL, 0, 80, 24, 0, 0));
SSH_AGAIN(libssh2_channel_shell(ch));
// then libssh2_channel_read() / _write() from the program's loop
```

Every libssh2 call on the session returns `LIBSSH2_ERROR_EAGAIN` until it
can go on, and the program calls it again: from its own loop, so the
keyboard and screen keep going, or with `SSH_AGAIN()` when it has nothing
else to do meanwhile. Free the session with `libssh2_session_free()`, then
`net_conn_close()` the connection.

The rest is libssh2's own API: [its documentation](https://libssh2.org/docs.html).
`examples/sshprobe/main.c` is the whole thing in about 150 lines.

## What's in it

- `ssh.cmake`: `picocalc_ssh()`, libssh2's sources with the files below.
- `sshnet.c`, `sshnet.h`: `ssh_session()`, a session whose send and receive
  are a net/ connection (carried in the session's "abstract" pointer, so a
  program can have more than one); `ssh_error()`; `SSH_AGAIN()`.
- `config/libssh2_config.h`: libssh2 on newlib with no sockets.
- `config/mbedtls_config.h`: only what SSH needs from Mbed TLS: ECDH and
  ECDSA (P-256/384/521), RSA, AES, SHA-1/2, keys in PEM. Random numbers come
  from the RP2350's TRNG, and SHA-256 from its hardware. The build stops if
  Mbed TLS ends up with a different config.
- `config/sys/`, `sockets.c`: the few socket headers and functions libssh2
  expects, which here only fail (its sends and receives are replaced) or,
  for blocking mode's waiting, say "ready" after a millisecond.

## sshprobe

`examples/sshprobe` joins Wi-Fi, logs in to the machine in `/ssh/probe.cfg`,
runs one command, and shows what came back, how long each step took and
the most memory used:

    host = 192.168.1.20
    port = 22
    user = chris
    password = something
    command = uname -a; uptime

The password is in that file as plain text, so it's for trying SSH out,
not for keeping. Copy `sshprobe.uf2` to the card's `pico2-apps` folder.

## Not yet

- Logging in with a key (libssh2 can, from a key in memory; Mbed TLS reads
  PEM keys, not OpenSSH's newer format).
- ed25519 host keys and curve25519 key exchange: Mbed TLS 3.6 has neither,
  so libssh2 offers ECDSA and RSA host keys and ECDH/DH key exchange. Every
  OpenSSH server has those too.
