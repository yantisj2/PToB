/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "time_service.h"
#include "capture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int64_t r2_time_now_us(void){return now;}
void r2_time_input_fault(enum r2_clock_fault r){(void)r;faults++;}
void r2_time_input_pps(int64_t at){(void)at;}
void r2_time_input_precise_pps(struct r2_capture c,int64_t at){(void)c;(void)at;}
int r2_capture_start(const struct gpio_dt_spec *p,int64_t at){(void)p;(void)at;return 0;}
void r2_capture_poll(int64_t at){(void)at;}
bool r2_capture_pps(struct r2_capture *p,int64_t at){(void)p;(void)at;return false;}
bool r2_capture_now(struct r2_capture *p){(void)p;return false;}
int r2_capture_stop(void){return 0;}
int r2_ui_close_result(void){return 0;}
void r2_time_input_bytes(const uint8_t *p,size_t n,int64_t at){(void)p;(void)n;(void)at;}
static bool queued=true;static uint32_t usb_epoch=1;static unsigned stopped;
void r2_dc_gnss_receive(const uint8_t *p,size_t n,int64_t at){(void)p;(void)n;(void)at;}
uint32_t r2_dc_gnss_epoch(void){return usb_epoch;}
void r2_dc_gnss_stop(void){stopped++;}
size_t r2_dc_gnss_next(uint8_t *p,size_t n,int64_t at,uint32_t *epoch){(void)at;if(!queued)return 0;assert(n>=8);memcpy(p,"$PDTINFO",8);queued=false;*epoch=usb_epoch;return 8;}
#include "../src/gnss.c"
int main(int argc,char **argv){
 assert(argc==2);assert(!r2_gnss_start());now=10000;r2_gnss_poll(now);pending_worker(NULL,NULL,NULL);
 r2_gnss_poll(++now);assert(tx_calls==1&&atomic_get(&bridge_state)==1);
 r2_gnss_aid(1791744000123000ULL,now,true,false);assert(tx_calls==1);
 if(!strcmp(argv[1],"reset"))usb_epoch++;
 else if(!strcmp(argv[1],"timeout"))now+=100000;
 else r2_gnss_stop();
 r2_gnss_poll(++now);assert(abort_calls==1&&levels[1]==1);
 r2_gnss_stop();r2_gnss_poll(++now);assert(stopped==1);
 struct uart_event e={.type=UART_RX_DISABLED};uart_event(&fake_uart,&e,NULL);
 e.type=UART_RX_BUF_RELEASED;e.data.rx_buf.buf=rx[0];uart_event(&fake_uart,&e,NULL);
 r2_gnss_poll(++now);assert(r2_gnss_close_result()==-EAGAIN&&levels[1]==1);
 e.type=UART_TX_ABORTED;e.data.tx.buf=bridge_tx;e.data.tx.len=1;uart_event(&fake_uart,&e,NULL);
 r2_gnss_poll(++now);assert(!r2_gnss_close_result()&&levels[1]==0&&!faults);
 puts("DC actual UART owner: USB epoch/timeout/stop abort, aid exclusion and DMA drain before poweroff PASS");
}
