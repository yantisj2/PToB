/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual pinned Zephyr function bodies retain Apache-2.0. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define LOG_WRN(...) ((void)0)
#define CONFIG_USB_CDC_ACM_RINGBUF_SIZE 128
#define CONFIG_CDC_ACM_BULK_EP_MPS 64
#define CONFIG_CDC_ACM_TX_DELAY_MS 10
#define USB_TRANS_WRITE 1u
#define USB_TRANS_READ 0u
#define ACM_IN_EP_IDX 1
#define ACM_OUT_EP_IDX 2
#define K_NO_WAIT 0
#define K_MSEC(n) (n)
#define CONTAINER_OF(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct k_work {int unused;};struct k_work_delayable {struct k_work work;};
struct ring_buf {unsigned count,claim;uint8_t data[128];};
struct device {void *data;const void *config;};
struct usb_cfg_data {struct {uint8_t ep_addr;}endpoint[3];};
struct cdc_acm_dev_data_t {
 void (*cb)(const struct device *,void *);void *cb_data;struct k_work cb_work;struct k_work_delayable tx_work;
 struct {const struct device *dev;}common;
 bool tx_ready,rx_ready,tx_irq_ena,rx_irq_ena,configured,suspended,rx_paused;
 uint8_t rx_buf[64];struct ring_buf *rx_ringbuf,*tx_ringbuf;
};
static int USB_WORK_Q,next_result;static bool busy[256],in_irq_enable;static unsigned starts,rx_starts,tx_starts,scheduled,callbacks;
static unsigned irq_lock(void){return 0;}static void irq_unlock(unsigned k){(void)k;}
static unsigned ring_buf_space_get(struct ring_buf *r){return 128-r->count;}
static bool ring_buf_is_empty(struct ring_buf *r){return !r->count;}
static unsigned ring_buf_put(struct ring_buf *r,const uint8_t *b,unsigned n){unsigned size=n<128-r->count?n:128-r->count;memcpy(r->data+r->count,b,size);r->count+=size;return size;}
static unsigned ring_buf_get_claim(struct ring_buf *r,uint8_t **b,unsigned n){*b=r->data;r->claim=r->count<n?r->count:n;return r->claim;}
static int ring_buf_get_finish(struct ring_buf *r,unsigned n){assert(n<=r->claim);r->count-=n;memmove(r->data,r->data+n,r->count);r->claim=0;return 0;}
static unsigned ring_buf_get(struct ring_buf *r,uint8_t *b,unsigned n){uint8_t *p;unsigned size=ring_buf_get_claim(r,&p,n);memcpy(b,p,size);ring_buf_get_finish(r,size);return size;}
static struct k_work_delayable *k_work_delayable_from_work(struct k_work *w){return CONTAINER_OF(w,struct k_work_delayable,work);}
static int k_work_submit_to_queue(int *q,struct k_work *w){(void)q;(void)w;++callbacks;return 1;}
static int k_work_schedule_for_queue(int *q,struct k_work_delayable *w,int delay){(void)q;(void)w;(void)delay;++scheduled;return 1;}
static bool usb_transfer_is_busy(uint8_t ep){return busy[ep];}
static int usb_transfer(uint8_t ep,uint8_t *data,size_t n,unsigned flags,void (*cb)(uint8_t,int,void *),void *priv){
 (void)data;(void)n;(void)cb;(void)priv;assert(!in_irq_enable);++starts;if(flags==USB_TRANS_WRITE)++tx_starts;else ++rx_starts;
 if(next_result){int rc=next_result;next_result=0;return rc;}assert(!busy[ep]);busy[ep]=true;return 0;
}
#ifdef FIXED
static void cdc_acm_rearm_idle(struct cdc_acm_dev_data_t *dev_data);
#endif
#include R2_CDC_BODY
static struct cdc_acm_dev_data_t d;static struct device dev;static struct usb_cfg_data cfg;static struct ring_buf tx,rx;
static void enable_rx(void){in_irq_enable=true;cdc_acm_irq_rx_enable(&dev);in_irq_enable=false;}
static void enable_tx(void){in_irq_enable=true;cdc_acm_irq_tx_enable(&dev);in_irq_enable=false;}
static void run_queue(void){
#ifdef FIXED
 assert(scheduled);tx_work_handler(&d.tx_work.work);
#endif
}
static void reset(void){memset(&d,0,sizeof d);memset(&tx,0,sizeof tx);memset(&rx,0,sizeof rx);memset(busy,0,sizeof busy);next_result=0;starts=rx_starts=tx_starts=scheduled=callbacks=0;cfg.endpoint[1].ep_addr=0x81;cfg.endpoint[2].ep_addr=1;dev.data=&d;dev.config=&cfg;d.common.dev=&dev;d.tx_ringbuf=&tx;d.rx_ringbuf=&rx;d.configured=true;d.tx_ready=true;}
int main(void){
 reset();tx.count=64;next_result=-ENOMEM;tx_work_handler(&d.tx_work.work);
#ifdef FIXED
 assert(tx.count==64&&d.tx_ready&&!busy[0x81]);
#else
 assert(tx.count==1&&!d.tx_ready&&!busy[0x81]);
#endif
 reset();next_result=-ENOMEM;cdc_acm_read_cb(1,0,&d);assert(rx_starts==1&&!busy[1]);enable_rx();run_queue();
#ifdef FIXED
 assert(rx_starts==2&&busy[1]);
#else
 assert(rx_starts==1&&!busy[1]);
#endif
 reset();d.tx_ready=false;enable_tx();run_queue();
#ifdef FIXED
 assert(d.tx_ready);
#else
 assert(!d.tx_ready);
#endif
 reset();d.rx_irq_ena=true;d.tx_irq_ena=true;busy[1]=busy[0x81]=true;tx.count=20;
 enable_rx();enable_tx();run_queue();assert(starts==0); /* No active ownership is disturbed. */
 cdc_acm_irq_rx_disable(&dev);cdc_acm_irq_tx_disable(&dev);assert(!d.rx_irq_ena&&!d.tx_irq_ena&&busy[1]&&busy[0x81]);
 busy[1]=busy[0x81]=false;enable_rx();enable_tx();run_queue();
#ifdef FIXED
 assert(rx_starts==1&&busy[1]&&busy[0x81]&&tx_starts==1&&!d.tx_ready&&scheduled);
#else
 assert(!starts);
#endif
 reset();d.configured=false;d.tx_ready=false;enable_rx();enable_tx();run_queue();assert(!starts&&!d.tx_ready);
 d.configured=true;d.suspended=true;enable_rx();enable_tx();run_queue();assert(!starts&&!d.tx_ready);
 reset();rx.count=100;enable_rx();run_queue();assert(!starts); /* Full ring cannot be overwritten. */
#ifdef FIXED
 reset();tx.count=64;tx_work_handler(&d.tx_work.work);assert(tx.count==1&&busy[0x81]&&!d.tx_ready);busy[0x81]=false;cdc_acm_write_cb(0x81,63,&d);assert(d.tx_ready);
#endif
 puts("PASS actual CDC rejected-start/idle-rearm/busy/close-reopen/disconnect/buffer cases");return 0;
}
