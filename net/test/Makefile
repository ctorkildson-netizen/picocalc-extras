# net/test -- tcp.c against real lwIP on a PC, over loopback:
#   make LWIP=/path/to/pico-sdk/lib/lwip
LWIP ?= $(PICO_SDK_PATH)/lib/lwip
SRC = $(wildcard $(LWIP)/src/core/*.c) $(wildcard $(LWIP)/src/core/ipv4/*.c) $(LWIP)/src/netif/ethernet.c
CFLAGS = -w -I. -I.. -I$(LWIP)/src/include -I$(LWIP)/contrib/ports/unix/port/include

tcp_test: tcp_test.c ../tcp.c
	$(CC) $(CFLAGS) -o $@ tcp_test.c ../tcp.c $(SRC)

check: tcp_test
	./tcp_test

clean:
	rm -f tcp_test
