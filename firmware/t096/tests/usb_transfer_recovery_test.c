/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual pinned Zephyr function bodies retain Apache-2.0. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define CONFIG_USB_MAX_NUM_TRANSFERS 3
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define CONTAINER_OF(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define USB_TRANS_WRITE 1u
#define USB_TRANS_READ 0u
#define USB_TRANS_NO_ZLP 2u
#define K_NO_WAIT 0
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
struct k_work {int unused;};struct k_sem {unsigned value,gives;};
static int USB_WORK_Q,submit_result=1,read_result;static unsigned submissions;
static int read_wait_result;
static unsigned read_wait_count,read_wait_bytes;
static void (*lock_hook)(void);
static unsigned irq_lock(void){if(lock_hook){void (*f)(void)=lock_hook;lock_hook=NULL;f();}return 0;}
static void irq_unlock(unsigned k){(void)k;}
static int k_sem_take(struct k_sem *s,int wait){(void)wait;if(!s->value)return -EAGAIN;--s->value;return 0;}
static void k_sem_give(struct k_sem *s){++s->gives;s->value=1;}
static int k_work_submit_to_queue(int *q,struct k_work *w){(void)q;(void)w;++submissions;return submit_result;}
static bool k_is_in_isr(void){return false;}
typedef void (*usb_transfer_callback)(uint8_t,int,void *);
enum usb_dc_ep_cb_status_code {USB_DC_EP_DATA_IN,USB_DC_EP_DATA_OUT};
static int usb_dc_ep_read_continue(uint8_t ep){(void)ep;return read_result;}
static unsigned usb_dc_ep_mps(uint8_t ep){(void)ep;return 64;}
static int usb_write(uint8_t ep,uint8_t *p,size_t n,uint32_t *out){(void)ep;(void)p;*out=(uint32_t)n;return 0;}
static int usb_dc_ep_read_wait(uint8_t ep,uint8_t *p,size_t n,uint32_t *out){
 (void)ep;(void)p;(void)n;++read_wait_count;
 if(read_wait_result)return read_wait_result;
 *out=read_wait_bytes;return 0;
}
#include R2_USB_TRANSFER_BODY
static unsigned callbacks[3];
static void callback(uint8_t ep,int size,void *p){(void)ep;(void)size;assert((uintptr_t)p<3);++callbacks[(uintptr_t)p];}
static void reset(void){memset(ut_data,0,sizeof ut_data);memset(callbacks,0,sizeof callbacks);for(unsigned i=0;i<ARRAY_SIZE(ut_data);i++)ut_data[i].sem.value=1;submit_result=1;read_result=read_wait_result=0;submissions=read_wait_count=read_wait_bytes=0;lock_hook=NULL;}
static void inject_active(void){ut_data[1].ep=1;ut_data[1].status=-EBUSY;ut_data[1].sem.value=0;}
int main(void){
 reset();ut_data[0].ep=1;ut_data[0].status=-ECANCELED;ut_data[0].sem.value=0;ut_data[0].cb=callback;ut_data[0].priv=(void *)0;
 ut_data[1].ep=1;ut_data[1].status=-EBUSY;ut_data[1].sem.value=0;ut_data[1].cb=callback;ut_data[1].priv=(void *)1;
#ifdef FIXED
 assert(usb_ep_get_transfer(1)==&ut_data[1]&&usb_transfer_is_busy(1));
#else
 assert(usb_ep_get_transfer(1)==&ut_data[0]&&!usb_transfer_is_busy(1));
#endif
 usb_transfer_work(&ut_data[0].work);assert(callbacks[0]==1&&ut_data[0].sem.gives==1);
 usb_transfer_ep_callback(1,USB_DC_EP_DATA_OUT);
#ifdef FIXED
 assert(callbacks[1]==1&&ut_data[1].sem.gives==1&&ut_data[0].sem.gives==1&&!usb_transfer_is_busy(1));
 assert(!ut_data[0].status&&!ut_data[1].status);
#else
 assert(!callbacks[1]&&ut_data[1].sem.gives==0&&ut_data[0].sem.gives==2);
#endif
 reset();lock_hook=inject_active;uint8_t bytes[64]={0};int rc=usb_transfer(1,bytes,sizeof bytes,USB_TRANS_READ,callback,(void *)2);
#ifdef FIXED
 assert(rc==-EBUSY&&ut_data[0].sem.value==1);
#else
 assert(!rc&&ut_data[0].status==-EBUSY&&ut_data[1].status==-EBUSY);
#endif
 reset();read_result=-EIO;rc=usb_transfer(2,bytes,sizeof bytes,USB_TRANS_READ,callback,(void *)2);
 assert(rc==-EIO&&!callbacks[2]);
#ifdef FIXED
 assert(!usb_transfer_is_busy(2)&&ut_data[0].sem.value==1&&ut_data[0].sem.gives==1&&!ut_data[0].cb);
#else
 assert(usb_transfer_is_busy(2)&&ut_data[0].sem.value==0&&ut_data[0].cb);
#endif
 reset();submit_result=-EIO;rc=usb_transfer(0x81,bytes,sizeof bytes,USB_TRANS_WRITE,callback,(void *)2);
#ifdef FIXED
 assert(rc==-EIO&&!usb_transfer_is_busy(0x81)&&ut_data[0].sem.value==1&&!callbacks[2]);
#else
 assert(!rc&&usb_transfer_is_busy(0x81)&&ut_data[0].sem.value==0);
#endif
 reset();assert(!usb_transfer(2,bytes,sizeof bytes,USB_TRANS_READ,callback,(void *)2));
 usb_cancel_transfer(2);assert(ut_data[0].status==-ECANCELED&&ut_data[0].sem.value==0&&!callbacks[2]);
 usb_transfer_work(&ut_data[0].work);assert(callbacks[2]==1&&ut_data[0].sem.value==1&&ut_data[0].sem.gives==1);
 usb_cancel_transfer(2);assert(callbacks[2]==1&&ut_data[0].sem.gives==1);
 assert(!usb_transfer(2,bytes,sizeof bytes,USB_TRANS_READ,callback,(void *)2));
 assert(usb_transfer_is_busy(2)&&!usb_transfer_is_busy(3));
#ifdef FIXED
 reset();read_wait_result=-EIO;
 usb_transfer_ep_callback(1,USB_DC_EP_DATA_OUT);
 assert(read_wait_count==1&&!usb_transfer_is_busy(1));
 reset();read_wait_bytes=1;
 usb_transfer_ep_callback(1,USB_DC_EP_DATA_OUT);
 assert(read_wait_count==64&&!usb_transfer_is_busy(1));
 reset();uint8_t multi[128]={0};
 assert(!usb_transfer(2,multi,sizeof multi,USB_TRANS_READ,callback,(void *)2));
 read_wait_bytes=64;read_result=-EIO;
 usb_transfer_ep_callback(2,USB_DC_EP_DATA_OUT);
 assert(read_wait_count==1&&callbacks[2]==1&&!usb_transfer_is_busy(2));
#endif
 puts("PASS actual transfer ownership/start/cancel/callback/semaphore/interleaving cases");return 0;
}
