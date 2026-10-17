/* SPDX-License-Identifier: Apache-2.0
 * Public UART async API owns DMA and RX shutdown. No PM RXTO busy-wait,
 * register access or application TIMER/PPI reservation. AIDTIME uses async TX.
 */
#include "time_service.h"
#include "ui.h"
#include "capture.h"
#include "rc_shadow.h"
#ifdef R2_DC_PROFILE
#include "dc_gnss.h"
#include "dc_gnss_transport.h"
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#ifdef R2_DC_PROFILE
static uint8_t bridge_tx[DC_GNSS_FRAME_MAX];static size_t bridge_size;
static atomic_t bridge_state;
static uint32_t bridge_epoch;
static int64_t bridge_at,bridge_abort_at;
static bool bridge_aborting;
static void bridge_poll(int64_t now);
#endif
BUILD_ASSERT(DT_PROP(DT_NODELABEL(uart0),zephyr_deferred_init));
static const struct device *const uart=DEVICE_DT_GET(DT_NODELABEL(uart0));
static const struct gpio_dt_spec enable=GPIO_DT_SPEC_GET(DT_NODELABEL(gnss_enable),gpios);
static const struct gpio_dt_spec vext=GPIO_DT_SPEC_GET(DT_NODELABEL(vext_enable),gpios);
static const struct gpio_dt_spec pps=GPIO_DT_SPEC_GET(DT_NODELABEL(pps),gpios);
static struct gpio_callback edge_callback;
static uint8_t rx[2][128];
static atomic_t used[2],disabled=1,stop_requested,error,initializing;
static bool callback_added,uart_ready,powering,opened,closed=true;
static int64_t power_ready,startup_deadline;
static bool stop_sent,rail_kept;
K_THREAD_STACK_DEFINE(gnss_stack,2048);
static struct k_thread gnss_thread;
static uint8_t aid_tx[64];static size_t aid_size;
static atomic_t aid_tx_state; /* 0 idle,1 DMA-owned,2 done,3 aborted */
static bool aid_abort_sent;static int64_t aid_at,aid_abort_at;
static struct r2_gnss_aid_status aid_status;
static char aid_reply[24];static unsigned aid_reply_len;
static void aid_poll(int64_t now);
static int aid_format(uint64_t utc){
 uint64_t seconds=utc/1000000;
 if(seconds<946684800ULL||seconds>=4102444800ULL)return 0; /* 2000..2099 */
 uint32_t days=(uint32_t)(seconds/86400),second=seconds%86400,year=1970,month=1;
 for(;;year++){unsigned n=365+(year%4==0&&(year%100!=0||year%400==0));if(days<n)break;days-=n;}
 static const uint8_t lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
 for(;month<=12;month++){unsigned n=lengths[month-1]+(month==2&&year%4==0&&(year%100!=0||year%400==0));if(days<n)break;days-=n;}
 int n=snprintf((char *)aid_tx,sizeof aid_tx,"$AIDTIME,%04u,%02u,%02u,%02u,%02u,%02u,%03u\r\n",year,month,days+1,second/3600,(second/60)%60,second%60,(unsigned)(utc/1000%1000));
 return n>0&&(size_t)n<sizeof aid_tx?n:0;
}
void r2_gnss_aid(uint64_t utc,uint64_t at,bool valid,bool local_fix){
 int64_t now=r2_time_now_us();
#ifdef R2_DC_PROFILE
 if(atomic_get(&bridge_state))return;
#endif
 if(!valid||local_fix||now<0||(uint64_t)now<at||(uint64_t)now-at>=1500000||
    utc>UINT64_MAX-((uint64_t)now-at)||!uart_ready||atomic_get(&initializing)||atomic_get(&disabled)||
    atomic_get(&stop_requested)||atomic_get(&error)||atomic_get(&aid_tx_state)||
    (aid_status.attempts&&(now<aid_at||now-aid_at<60000000)))return;
 utc+=(uint64_t)now-at;int n=aid_format(utc);if(!n)return;
 aid_size=(size_t)n;aid_at=now;aid_abort_sent=false;aid_reply_len=0;
 if(aid_status.attempts<UINT32_MAX)++aid_status.attempts;
 aid_status.utc_ms=utc/1000;aid_status.ack=1;atomic_set(&aid_tx_state,1);
 aid_status.result=uart_tx(uart,aid_tx,aid_size,100000);
 if(aid_status.result){atomic_clear(&aid_tx_state);aid_status.ack=0;}
}
void r2_gnss_aid_status(struct r2_gnss_aid_status *out){*out=aid_status;out->busy=atomic_get(&aid_tx_state)==1;}
/* Called by the time owner after queue dequeue, never the UART ISR. Only this
 * adapter writes UART; a checksum-valid response within2s belongs to its aid. */
