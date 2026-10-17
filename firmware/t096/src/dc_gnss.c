/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "dc_gnss.h"
#include "dc_gnss_transport.h"
#include "time_service.h"
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
static const struct device *const cdc=DEVICE_DT_GET(DT_NODELABEL(cdc_acm_gnss));
static struct dc_gnss_ring output,input;
static struct dc_gnss_demux demux;
static struct dc_gnss_frame_parser parser;
static struct k_spinlock lock;
static uint32_t epoch=1,command_drops;
static uint32_t rejected_frames;
static int64_t input_at,output_at;
static bool active=true,dtr;
struct command {uint8_t bytes[DC_GNSS_FRAME_MAX];size_t size;int64_t at;uint32_t epoch;};
static struct command commands[2];static unsigned first,count;
K_THREAD_STACK_DEFINE(bridge_stack,2048);
static struct k_thread bridge_thread;
static void time_bytes(const uint8_t *p,size_t n,int64_t at,void *ctx){
 (void)ctx;for(size_t i=0;i<n;i+=R2_TIME_CHUNK)r2_time_input_bytes(p+i,MIN(n-i,R2_TIME_CHUNK),at);
}
void r2_dc_gnss_receive(const uint8_t *p,size_t n,int64_t at){
 uint32_t before=demux.overflow;
 dc_gnss_demux_feed(&demux,p,n,at,time_bytes,NULL);
 if(demux.overflow!=before)r2_time_input_fault(R2_INPUT_OVERFLOW);
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(active&&dtr){if(!output.count)output_at=at;(void)dc_gnss_ring_put(&output,p,n);}
 k_spin_unlock(&lock,key);
}
void r2_dc_gnss_usb_reset(void){
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(epoch<UINT32_MAX)epoch++;else active=false;
 output.head=output.tail=output.count=input.head=input.tail=input.count=first=count=0;dtr=false;
 k_spin_unlock(&lock,key);
}
uint32_t r2_dc_gnss_epoch(void){k_spinlock_key_t key=k_spin_lock(&lock);uint32_t e=epoch;k_spin_unlock(&lock,key);return e;}
void r2_dc_gnss_status(uint32_t out[5]){
 k_spinlock_key_t key=k_spin_lock(&lock);out[0]=output.dropped;out[1]=input.dropped;out[2]=rejected_frames;out[3]=command_drops;out[4]=output.count;k_spin_unlock(&lock,key);
}
void r2_dc_gnss_stop(void){r2_dc_gnss_usb_reset();k_spinlock_key_t key=k_spin_lock(&lock);active=false;k_spin_unlock(&lock,key);}
size_t r2_dc_gnss_next(uint8_t *p,size_t capacity,int64_t now,uint32_t *generation){
 size_t n=0;k_spinlock_key_t key=k_spin_lock(&lock);
 while(count){struct command *c=&commands[first];
  if(active&&dtr&&c->epoch==epoch&&now>=c->at&&now-c->at<250000&&c->size<=capacity){n=c->size;memcpy(p,c->bytes,n);*generation=epoch;}
  else if(command_drops<UINT32_MAX)command_drops++;
  first=(first+1)%2;count--;if(n)break;
 }
 k_spin_unlock(&lock,key);return n;
}
static void interrupt(const struct device *dev,void *ctx){
 (void)ctx;
 for(unsigned turn=0;turn<4&&uart_irq_update(dev)&&uart_irq_is_pending(dev);turn++){
  bool progress=false;
  if(uart_irq_rx_ready(dev)){
   uint8_t p[64];int n=uart_fifo_read(dev,p,sizeof p);
   if(n>0){k_spinlock_key_t key=k_spin_lock(&lock);if(active&&dtr){
    if(!input.count)input_at=r2_time_now_us();
    if(dc_gnss_ring_put(&input,p,(size_t)n)!=(size_t)n){
     /* A lost byte invalidates framing and any in-progress host TX. */
     input.head=input.tail=input.count=first=count=0;
     if(epoch<UINT32_MAX)epoch++;else active=false;
    }
   }k_spin_unlock(&lock,key);progress=true;}
  }
  if(uart_irq_tx_ready(dev)){
   k_spinlock_key_t key=k_spin_lock(&lock);
   if(!active||!dtr||!output.count)uart_irq_tx_disable(dev);
   else {
    /* FIFO copies accepted bytes; preserve an unaccepted suffix. */
    size_t n=MIN(output.count,MIN(64u,DC_GNSS_RING_SIZE-output.tail));
    int sent=uart_fifo_fill(dev,output.bytes+output.tail,n);
    if(sent>0&&(size_t)sent<=n){output.tail=(output.tail+(unsigned)sent)%DC_GNSS_RING_SIZE;output.count-=(unsigned)sent;progress=true;}
    else uart_irq_tx_disable(dev);
   }
   k_spin_unlock(&lock,key);
  }
  if(!progress)break;
 }
}
static void worker(void *a,void *b,void *c){
 (void)a;(void)b;(void)c;uint32_t seen=0;
 for(;;){
  uint32_t line=0;(void)uart_line_ctrl_get(cdc,UART_LINE_CTRL_DTR,&line);
  uint8_t bytes[128];uint32_t current;size_t n;int64_t received,now=r2_time_now_us();
  k_spinlock_key_t key=k_spin_lock(&lock);
  if(dtr!=(bool)line){dtr=(bool)line;output.head=output.tail=output.count=input.head=input.tail=input.count=first=count=0;if(epoch<UINT32_MAX)epoch++;else active=false;}
  current=epoch;received=input_at;n=dc_gnss_ring_get(&input,bytes,sizeof bytes);
  if(output.count&&(now<output_at||now-output_at>=500000)){
   output.dropped=UINT32_MAX-output.dropped<output.count?UINT32_MAX:output.dropped+output.count;
   output.head=output.tail=output.count=0;
  }
  if(active&&dtr&&output.count)uart_irq_tx_enable(cdc);else uart_irq_tx_disable(cdc);
  k_spin_unlock(&lock,key);
  if(seen!=current){uint32_t rejected=parser.rejected;memset(&parser,0,sizeof parser);parser.rejected=rejected;seen=current;}
  for(size_t i=0;i<n;i++){
   size_t ready=dc_gnss_frame_byte(&parser,bytes[i],received);if(!ready)continue;
   key=k_spin_lock(&lock);
   if(active&&dtr&&epoch==current&&count<2){struct command *cmd=&commands[(first+count)%2];memcpy(cmd->bytes,parser.bytes,ready);cmd->size=ready;cmd->at=parser.started;cmd->epoch=current;count++;}
   else if(command_drops<UINT32_MAX)command_drops++;
   k_spin_unlock(&lock,key);
  }
  key=k_spin_lock(&lock);rejected_frames=parser.rejected;k_spin_unlock(&lock,key);
  k_sleep(K_MSEC(5));
 }
}
void r2_dc_gnss_start(void){
 if(!device_is_ready(cdc)||uart_irq_callback_user_data_set(cdc,interrupt,NULL))return;
 uart_irq_rx_enable(cdc);
 k_thread_create(&bridge_thread,bridge_stack,K_THREAD_STACK_SIZEOF(bridge_stack),worker,NULL,NULL,NULL,11,0,K_NO_WAIT);
 k_thread_name_set(&bridge_thread,"dc_gnss_bridge");
}
