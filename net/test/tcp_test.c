/* tcp_test.c -- net/tcp.c on a PC, through a loopback interface: listen,
 * dial, accept, 100 KB with flow control, the pool limit, hang-ups, and the
 * old one-connection calls. Stubs stand in for the Pico SDK and for net.c
 * (joining is a no-op here). Run: make check */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "pico/stdlib.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "net.h"
uint32_t sys_now(void){return (uint32_t)(get_absolute_time()/1000);}
unsigned lwip_port_rand(void){return 4242;}
void sleep_ms(unsigned ms){ netif_poll_all(); sys_check_timeouts(); usleep(ms*1000); netif_poll_all(); }
void sleep_us(unsigned us){ sleep_ms(0); usleep(us); }
bool net_join(net_progress a, void *b, char *w, size_t n){(void)a;(void)b;(void)w;(void)n;return true;}
bool net_lookup(const char *h, ip_addr_t *ip, net_progress a, void *b, char *w, size_t n){(void)a;(void)b;if(ipaddr_aton(h,ip))return true;snprintf(w,n,"bad");return false;}
static int fails;
#define CHECK(c,m) do{ if(!(c)){printf("FAIL: %s\n",m);fails++;} else printf("ok: %s\n",m);}while(0)
static bool pump(net_conn *c, size_t want, uint8_t *buf){ size_t got=0; for(int i=0;i<2000&&got<want;i++){ got+=net_conn_read(c,buf+got,want-got); sleep_ms(1);} return got==want; }
int main(void){
  lwip_init(); char why[96];
  net_listener *l=net_listen(2121,NULL,NULL,why,sizeof why); CHECK(l,"listen on 2121");
  net_listener *l0=net_listen(0,NULL,NULL,why,sizeof why); CHECK(l0 && net_listener_port(l0)>1024,"listen on any port");
  printf("   (any port: %u)\n", net_listener_port(l0));
  net_listener *dup=net_listen(2121,NULL,NULL,why,sizeof why); CHECK(!dup && strstr(why,"in use"),"second listen on 2121 refused");
  net_conn *a=net_dial("127.0.0.1",2121,NULL,NULL,why,sizeof why); CHECK(a,"dial 2121");
  net_conn *b=NULL; for(int i=0;i<200&&!b;i++){ b=net_accept(l); sleep_ms(1);} CHECK(b,"accept it");
  CHECK(!strcmp(net_conn_peer(b),"127.0.0.1"),"peer address");
  static uint8_t big[100000], back[100000]; for(size_t i=0;i<sizeof big;i++) big[i]=(uint8_t)(i*7+i/251);
  CHECK(net_conn_write(a,(const uint8_t*)"220 hello\r\n",11),"write a line");
  uint8_t line[32]={0}; CHECK(pump(b,11,line) && !memcmp(line,"220 hello\r\n",11),"read the line the other side");
  /* 100 KB one way while reading: window/ring flow control */
  size_t sent=0, got=0; int spins=0;
  while((sent<sizeof big || got<sizeof big) && spins<20000){
    if(sent<sizeof big){ size_t m=sizeof big-sent; if(m>1000)m=1000; if(net_conn_write(a,big+sent,m)) sent+=m; }
    got+=net_conn_read(b,back+got,sizeof back-got); sleep_ms(0); spins++; }
  CHECK(got==sizeof big && !memcmp(big,back,sizeof big),"100 KB through, in order");
  /* a third connection: pool of 2 is full -> dial fails cleanly */
  net_conn *c=net_dial("127.0.0.1",2121,NULL,NULL,why,sizeof why); CHECK(!c,"third connection refused (pool of 2)"); printf("   (%s)\n",why);
  net_conn_close(a);
  bool hung=false; for(int i=0;i<500&&!hung;i++){ uint8_t x[8]; net_conn_read(b,x,8); hung=net_conn_closed_by_host(b); sleep_ms(1);} CHECK(hung,"other side sees the hang-up");
  net_conn_close(b);
  /* old one-connection calls still work */
  CHECK(net_connect("127.0.0.1",2121,NULL,NULL,why,sizeof why),"net_connect (old call)");
  net_conn *d=NULL; for(int i=0;i<200&&!d;i++){ d=net_accept(l); sleep_ms(1);} CHECK(d,"accepted");
  net_write((const uint8_t*)"xyz",3); uint8_t t[4]={0}; CHECK(pump(d,3,t)&&!memcmp(t,"xyz",3),"net_write reaches it");
  net_conn_write(d,(const uint8_t*)"ok",2); size_t k=0; for(int i=0;i<200&&k<2;i++){k+=net_read(t+k,2-k); sleep_ms(1);} CHECK(k==2&&!memcmp(t,"ok",2),"net_read gets the answer");
  net_close_all(); CHECK(!net_open(),"all closed");
  CHECK(!net_dial("127.0.0.1",2121,NULL,NULL,why,sizeof why),"nothing listening after close_all"); printf("   (%s)\n",why);
  printf("%d failed\n",fails); return fails;
}
