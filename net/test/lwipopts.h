/* lwIP for the host test: net/'s own settings, with a loopback interface
   and without DHCP and DNS (there's no network here) */
#include "../config/lwipopts.h"
#undef LWIP_DHCP
#define LWIP_DHCP 0
#undef LWIP_DNS
#define LWIP_DNS 0
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_NETIF_LOOPBACK 1
#define LWIP_HAVE_LOOPIF 1