void r2_gnss_observe(const uint8_t *bytes,size_t size,int64_t received){
 if(aid_status.ack!=1||received<aid_at||received-aid_at>=2000000)return;
 for(size_t i=0;i<size;i++){
  char ch=(char)bytes[i];if(ch=='$'){aid_reply_len=0;aid_reply[aid_reply_len++]=ch;}
  else if(ch=='\r'||ch=='\n'){
   aid_reply[aid_reply_len]=0;
   if(!strcmp(aid_reply,"$OK*04"))aid_status.ack=2;
   else if(!strcmp(aid_reply,"$FAIL,0*1E")||!strcmp(aid_reply,"$FAIL,1*1F"))aid_status.ack=3;
   aid_reply_len=0;
  }else if(aid_reply_len&&aid_reply_len<sizeof aid_reply-1)aid_reply[aid_reply_len++]=ch;
  else aid_reply_len=0;
 }
}

int r2_gnss_input_error(void) {return (int)atomic_get(&error);}
enum r2_association r2_gnss_association(void) { return R2_ASSOC_UNKNOWN; }
bool r2_gnss_capture_now(struct r2_capture *current) { return r2_capture_now(current); }
static void fail(int rc)
{
  if(!rc)return;
  if(atomic_cas(&error,0,rc))r2_time_input_fault(R2_INPUT_DRIVER);
}
static void edge(const struct device *dev,struct gpio_callback *cb,uint32_t pins)
{
  if(!atomic_get(&stop_requested)&&(pins&BIT(pps.pin))){
    int64_t now=r2_time_now_us();struct r2_capture capture;
    if(r2_capture_pps(&capture,now)){
      if(r2_shadow_enabled())r2_shadow_edge(capture,now);
      r2_time_input_precise_pps(capture,now);
    }
    else r2_time_input_pps(now);
  }
}
static void uart_event(const struct device *dev,struct uart_event *event,void *unused)
{
  switch(event->type) {
  case UART_TX_DONE:
  case UART_TX_ABORTED:
#ifdef R2_DC_PROFILE
    if(event->data.tx.buf==bridge_tx&&atomic_get(&bridge_state)==1){
      atomic_set(&bridge_state,event->type==UART_TX_DONE&&event->data.tx.len==bridge_size?2:3);break;
    }
#endif
    if(event->data.tx.buf!=aid_tx||atomic_get(&aid_tx_state)!=1){fail(-EFAULT);break;}
    atomic_set(&aid_tx_state,event->type==UART_TX_DONE&&event->data.tx.len==aid_size?2:3);
    break;
  case UART_RX_RDY:
    if(atomic_get(&stop_requested))break;
    if((event->data.rx.buf!=rx[0]&&event->data.rx.buf!=rx[1]) ||
       event->data.rx.offset>128 || event->data.rx.len>128-event->data.rx.offset){fail(-EOVERFLOW);break;}
#ifdef R2_DC_PROFILE
    r2_dc_gnss_receive(event->data.rx.buf+event->data.rx.offset,event->data.rx.len,r2_time_now_us());
#else
    for(size_t i=0;i<event->data.rx.len;i+=R2_TIME_CHUNK) {
      size_t n=MIN((size_t)R2_TIME_CHUNK,event->data.rx.len-i);
      r2_time_input_bytes(event->data.rx.buf+event->data.rx.offset+i,n,r2_time_now_us());
    }
#endif
    break;
  case UART_RX_BUF_REQUEST:
    if(atomic_get(&stop_requested))break;
    for(unsigned i=0;i<2;i++)if(atomic_cas(&used[i],0,1)) {
      int rc=uart_rx_buf_rsp(dev,rx[i],sizeof(rx[i]));
      if(rc){atomic_clear(&used[i]);fail(rc);}break;
    }
    break;
  case UART_RX_BUF_RELEASED:
    if(event->data.rx_buf.buf==rx[0])atomic_clear(&used[0]);
    else if(event->data.rx_buf.buf==rx[1])atomic_clear(&used[1]);
    else fail(-EFAULT);
    break;
  case UART_RX_STOPPED: fail(-EIO);break;
  case UART_RX_DISABLED:
    atomic_set(&disabled,1);
    if(!atomic_get(&stop_requested))fail(-EPIPE);
    break;
  default: break;
  }
}
static void aid_poll(int64_t now){
 int state=atomic_get(&aid_tx_state);
 if(state==2||state==3){
  uint32_t *count=state==2?&aid_status.completed:&aid_status.aborted;
  if(*count<UINT32_MAX)++*count;
  if(state==3)aid_status.result=-ECANCELED;
  atomic_clear(&aid_tx_state);state=0;
 }
 if(state==1&&(atomic_get(&stop_requested)||now<aid_at||now-aid_at>=100000)&&!aid_abort_sent){
  aid_abort_sent=true;aid_abort_at=now;int rc=uart_tx_abort(uart);
  /* DONE may win the abort race. Never free an unreturned DMA buffer. */
  if(rc&&atomic_get(&aid_tx_state)==1)fail(rc);
 }
 if(atomic_get(&aid_tx_state)==1&&aid_abort_sent&&now-aid_abort_at>=100000)fail(-ETIMEDOUT);
 if(aid_status.ack==1&&now>=aid_at&&now-aid_at>=2000000)aid_status.ack=4;
}
static void power_off(void)
{
  /* GNSS owns ENABLE. VEXT remains on while the independent display runs. */
  /* Only after UART_RX_DISABLED AND every DMA buffer release. */
  fail(gpio_pin_set_dt(&enable,0));
  rail_kept=r2_ui_close_result()!=0;
  if(!rail_kept)fail(gpio_pin_set_dt(&vext,0));
  closed=true;
}
#ifdef R2_DC_PROFILE
static void bridge_poll(int64_t now){
 int state=atomic_get(&bridge_state);
 if(state==2||state==3){atomic_clear(&bridge_state);state=0;}
 if(state==1&&(atomic_get(&stop_requested)||now<bridge_at||now-bridge_at>=100000||bridge_epoch!=r2_dc_gnss_epoch())&&!bridge_aborting){
  bridge_aborting=true;bridge_abort_at=now;int rc=uart_tx_abort(uart);
  if(rc&&atomic_get(&bridge_state)==1)fail(rc);
 }
 if(atomic_get(&bridge_state)==1&&bridge_aborting&&now>=bridge_abort_at&&now-bridge_abort_at>=100000)fail(-ETIMEDOUT);
 if(atomic_get(&bridge_state)||atomic_get(&aid_tx_state)||!uart_ready||atomic_get(&initializing)||atomic_get(&disabled)||atomic_get(&stop_requested)||atomic_get(&error))return;
 bridge_size=r2_dc_gnss_next(bridge_tx,sizeof bridge_tx,now,&bridge_epoch);
 if(!bridge_size)return;
 bridge_at=now;bridge_aborting=false;atomic_set(&bridge_state,1);
 if(uart_tx(uart,bridge_tx,bridge_size,100000))atomic_clear(&bridge_state);
}
#endif
int r2_gnss_start(void)
{
  if(opened)return -EALREADY;
  opened=true;closed=false;atomic_clear(&stop_requested);atomic_clear(&error);
  if(device_is_ready(uart)){fail(-EALREADY);closed=true;atomic_set(&stop_requested,1);return -EALREADY;}
  if(!gpio_is_ready_dt(&enable)||!gpio_is_ready_dt(&vext)||!gpio_is_ready_dt(&pps)) {fail(-ENODEV);closed=true;atomic_set(&stop_requested,1);return -ENODEV;}
  int rc=gpio_pin_configure_dt(&pps,GPIO_INPUT);
  if(!rc){gpio_init_callback(&edge_callback,edge,BIT(pps.pin));rc=gpio_add_callback(pps.port,&edge_callback);if(!rc)callback_added=true;}
  if(!rc)rc=gpio_pin_interrupt_configure_dt(&pps,GPIO_INT_EDGE_TO_ACTIVE);
  if(!rc)(void)r2_capture_start(&pps,r2_time_now_us()); /* Failure leaves coarse GPS available. */
  if(!rc)rc=gpio_pin_set_dt(&vext,1);
  if(rc){fail(rc);r2_gnss_stop();return rc;}
  power_ready=r2_time_now_us()+10000;powering=true;return 0;
}
static void initialize_uart(void *a,void *b,void *c)
{
  /* Stock device_init contains peripheral event waits. Isolate it below the
   * application owner; a late or missing completion cannot stop expiry. */
  int rc=device_init(uart);
  if(!rc&&!device_is_ready(uart))rc=-ENODEV;
  if(!rc){uart_ready=true;rc=uart_callback_set(uart,uart_event,NULL);}
  if(!rc && (atomic_get(&stop_requested)||r2_time_now_us()>=startup_deadline))rc=-ETIMEDOUT;
  if(!rc) {
    atomic_set(&used[0],1);atomic_clear(&disabled);
    rc=uart_rx_enable(uart,rx[0],sizeof(rx[0]),20000);
    if(rc){atomic_clear(&used[0]);atomic_set(&disabled,1);}
    if(!rc&&r2_time_now_us()>=startup_deadline)rc=-ETIMEDOUT;
  }
  if(rc)fail(rc);
  atomic_clear(&initializing);
}
void r2_gnss_poll(int64_t now)
{
  aid_poll(now);
#ifdef R2_DC_PROFILE
  bridge_poll(now);
#endif
  if(!atomic_get(&stop_requested))r2_capture_poll(now);
  if(atomic_get(&initializing)&&now>=startup_deadline)fail(-ETIMEDOUT);
  if(atomic_get(&error)&&!atomic_get(&stop_requested))r2_gnss_stop();
  if(atomic_get(&stop_requested)) {
    if(atomic_get(&initializing))return; /* no rail/DMA changes during device_init */
    if(uart_ready&&!atomic_get(&disabled)&&!stop_sent) {
      stop_sent=true;int rc=uart_rx_disable(uart);if(rc)fail(rc);
    }
    if((!closed||(rail_kept&&!r2_ui_close_result()))&&atomic_get(&aid_tx_state)!=1
#ifdef R2_DC_PROFILE
       &&atomic_get(&bridge_state)!=1
#endif
       &&atomic_get(&disabled)&&!atomic_get(&used[0])&&!atomic_get(&used[1]))power_off();
    return;
  }
  if(!powering||now<power_ready)return;
  powering=false;int rc=gpio_pin_set_dt(&enable,1);
  if(rc){fail(rc);r2_gnss_stop();return;}
  startup_deadline=now+500000;atomic_set(&initializing,1);
  k_thread_create(&gnss_thread,gnss_stack,K_THREAD_STACK_SIZEOF(gnss_stack),initialize_uart,
    NULL,NULL,NULL,9,0,K_NO_WAIT);
  k_thread_name_set(&gnss_thread,"r2_gnss_init");
}
void r2_gnss_stop(void)
{
  if(atomic_set(&stop_requested,1))return;
#ifdef R2_DC_PROFILE
  r2_dc_gnss_stop();
#endif
  powering=false;
  int capture_rc=r2_capture_stop();if(capture_rc)fail(capture_rc);
  if(callback_added) {
    int rc=gpio_pin_interrupt_configure_dt(&pps,GPIO_INT_DISABLE);fail(rc);
    if(!rc){rc=gpio_remove_callback(pps.port,&edge_callback);fail(rc);if(!rc)callback_added=false;}
  }
  if(!opened)closed=true;
}
int r2_gnss_close_result(void)
{
  int rc=(int)atomic_get(&error);
  if(rc)return rc;
  return closed&&!callback_added&&!atomic_get(&initializing)&&atomic_get(&aid_tx_state)!=1
#ifdef R2_DC_PROFILE
    &&atomic_get(&bridge_state)!=1
#endif
    &&atomic_get(&disabled)&&!atomic_get(&used[0])&&!atomic_get(&used[1])?0:-EAGAIN;
}
