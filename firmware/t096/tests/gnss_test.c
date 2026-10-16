/* Actual native adapter, with only public driver calls replaced. */
#include "time_service.h"
#include "capture.h"
#include "gnss_aid.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int64_t r2_time_now_us(void){return now;}
void r2_time_input_fault(enum r2_clock_fault reason){++faults;}
void r2_time_input_pps(int64_t at){assert(at==now);++pps_calls;}
void r2_time_input_precise_pps(struct r2_capture capture,int64_t at){assert(capture.epoch&&at==now);++pps_calls;}
int r2_capture_start(const struct gpio_dt_spec *p,int64_t at){return 0;}
void r2_capture_poll(int64_t at){}
bool r2_capture_pps(struct r2_capture *out,int64_t at){return false;}
bool r2_capture_now(struct r2_capture *out){return false;}
int r2_capture_stop(void){return 0;}
void r2_time_input_bytes(const uint8_t *p,size_t n,int64_t at){assert(n<=64&&at==now);received_bytes+=(unsigned)n;}
static int ui_close;static unsigned ui_stops;
void r2_ui_stop(void){++ui_stops;}
int r2_ui_close_result(void){return ui_close;}
#include "../src/gnss.c"
static void event(int type,uint8_t *buf,size_t offset,size_t len)
{
 struct uart_event e={.type=type};
 if(type==UART_RX_RDY)e.data.rx=(typeof(e.data.rx)){buf,offset,len};
 else e.data.rx_buf.buf=buf;
 rx_callback(&fake_uart,&e,NULL);
}
int main(int argc,char **argv)
{
 assert(argc==2);
 bool failure=!strcmp(argv[1],"failure");if(failure)start_rc=-5;
 assert(r2_gnss_association()==R2_ASSOC_UNKNOWN);
 assert(!r2_gnss_start()&&levels[2]==1&&levels[1]==0);
 now=9999;r2_gnss_poll(now);assert(!init_calls);
 now=10000;r2_gnss_poll(now);assert(!init_calls&&pending_worker&&levels[1]==1);
 if(!strcmp(argv[1],"timeout")) {
   now=510000;r2_gnss_poll(now);assert(!init_calls&&!enable_calls&&faults&&levels[1]==1);
   pending_worker(NULL,NULL,NULL);r2_gnss_poll(++now);
   assert(init_calls==1&&!enable_calls&&r2_gnss_close_result()==-ETIMEDOUT&&levels[1]==0);
   puts("R2 actual GNSS init: original 500 ms deadline, main progress and late-result refusal PASS");return 0;
 }
 pending_worker(NULL,NULL,NULL);assert(init_calls==1&&enable_calls==1);
 if(failure){r2_gnss_poll(++now);assert(faults&&r2_gnss_close_result()<0&&levels[1]==0&&levels[2]==0);puts("R2 actual GNSS start failure remains fenced PASS");return 0;}
 if(!strcmp(argv[1],"ui_wait")||!strcmp(argv[1],"ui_fault")){
   ui_close=-EAGAIN;event(UART_RX_STOPPED,NULL,0,0);r2_gnss_poll(++now);assert(!ui_stops);
   event(UART_RX_DISABLED,NULL,0,0);event(UART_RX_BUF_RELEASED,rx[0],0,0);r2_gnss_poll(++now);
   assert(closed&&levels[1]==0&&levels[2]==1);
   ui_close=!strcmp(argv[1],"ui_fault")?-ETIMEDOUT:0;
   for(unsigned i=0;i<10;i++)r2_gnss_poll(++now);
   assert(closed);assert(levels[2]==(ui_close?1:0));
   puts("GNSS automatic fault: independent UI stays active; GNSS enable removed only after DMA drain; shared rail preserved PASS");return 0;
 }
 if(!strncmp(argv[1],"aid",3)){
  uint64_t utc=1791744000123000ULL,at=(uint64_t)now;
  r2_gnss_aid(utc,at,false,false);r2_gnss_aid(utc,at,true,true);
  r2_gnss_aid(utc,at+1,true,false);r2_gnss_aid(UINT64_MAX,at-1,true,false);assert(!tx_calls);
  now+=1500000;r2_gnss_aid(utc,at,true,false);assert(!tx_calls);at=now;
  struct r2_service_status svc={.active=true,.display_coarse_valid=true,.display_coarse_utc=utc,.display_coarse_at=at};uint64_t u,t;
  assert(r2_peer_aid(&svc,now,&u,&t)&&u==utc&&t==at);
  svc.closing=true;assert(!r2_peer_aid(&svc,now,&u,&t));svc.closing=false;
  svc.display_coarse_valid=false;assert(!r2_peer_aid(&svc,now,&u,&t));
  svc.follower_clock_valid=svc.precision.authorized=true;svc.root_boot=1;svc.source_epoch=2;svc.service_until=now+100;svc.follower_utc=utc+1;svc.time_sampled_us=at;
  assert(r2_peer_aid(&svc,now,&u,&t)&&u==utc+1);assert(!r2_peer_aid(&svc,now+100,&u,&t));
  if(!strcmp(argv[1],"aid_reject"))tx_rc=-EBUSY;
  r2_gnss_aid(utc,at,true,false);assert(tx_calls==1);
  assert(!strcmp((const char *)tx_buf,"$AIDTIME,2026,10,11,18,40,00,123\r\n"));
  r2_gnss_aid(utc,at,true,false);assert(tx_calls==1);
  if(tx_rc){now+=1000000;r2_gnss_aid(utc,now,true,false);assert(tx_calls==1&&!aid_status.completed);puts("Aid submission failure rate limited PASS");return 0;}
  struct uart_event e={.type=UART_TX_DONE,.data.tx={tx_buf,tx_len}};
  if(!strcmp(argv[1],"aid_stop")||!strcmp(argv[1],"aid_timeout")||!strcmp(argv[1],"aid_stuck")){
   if(!strcmp(argv[1],"aid_stop"))r2_gnss_stop();else now+=100000;
   r2_gnss_poll(++now);assert(abort_calls==1&&levels[1]==1);
   r2_gnss_stop();r2_gnss_poll(++now);event(UART_RX_DISABLED,NULL,0,0);event(UART_RX_BUF_RELEASED,rx[0],0,0);r2_gnss_poll(++now);
   assert(levels[1]==1&&r2_gnss_close_result()==-EAGAIN);
   if(!strcmp(argv[1],"aid_stuck")){now+=100000;r2_gnss_poll(now);assert(r2_gnss_close_result()==-ETIMEDOUT&&levels[1]==1&&abort_calls==1);puts("Missing TX drain callback fences poweroff PASS");return 0;}
   e.type=UART_TX_ABORTED;e.data.tx.len=2;
   rx_callback(&fake_uart,&e,NULL);r2_gnss_poll(++now);assert(!r2_gnss_close_result()&&levels[1]==0&&aid_status.aborted==1);
   puts("Aid bounded abort and DMA-return-before-poweroff PASS");return 0;
  }
  rx_callback(&fake_uart,&e,NULL);r2_gnss_poll(++now);assert(aid_status.completed==1&&!atomic_get(&aid_tx_state));
  r2_gnss_observe((const uint8_t *)"$OK*04\r\n",8,aid_at-1);assert(aid_status.ack==1);
  r2_gnss_observe((const uint8_t *)"$OK*05\r\n",8,now);assert(aid_status.ack==1);
  r2_gnss_observe((const uint8_t *)"$O",2,now);r2_gnss_observe((const uint8_t *)"K*04\r\n",6,now);assert(aid_status.ack==2);
  now=aid_at+60000000;r2_gnss_aid(utc,now,true,false);assert(tx_calls==2);
  rx_callback(&fake_uart,&e,NULL);r2_gnss_poll(++now);assert(aid_status.completed==2);
  r2_gnss_observe((const uint8_t *)"$FAIL,0*1E\r\n",12,now);assert(aid_status.ack==3);
  now=aid_at+60000000;r2_gnss_aid(utc,now,true,false);rx_callback(&fake_uart,&e,NULL);r2_gnss_poll(++now);now=aid_at+2000000;r2_gnss_poll(now);assert(aid_status.ack==4);
  assert(aid_format(1709164800999000ULL)>0&&!strcmp((const char *)aid_tx,"$AIDTIME,2024,02,29,00,00,00,999\r\n"));
  assert(!aid_format(0)&&!aid_format(UINT64_MAX));
  puts("Actual GNSS aid: fresh peer policy, bounded UTC formatting, TX completion, split/checksummed ACK and60s retry PASS");return 0;
 }
 event(UART_RX_BUF_REQUEST,NULL,0,0);assert(used[0]&&used[1]);
 event(UART_RX_RDY,rx[0],0,128);assert(received_bytes==128);
 edge(&fake_uart,&edge_callback,BIT(3));assert(pps_calls==1);
 r2_gnss_stop();r2_gnss_poll(++now);assert(disable_calls==1&&r2_gnss_close_result()==-EAGAIN);
 event(UART_RX_DISABLED,NULL,0,0);r2_gnss_poll(++now);assert(levels[1]==1&&r2_gnss_close_result()==-EAGAIN);
 event(UART_RX_BUF_RELEASED,rx[0],0,0);r2_gnss_poll(++now);assert(levels[1]==1);
 event(UART_RX_BUF_RELEASED,rx[1],0,0);r2_gnss_poll(++now);assert(!r2_gnss_close_result()&&levels[1]==0&&levels[2]==0);
 edge(&fake_uart,&edge_callback,BIT(3));event(UART_RX_RDY,rx[0],0,128);assert(pps_calls==1&&received_bytes==128);
 r2_gnss_stop();r2_gnss_poll(++now);assert(disable_calls==1&&!faults);
 puts("R2 actual GNSS adapter: deferred power/init, bounded RX, PPS, async DMA closure and late-callback fence PASS");
}
