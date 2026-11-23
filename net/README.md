# net/ -- Wi-Fi for PicoCalc programs (Pico 2 W)

Joins the Wi-Fi network named in `/ntp/ntp.cfg`, and gives a program TCP
connections (outgoing, and incoming on ports it listens on), name lookups
and ping. It came out of
[calcterm](https://github.com/ctorkildson-netizen/calcterm)'s telnet.

The network is set up in one place on the card: `/ntp/ntp.cfg`, the file
Blair Leduc's [picocalc-shell](https://github.com/BlairLeduc/picocalc-shell)
reads for its `NTP` command, read here with the shell's own parser.

    ssid = MyNetwork
    password = secret
    country = US

## Using it in a program

```cmake
set(PICO_BOARD pico2_w)                                  # before pico_sdk_init()
include(path/to/picocalc-extras/fatfs/fatfs.cmake)
picocalc_fatfs(path/to/picocalc-text-starter)            # ntp.cfg is on the card
include(path/to/picocalc-extras/net/net.cmake)
picocalc_net(path/to/picocalc-shell)                     # for ntp.c, the shell's parser
target_link_libraries(myprogram picocalc_net ...)
```

```c
#include "net.h"

char why[96];
fs_init();                                   // (or picocalc_init())
if (!net_join(NULL, NULL, why, sizeof why))  // or a progress callback, for "Joining..."
    printf("%s\n", why);                     // "Wrong password for MyNetwork", say

unsigned ms;
if (net_ping("192.168.1.50", 2000, &ms, NULL, NULL, why, sizeof why))
    printf("%u ms\n", ms);

if (net_connect("bbs.example.org", 23, NULL, NULL, why, sizeof why)) {
    net_write((const uint8_t *)"hello\r\n", 7);
    uint8_t buf[256];
    size_t n = net_read(buf, sizeof buf);   // what's arrived so far (0 if nothing yet)
    net_close();
}

net_leave();                                 // before the program ends
```

[`net.h`](net.h) has the rest: `net_lookup()`, `net_waiting()`,
`net_open()` and `net_closed_by_host()`.

**Several connections, and listening** (a server, say): each connection is
a `net_conn`, from a pool of `PICOCALC_NET_CONNS` (2 unless the program sets
it in CMake before `picocalc_net()`; each takes a 16 KB receive buffer).

```c
net_listener *l = net_listen(21, NULL, NULL, why, sizeof why);   // 0: any free port
for (;;) {
    net_conn *c = net_accept(l);          // doesn't wait: NULL if nobody yet
    if (c) {
        net_conn_write(c, (const uint8_t *)"220 Hello\r\n", 11);
        ...                               // net_conn_read(), net_conn_closed_by_host()
        net_conn_close(c);
    }
    // ... the program's other work
}

net_conn *d = net_dial("192.168.1.50", 80, NULL, NULL, why, sizeof why);
```

The one-connection calls (`net_connect()`, `net_read()` ...) use a
connection from the same pool, so they and these mix freely.

**Every call waits** until it's done, calling the progress callback (if
any) now and then with what's happening; returning false from it stops
the wait. lwIP runs in the background meanwhile, so data that arrives
between reads waits in a 16 KB buffer, and a fast host is held back by
TCP rather than anything being lost.

**Leave before the program ends.** `net_leave()` tells the router the
PicoCalc has gone. Without it, the router keeps the old connection for a
minute or so and turns the next program away until that runs out.

**With Bluetooth as well** (picocalc-extras' `bluetooth/`), call
`net_radio()` before `btkbd_init()`, and set `BTKBD_WITH_WIFI` before
including `bluetooth.cmake`; the two share the radio.

**lwIP's settings** come from [`config/lwipopts.h`](config/lwipopts.h):
IPv4, DHCP, DNS, TCP and raw ICMP for ping. A program with its own sets
`PICOCALC_NET_OWN_LWIPOPTS` before calling `picocalc_net()`; its
`lwipopts.h` needs `LWIP_RAW 1` for ping and a `TCP_WND` under 16 KB.

## The example

[`examples/ping`](../examples/ping) joins the network, then pings
whatever you type, four times, as ping does on a PC.

## Limits

- Connections from a fixed pool (`PICOCALC_NET_CONNS`); a newcomer to a
  listener when none is free is turned away.
- IPv4 only. Names ending in `.local` aren't looked up; use the address.

## Tests

[`test/`](test/) runs `tcp.c` against real lwIP on a PC, over a loopback
interface: `make check LWIP=/path/to/pico-sdk/lib/lwip`.
