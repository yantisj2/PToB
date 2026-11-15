#!/usr/bin/env python3
"""Actual patched USB worker/probe with synthetic queue, ISR and driver boundaries.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
"""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
from usb_ep0_recovery import generate
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);generate(a.deps/'zephyr',out/'driver.c');body=(out/'driver.c').read_text()
generate(a.deps/'zephyr',out/'common.c','usb-dma-health');common=(out/'common.c').read_text()
def fn(name,body=body):
    m=re.search(r'^.*?\b'+name+r'\([^;]*?\)\s*\{',body,re.M);assert m,name
    return body[m.start():body.index('\n}',body.index('{',m.start()))+2]
src=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include "usb_health.h"
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define CONTAINER_OF(p,t,f) get_usbd_ctx()
#define K_FOREVER 0
#define USB_DC_RESET 1
#define USB_DC_SOF 2
#define NRFX_POWER_USB_STATE_READY 2
#define NRFX_POWER_USB_STATE_DISCONNECTED 0
#define USBD_IRQn 39
struct k_work {int unused;};
struct endpoint {struct {unsigned addr;} cfg;};
struct usbd_event {int evt_type;struct {void *data;} block;union {struct {struct endpoint *ep;} ep_evt;int pwr_evt;} evt;};
enum {USBD_EVT_EP,USBD_EVT_POWER,USBD_EVT_RESET,USBD_EVT_SOF,USBD_EVT_REINIT};
struct nrf_usbd_ctx {bool attached,ready;struct k_work usb_work;int drv_lock;uint32_t health_progress,health_pending,ep0_generation;void(*status_cb)(int,void *);};
static struct nrf_usbd_ctx ctx;
static struct {uint32_t FRAMECNTR,EVENTS_EP0SETUP,EVENTS_USBRESET,ENABLE,USBPULLUP;} regs;
#define NRF_USBD (&regs)
static int fifo_elem_slab,usbd_evt_fifo,usbd_work_queue,frees,power=2,suspended,enabled=1,blocked;
static struct usbd_event *queued;
static bool nrf_usbd_common_errata_199(void){return true;}
static int dma_available=1, dma_ep=0x81;
static uint32_t r2_dma_completions;
static unsigned k_sem_count_get(int *s){return *s;}
static bool irq_is_enabled(int irq){(void)irq;return enabled;}
static jmp_buf stuck;
static struct nrf_usbd_ctx *get_usbd_ctx(void){return &ctx;}
static unsigned irq_lock(void){return 0;}
static void irq_unlock(unsigned k){(void)k;}
static int nrfx_power_usbstatus_get(void){return power;}
static bool nrf_usbd_common_bus_suspend_check(void){return suspended;}
static bool nrf_usbd_common_suspend_check(void){return suspended;}
static bool nrf_usbd_common_is_enabled(void){return enabled;}
static void k_mem_slab_free(void *s,void *p){(void)s;(void)p;frees++;}
static void k_work_submit_to_queue(void *q,void *w){(void)q;(void)w;}
static bool k_fifo_is_empty(void *q){(void)q;return !queued;}
static struct usbd_event *usbd_evt_get(void){struct usbd_event *e=queued;queued=NULL;return e;}
static bool dev_ready(void){return ctx.ready;}
static void usbd_work_process_ep_events(void *e){(void)e;if(blocked)longjmp(stuck,1);}
static void usbd_work_process_pwr_events(void *e){(void)e;if(blocked)longjmp(stuck,1);}
static void usbd_reinit(void){if(blocked)longjmp(stuck,1);}
static void k_mutex_lock(void *m,int t){(void)m;(void)t;}
static void k_mutex_unlock(void *m){(void)m;}
static void eps_ctx_init(void){}
static void usbd_sof_trigger_iso_read(void){}
'''
src+='\n'+fn('r2_usb_dma_health',common)
for name in ('usbd_evt_free','usbd_work_schedule','usbd_worker_progress','usbd_work_handler','r2_usb_health_snapshot'):src+='\n'+fn(name)
src+=r'''
int main(void){
 ctx.attached=ctx.ready=true;ctx.ep0_generation=9;regs.FRAMECNTR=123;regs.ENABLE=regs.USBPULLUP=1;
 struct endpoint ep={0};struct usbd_event ev={.evt_type=USBD_EVT_EP,.evt.ep_evt.ep=&ep};
 uint32_t s[4];struct r2_usb_health h={0};
 queued=&ev;usbd_work_schedule();assert(ctx.health_pending);
 blocked=1;if(!setjmp(stuck)){usbd_work_handler(&ctx.usb_work);assert(0);}
 assert(ctx.health_progress==1&&ctx.health_pending&&!frees);
 r2_usb_health_snapshot(s);assert((s[0]&255)==55&&s[1]==1&&s[2]==9&&s[3]==123);
 assert(!r2_usb_health_stalled(&h,s,100));
 /* ISR overflow can discard many events; no worker completed or returned. */
 for(unsigned i=0;i<100;i++)usbd_evt_free(&ev);
 r2_usb_health_snapshot(s);assert(s[1]==1&&r2_usb_health_stalled(&h,s,30100));
 blocked=0;queued=&ev;usbd_work_handler(&ctx.usb_work);
 assert(ctx.health_progress==2&&!ctx.health_pending);
 r2_usb_health_snapshot(s);assert(!r2_usb_health_stalled(&h,s,30101));
 regs.EVENTS_EP0SETUP=1;r2_usb_health_snapshot(s);assert((s[0]&255)==87);
 assert(!r2_usb_health_stalled(&h,s,40000));assert(r2_usb_health_stalled(&h,s,70000));
 suspended=1;regs.EVENTS_EP0SETUP=0;r2_usb_health_snapshot(s);assert(!r2_usb_health_stalled(&h,s,100000));
 /* A held DMA semaphore in suspend must recover even with worker traffic. */
 dma_available=0;r2_dma_completions=7;h=(struct r2_usb_health){0};
 r2_usb_health_snapshot(s);assert((s[0]&256)&&s[1]==7&&(s[0]>>24)==0x81);
 assert(!r2_usb_health_stalled(&h,s,100000));
 ctx.health_progress+=100;r2_usb_health_snapshot(s);assert(s[1]==7);
 assert(r2_usb_health_stalled(&h,s,130000));
 ++r2_dma_completions;r2_usb_health_snapshot(s);assert(!r2_usb_health_stalled(&h,s,130001));
 dma_available=1;r2_usb_health_snapshot(s);assert(!(s[0]&256));
 regs.USBPULLUP=0;r2_usb_health_snapshot(s);assert(s[0]&512);
 assert(!r2_usb_health_stalled(&h,s,140000));assert(r2_usb_health_stalled(&h,s,170000));
 suspended=0;power=0;r2_usb_health_snapshot(s);assert(!r2_usb_health_stalled(&h,s,200000));
 puts("PASS actual USB worker stall, ISR overflow frees do not renew progress, completion, hardware SETUP, suspend and unplug");
}
'''
(out/'exact.c').write_text(src);cc=os.environ.get('CC','/opt/homebrew/opt/llvm/bin/clang')
cmd=[cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(APP/'include'),str(out/'exact.c'),'-o',str(out/'test')]
with(out/'test.log').open('x')as log:
 subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
(out/'receipt.json').write_text(json.dumps(dict(status='PASS',hardware=False,driver_sha256=hashlib.sha256(body.encode()).hexdigest(),common_sha256=hashlib.sha256(common.encode()).hexdigest(),policy_sha256=hashlib.sha256((APP/'include/usb_health.h').read_bytes()).hexdigest(),limits='Synthetic queue/ISR/driver boundaries; no claim of reproducing physical USB failure.'),indent=2)+'\n')
print('PASS USB worker health including overflow concealment regression')
