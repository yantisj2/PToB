/* Actual console.c body; fake public UART/kernel boundary, not a driver or scheduler proof. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include "app.h"
static uint64_t now;
static bool tx_enabled;
static int fill_limit=64, fifo_result;
static unsigned callback_fill_calls;
static uint8_t output[16384], input[512];
static size_t output_size,input_size,input_at;
int64_t k_uptime_get(void) { return (int64_t)now; }
int uart_irq_update(const struct device *d) { return 1; }
int uart_irq_is_pending(const struct device *d) { return tx_enabled || input_at<input_size; }
int uart_irq_rx_ready(const struct device *d) { return input_at<input_size; }
int uart_irq_tx_ready(const struct device *d) { return tx_enabled; }
void uart_irq_tx_enable(const struct device *d) { tx_enabled=true; }
void uart_irq_tx_disable(const struct device *d) { tx_enabled=false; }
int uart_fifo_read(const struct device *d,uint8_t *p,size_t n) {
 n=MIN(n,input_size-input_at); memcpy(p,input+input_at,n);input_at+=n;return (int)n;
}
int uart_fifo_fill(const struct device *d,const uint8_t *p,size_t n) {
 ++callback_fill_calls; if(fifo_result) return fifo_result;
 n=MIN(n,(size_t)fill_limit);assert(output_size+n<=sizeof(output));memcpy(output+output_size,p,n);output_size+=n;return (int)n;
}
static void *copy_with_interruption(void *,const void *,size_t);
#define memcpy copy_with_interruption
#include "../src/console.c"
#undef memcpy
static unsigned copy_mode;
static pthread_mutex_t copy_gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t copy_condition=PTHREAD_COND_INITIALIZER;
static bool copy_started,copy_release;
static atomic_int main_reply_result;
static atomic_bool main_reply_entered;
static void *health_producer(void *unused){assert(!r2_console_reply("health",6,100));return NULL;}
static void *main_producer(void *unused){atomic_store(&main_reply_entered,true);atomic_store(&main_reply_result,r2_console_reply("reply",5,100));return NULL;}

static void *copy_with_interruption(void *dst,const void *src,size_t n){
 unsigned mode=copy_mode;copy_mode=0;
 if(mode==4){
  pthread_mutex_lock(&copy_gate);copy_started=true;pthread_cond_broadcast(&copy_condition);
  while(!copy_release)pthread_cond_wait(&copy_condition,&copy_gate);
  pthread_mutex_unlock(&copy_gate);return memcpy(dst,src,n);
 }
 if(mode){
  assert(reply_copying);
  assert(r2_console_reply("nested",6,100)==-ENOBUFS);
  if(mode==1)usb_event(USB_DC_RESET,NULL);
  if(mode==2){tx_enabled=true;interrupt(cdc,NULL);assert(!count);}
  if(mode==3)now=100;
 }
 return memcpy(dst,src,n);
}
static void reset(void) {
 pthread_mutex_destroy(&reply_producer.native);pthread_mutexattr_t attr;pthread_mutexattr_init(&attr);pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);pthread_mutex_init(&reply_producer.native,&attr);pthread_mutexattr_destroy(&attr);fake_isr=false;reply_copying=false;copy_mode=0;memset(&stats,0,sizeof(stats));head=tail=count=0;used=0;discard=false;rx_epoch=seen_epoch=0;
 k_msgq_purge(&commands);stats.dtr=true;now=10;fill_limit=64;fifo_result=0;
 output_size=input_size=input_at=callback_fill_calls=0;tx_enabled=false;
}
static void receive(const char *s,size_t n) {
 assert(n<=sizeof(input));memcpy(input,s,n);input_size=n;input_at=0;
 while(input_at<input_size) interrupt(cdc,NULL);
}
int main(void) {
 reset();fake_isr=true;assert(r2_console_reply("ISR",3,100)==-EWOULDBLOCK);fake_isr=false;fill_limit=3;
 assert(!r2_console_reply("abcdefghij",10,100));tx_enabled=true;
 interrupt(cdc,NULL);assert(output_size==10 && !memcmp(output,"abcdefghij",10) && !count && !tx_enabled);
 reset();fill_limit=0;assert(!r2_console_reply("abc",3,100));tx_enabled=true;
 interrupt(cdc,NULL);assert(!output_size && count==1 && tx[head].offset==0 && callback_fill_calls==1);
 fill_limit=2;interrupt(cdc,NULL);assert(output_size==3 && !count);
 reset();assert(!r2_console_reply("abc",3,100));tx_enabled=true;fifo_result=-EIO;
 interrupt(cdc,NULL);assert(count==1 && !tx_enabled && stats.fifo_errors==1 && !output_size);
 reset();assert(!r2_console_reply("abc",3,100));assert(!r2_console_reply("def",3,100));
 assert(r2_console_reply("ghi",3,100)==-ENOBUFS && stats.tx_drops==1);
 now=100;tx_enabled=true;interrupt(cdc,NULL);assert(count==0 && stats.tx_expired==2 && !output_size);
 reset();stats.dtr=false;assert(!r2_console_reply("abc",3,100));tx_enabled=true;
 interrupt(cdc,NULL);assert(count==1 && !output_size && !tx_enabled);
 reset();receive("status\n",7);struct r2_command cmd;assert(r2_console_get(&cmd) && !strcmp(cmd.text,"status") && cmd.arrival==10);
 for(unsigned i=0;i<R2_COMMAND_SLOTS+1;i++) receive("x\n",2);
 assert(stats.command_drops==1);k_msgq_purge(&commands);
 char huge[300];memset(huge,'x',sizeof(huge));huge[299]='\n';receive(huge,sizeof(huge));
 assert(stats.rx_overflow==1 && !r2_console_get(&cmd));receive("ok\n",3);assert(r2_console_get(&cmd) && !strcmp(cmd.text,"ok"));
 receive("partial",7);usb_event(USB_DC_RESET,NULL);receive("next\n",5);assert(r2_console_get(&cmd) && !strcmp(cmd.text,"next"));
 reset();char bulk[512];memset(bulk,'x',sizeof(bulk));assert(!r2_console_reply(bulk,sizeof(bulk),100));tx_enabled=true;
 interrupt(cdc,NULL);assert(output_size==256 && callback_fill_calls==4 && count==1);
 usb_event(USB_DC_DISCONNECTED,NULL);assert(!count && !stats.dtr && stats.tx_drops==1);
 reset();copy_mode=1;assert(r2_console_reply("old",3,100)==-ENOBUFS);assert(!count&&!reply_copying);
 stats.dtr=true;assert(!r2_console_reply("new",3,100));tx_enabled=true;interrupt(cdc,NULL);assert(output_size==3&&!memcmp(output,"new",3));
 reset();assert(!r2_console_reply("old",3,100));copy_mode=2;assert(!r2_console_reply("new",3,100));tx_enabled=true;interrupt(cdc,NULL);assert(output_size==6&&!memcmp(output,"oldnew",6));
 reset();copy_mode=3;assert(r2_console_reply("late",4,100)==-ENOBUFS);assert(!count&&!reply_copying);
 reset();copy_mode=4;copy_started=copy_release=false;atomic_store(&main_reply_result,123);atomic_store(&main_reply_entered,false);
 pthread_t health_writer,main_writer;assert(!pthread_create(&health_writer,NULL,health_producer,NULL));
 pthread_mutex_lock(&copy_gate);while(!copy_started)pthread_cond_wait(&copy_condition,&copy_gate);pthread_mutex_unlock(&copy_gate);
 assert(!pthread_create(&main_writer,NULL,main_producer,NULL));
 while(!atomic_load(&main_reply_entered)){};
 struct timespec pause={.tv_nsec=10000000};nanosleep(&pause,NULL);assert(atomic_load(&main_reply_result)==123);
 pthread_mutex_lock(&copy_gate);copy_release=true;pthread_cond_broadcast(&copy_condition);pthread_mutex_unlock(&copy_gate);
 pthread_join(health_writer,NULL);pthread_join(main_writer,NULL);assert(!atomic_load(&main_reply_result)&&count==2&&!stats.tx_drops);
 tx_enabled=true;interrupt(cdc,NULL);assert(output_size==11&&!memcmp(output,"healthreply",11));
 puts("R2 actual CDC adapter: partial/zero/error FIFO, bounded callback, overflow, expiry and reconnect PASS");
}
