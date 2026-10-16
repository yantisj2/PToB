/* SPDX-License-Identifier: Apache-2.0
 * Stock CDC UART FIFO API; no transfer reset, rearm hook or stack patch.
 */
#include "app.h"
#include "health.h"
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#define R2_LOCK_UNIT 5u
#include "health_lock.h"
static const struct device *const cdc=DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
K_MSGQ_DEFINE(commands,sizeof(struct r2_command),R2_COMMAND_SLOTS,8);
struct tx_slot { char data[R2_REPLY_BYTES]; size_t length, offset; uint64_t expiry; bool note; };
static struct tx_slot tx[R2_TX_SLOTS];
static unsigned head,tail,count;
K_MUTEX_DEFINE(reply_producer); /* Thread-only producers; never held by USB callbacks. */
static bool reply_copying; /* One unpublished producer reservation. */
static struct k_spinlock lock;
static struct r2_io stats={.usb_rc=-EAGAIN};
static struct r2_command incoming;
static size_t used;
static bool discard;
static uint32_t rx_epoch, seen_epoch;
K_THREAD_STACK_DEFINE(console_stack,2048);
static struct k_thread console_thread;
static void add(uint32_t *value, uint32_t delta)
{ *value=UINT32_MAX-*value<delta?UINT32_MAX:*value+delta; }
static void usb_event(enum usb_dc_status_code event,const uint8_t *parameter)
{
  k_spinlock_key_t key=k_spin_lock(&lock); add(&stats.usb_events,1);
  if(event==USB_DC_DISCONNECTED || event==USB_DC_RESET) {
    ++rx_epoch;
    for(unsigned i=0;i<count;i++)add(tx[(head+i)%R2_TX_SLOTS].note?&stats.note_drops:&stats.tx_drops,1);
    count=0; head=tail=0;
    stats.dtr=false; k_msgq_purge(&commands);
  }
  k_spin_unlock(&lock,key);
}
static void rx_bytes(const uint8_t *data,size_t size)
{
  if(seen_epoch!=rx_epoch) { seen_epoch=rx_epoch; used=0; discard=false; }
  for(size_t i=0;i<size;i++) {
    uint8_t c=data[i];
    if(!used && !discard) incoming.arrival=k_uptime_get();
    if(c=='\n') {
      if(!discard && used) {
        incoming.text[used]=0;
        if(k_msgq_put(&commands,&incoming,K_NO_WAIT)) add(&stats.command_drops,1);
      }
      used=0; discard=false;
    } else if(c<32 || c>126 || used==sizeof(incoming.text)-1) {
      if(!discard) add(&stats.rx_overflow,1);
      discard=true;
    } else if(!discard) incoming.text[used++]=(char)c;
  }
}
static void interrupt(const struct device *device,void *context)
{
  /* Bound each callback to four USB packets. Formatting/parsing/auth stay in main. */
  for(unsigned turn=0;turn<4 && uart_irq_update(device) && uart_irq_is_pending(device);turn++) {
    bool progress=false;
    if(uart_irq_rx_ready(device)) {
      uint8_t buffer[64]; int got=uart_fifo_read(device,buffer,sizeof(buffer));
      k_spinlock_key_t key=k_spin_lock(&lock);
      if(got>0) { add(&stats.rx_bytes,(uint32_t)got); rx_bytes(buffer,(size_t)got); progress=true; }
      else if(got<0) add(&stats.fifo_errors,1);
      k_spin_unlock(&lock,key);
    }
    if(uart_irq_tx_ready(device)) {
      k_spinlock_key_t key=k_spin_lock(&lock);
      if(!stats.dtr || !count) uart_irq_tx_disable(device);
      else {
        struct tx_slot *s=&tx[head];
        if((uint64_t)k_uptime_get()>=s->expiry) {
          add(s->note?&stats.note_drops:&stats.tx_expired,1);
          head=(head+1)%R2_TX_SLOTS; --count; progress=true;
        } else {
          size_t n=MIN(s->length-s->offset,64);
          int sent=uart_fifo_fill(device,(const uint8_t *)s->data+s->offset,n);
          /* The pinned public FIFO copies accepted bytes. Keep any unaccepted tail. */
          if(sent>0 && sent<=(int)n) {
            s->offset+=(size_t)sent; add(&stats.tx_bytes,(uint32_t)sent); progress=true;
            if(s->offset==s->length) { head=(head+1)%R2_TX_SLOTS; --count; }
          } else if(sent<0 || sent>(int)n) { add(&stats.fifo_errors,1); uart_irq_tx_disable(device); }
        }
        if(!count) uart_irq_tx_disable(device);
      }
      k_spin_unlock(&lock,key);
    }
    if(!progress) break;
  }
}
static void worker(void *a,void *b,void *c)
{
  int rc=device_is_ready(cdc)?uart_irq_callback_user_data_set(cdc,interrupt,NULL):-ENODEV;
  if(!rc) rc=usb_enable(usb_event);
  k_spinlock_key_t key=k_spin_lock(&lock); stats.usb_rc=rc; k_spin_unlock(&lock,key);
  if(rc) return;
  uart_irq_rx_enable(cdc);
  for(;;) {
    uint32_t dtr=0; rc=uart_line_ctrl_get(cdc,UART_LINE_CTRL_DTR,&dtr);
    key=k_spin_lock(&lock);
    bool live=!rc && dtr;
    if(live!=stats.dtr) { stats.dtr=live; add(&stats.dtr_changes,1); }
    while(count && (uint64_t)k_uptime_get()>=tx[head].expiry) {
      add(tx[head].note?&stats.note_drops:&stats.tx_expired,1);
      head=(head+1)%R2_TX_SLOTS; --count;
    }
    if(live && count) uart_irq_tx_enable(cdc); else uart_irq_tx_disable(cdc);
    k_spin_unlock(&lock,key);
    r2_health_console();
    k_sleep(K_MSEC(10));
  }
}
void r2_console_start(void)
{
  k_thread_create(&console_thread,console_stack,K_THREAD_STACK_SIZEOF(console_stack),worker,
    NULL,NULL,NULL,10,0,K_NO_WAIT);
  k_thread_name_set(&console_thread,"r2_console");
}
bool r2_console_get(struct r2_command *out) { return k_msgq_get(&commands,out,K_NO_WAIT)==0; }
static int reply_reserved(const char *bytes,size_t size,uint64_t expiry)
{
  if(!size || size>R2_REPLY_BYTES) return -EMSGSIZE;
  k_spinlock_key_t key=k_spin_lock(&lock);
  if(reply_copying || count==R2_TX_SLOTS || (uint64_t)k_uptime_get()>=expiry) {
    add(&stats.tx_drops,1); k_spin_unlock(&lock,key); return -ENOBUFS;
  }
  struct tx_slot *s=&tx[tail]; uint32_t epoch=rx_epoch;
  reply_copying=true;
  k_spin_unlock(&lock,key);
  /* Main/health are thread callers. The reserved tail is invisible to the
   * consumer until published; no interrupt masking during the bulk copy.
   * USB reset may clear the queue, but cannot admit another producer until
   * this reservation is released. The epoch then discards the old reply. */
  memcpy(s->data,bytes,size);
  key=k_spin_lock(&lock);reply_copying=false;
  if(epoch!=rx_epoch || (uint64_t)k_uptime_get()>=expiry){
    add(&stats.tx_drops,1);k_spin_unlock(&lock,key);return -ENOBUFS;
  }
  s->length=size; s->offset=0; s->expiry=expiry;s->note=false;
  tail=(tail+1)%R2_TX_SLOTS; ++count;
  k_spin_unlock(&lock,key);
  return 0;
}
int r2_console_reply(const char *bytes,size_t size,uint64_t expiry)
{
  if(k_is_in_isr())return -EWOULDBLOCK;
  if(!size||size>R2_REPLY_BYTES)return -EMSGSIZE;
  uint64_t now=(uint64_t)k_uptime_get();
  /* The health writer can be preempted by a main-thread command reply during
   * its copy. Priority inheritance lets that writer finish without masking
   * radio IRQs. Wait only within this reply's original expiry, then recheck. */
  int rc=now>=expiry?-EAGAIN:k_mutex_lock(&reply_producer,K_MSEC(MIN(expiry-now,R2_REPLY_MS)));
  if(rc){k_spinlock_key_t key=k_spin_lock(&lock);add(&stats.tx_drops,1);k_spin_unlock(&lock,key);return -ENOBUFS;}
  rc=reply_reserved(bytes,size,expiry);
  (void)k_mutex_unlock(&reply_producer);
  return rc;
}
int r2_console_note(const char *bytes,size_t size,uint64_t expiry)
{
  if(k_is_in_isr())return -EWOULDBLOCK;
  if(!size||size>512)return -EMSGSIZE;
  if(k_mutex_lock(&reply_producer,K_NO_WAIT))return -EAGAIN;
  k_spinlock_key_t key=k_spin_lock(&lock);
  if(!stats.dtr||count||reply_copying||(uint64_t)k_uptime_get()>=expiry){
    k_spin_unlock(&lock,key);(void)k_mutex_unlock(&reply_producer);return -EAGAIN;
  }
  unsigned index=tail;uint32_t epoch=rx_epoch;reply_copying=true;
  k_spin_unlock(&lock,key);
  memcpy(tx[index].data,bytes,size);
  key=k_spin_lock(&lock);reply_copying=false;
  int rc=-EAGAIN;
  if(epoch==rx_epoch&&stats.dtr&&!count&&(uint64_t)k_uptime_get()<expiry){
    tx[index].length=size;tx[index].offset=0;tx[index].expiry=expiry;tx[index].note=true;
    tail=(tail+1)%R2_TX_SLOTS;++count;rc=0;
  }
  k_spin_unlock(&lock,key);(void)k_mutex_unlock(&reply_producer);return rc;
}
void r2_console_snapshot(struct r2_io *out)
{ k_spinlock_key_t key=k_spin_lock(&lock); *out=stats; k_spin_unlock(&lock,key); }
