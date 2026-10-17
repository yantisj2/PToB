/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "dc_gnss.h"
#include "time_service.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define DEVICE_DT_GET(n) (&cdc_device)
#define DT_NODELABEL(n) 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define K_MSEC(n) (n)
#define K_NO_WAIT 0
#define K_THREAD_STACK_DEFINE(n,s) char n[s]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
#define UART_LINE_CTRL_DTR 1
struct device {int unused;};static struct device cdc_device;
struct k_thread {int unused;};struct k_spinlock {int unused;};typedef int k_spinlock_key_t;
static bool locked,inject,tx_enabled;static int64_t now=1000000;static jmp_buf finish;
static uint8_t uart_bytes[128],sent_bytes[512];static size_t sent;static int fifo_limit=64;
static int64_t inject_at;
static k_spinlock_key_t k_spin_lock(struct k_spinlock *p){(void)p;assert(!locked);locked=true;return 0;}
static void k_spin_unlock(struct k_spinlock *p,k_spinlock_key_t k){(void)p;(void)k;assert(locked);locked=false;}
int64_t r2_time_now_us(void){
 int64_t at=now;
 if(inject&&!locked){inject=false;now++;r2_dc_gnss_receive(uart_bytes,sizeof uart_bytes,now);inject_at=now;}
 return at;
}
void r2_time_input_bytes(const uint8_t *p,size_t n,int64_t at){(void)p;(void)n;(void)at;}
void r2_time_input_fault(enum r2_clock_fault f){(void)f;assert(false);}
static int uart_line_ctrl_get(const struct device *d,unsigned c,uint32_t *v){(void)d;(void)c;*v=1;return 0;}
static void uart_irq_tx_enable(const struct device *d){(void)d;tx_enabled=true;}
static void uart_irq_tx_disable(const struct device *d){(void)d;tx_enabled=false;}
static void uart_irq_rx_enable(const struct device *d){(void)d;}
static int uart_irq_update(const struct device *d){(void)d;return 1;}
static int uart_irq_is_pending(const struct device *d){(void)d;return tx_enabled;}
static int uart_irq_rx_ready(const struct device *d){(void)d;return 0;}
static int uart_irq_tx_ready(const struct device *d){(void)d;return tx_enabled;}
static int uart_fifo_read(const struct device *d,uint8_t *p,size_t n){(void)d;(void)p;(void)n;return 0;}
static int uart_fifo_fill(const struct device *d,const uint8_t *p,size_t n){(void)d;if(n>(size_t)fifo_limit)n=(size_t)fifo_limit;memcpy(sent_bytes+sent,p,n);sent+=n;return (int)n;}
static int uart_irq_callback_user_data_set(const struct device *d,void (*cb)(const struct device *,void *),void *c){(void)d;(void)cb;(void)c;return 0;}
static bool device_is_ready(const struct device *d){(void)d;return true;}
static void k_thread_create(struct k_thread *t,char *s,size_t n,void (*f)(void*,void*,void*),void *a,void *b,void *c,int p,int o,int w){(void)t;(void)s;(void)n;(void)f;(void)a;(void)b;(void)c;(void)p;(void)o;(void)w;}
static void k_thread_name_set(struct k_thread *t,const char *n){(void)t;(void)n;}
static void k_sleep(int n){(void)n;longjmp(finish,1);}
#include "../src/dc_gnss.c"
static void poll_once(void){if(!setjmp(finish))worker(NULL,NULL,NULL);}
int main(void){
 for(size_t i=0;i<sizeof uart_bytes;i++)uart_bytes[i]='A'+i%26;
 dtr=true;inject=true;poll_once();
 /* An interrupt between a worker clock read and its lock cannot age fresh data. */
 if(inject){inject=false;r2_dc_gnss_receive(uart_bytes,sizeof uart_bytes,++now);inject_at=now;poll_once();}
 assert(inject_at&&output.count==sizeof uart_bytes&&output.dropped==0);
 fifo_limit=17;interrupt(cdc,NULL);interrupt(cdc,NULL);interrupt(cdc,NULL);
 assert(sent==sizeof uart_bytes&&output.count==0&&!memcmp(sent_bytes,uart_bytes,sent));
 r2_dc_gnss_receive(uart_bytes,sizeof uart_bytes,now);now+=499999;poll_once();assert(output.count==128&&!output.dropped);
 now++;poll_once();assert(output.count==0&&output.dropped==128);
 r2_dc_gnss_receive(uart_bytes,sizeof uart_bytes,now);r2_dc_gnss_usb_reset();assert(!output.count&&!dtr);
 puts("DC actual serial bridge: UART preemption, partial FIFO, original 500 ms expiry and USB reset PASS");
}
