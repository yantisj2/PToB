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
#include <string.h>
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
static struct {uint32_t FRAMECNTR,EVENTS_EP0SETUP,EVENTS_USBRESET,ENABLE,USBPULLUP,EVENTCAUSE,INTEN;} regs;
#define NRF_USBD (&regs)
static struct {uint32_t HFCLKSTAT;} clock_regs;
static struct {uint32_t USBREGSTATUS;} power_regs;
#define NRF_CLOCK (&clock_regs)
#define NRF_POWER (&power_regs)
static int fifo_elem_slab,usbd_evt_fifo,usbd_work_queue,frees,power=2,suspended,bus_suspended,enabled=1,blocked;
static struct usbd_event *queued;
static bool common_failed;
static unsigned reinitializations;
static bool nrf_usbd_common_errata_199(void){return true;}
static bool nrf52_errata_162(void){return false;}
static bool nrf_usbd_common_errata_211(void){return true;}
static int dma_available=1, dma_ep=0x81;
static uint32_t r2_dma_completions;
static unsigned r2_lifecycle_failure;
static unsigned atomic_get(unsigned *p){return *p;}
static unsigned k_sem_count_get(int *s){return *s;}
static bool irq_is_enabled(int irq){(void)irq;return enabled;}
static bool nvic_pending;
static unsigned nvic_reads;
static uint32_t NVIC_GetPendingIRQ(int irq){assert(irq==USBD_IRQn);++nvic_reads;return nvic_pending;}
static jmp_buf stuck;
static struct nrf_usbd_ctx *get_usbd_ctx(void){return &ctx;}
static unsigned irq_depth;
static unsigned irq_lock(void){return irq_depth++;}
static void irq_unlock(unsigned k){assert(irq_depth==k+1);irq_depth=k;}
static int nrfx_power_usbstatus_get(void){return power;}
static bool nrf_usbd_common_bus_suspend_check(void){return bus_suspended;}
static bool nrf_usbd_common_suspend_check(void){return suspended;}
static bool nrf_usbd_common_is_enabled(void){return enabled;}
static void k_mem_slab_free(void *s,void *p){(void)s;(void)p;frees++;}
static void k_work_submit_to_queue(void *q,void *w){(void)q;(void)w;}
static bool k_fifo_is_empty(void *q){(void)q;return !queued;}
static struct usbd_event *usbd_evt_get(void){struct usbd_event *e=queued;queued=NULL;return e;}
static bool dev_ready(void){return ctx.ready;}
static void usbd_work_process_ep_events(void *e){(void)e;if(blocked)longjmp(stuck,1);}
static void usbd_work_process_pwr_events(void *e){(void)e;if(blocked)longjmp(stuck,1);}
static bool r2_usb_common_failed(void){return common_failed;}
static void usbd_reinit(void){++reinitializations;if(blocked)longjmp(stuck,1);}
static void k_mutex_lock(void *m,int t){(void)m;(void)t;}
static void k_mutex_unlock(void *m){(void)m;}
static int eps_ctx_init(void){return 0;}
static void usbd_sof_trigger_iso_read(void){}
'''
src+='\n'+fn('r2_usb_dma_health',common)
for name in ('usbd_evt_free','usbd_work_schedule','usbd_worker_progress','usbd_work_handler','r2_usb_health_snapshot','r2_usb_debug_snapshot'):src+='\n'+fn(name)
policy=(APP/'include/usb_health.h').read_text()
old=fn('r2_usb_health_stalled',policy)
start=old.index(' /* An awake,');end=old.index(' bool pending=',start)
old=old[:start]+old[end:]
src+='\n'+old.replace('r2_usb_health_stalled','old_usb_health_stalled')
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
 suspended=bus_suspended=1;regs.EVENTS_EP0SETUP=0;r2_usb_health_snapshot(s);assert(!r2_usb_health_stalled(&h,s,100000));
 /* A held DMA semaphore in suspend must recover even with worker traffic. */
 dma_available=0;r2_dma_completions=7;h=(struct r2_usb_health){0};
 r2_usb_health_snapshot(s);assert((s[0]&256)&&s[1]==7&&(s[0]>>24)==0x81);
 assert(!r2_usb_health_stalled(&h,s,100000));
 ctx.health_progress+=100;r2_usb_health_snapshot(s);assert(s[1]==7);
 assert(r2_usb_health_stalled(&h,s,130000));
 ++r2_dma_completions;r2_usb_health_snapshot(s);assert(!r2_usb_health_stalled(&h,s,130001));
 dma_available=1;r2_usb_health_snapshot(s);assert(!(s[0]&256));
 /* nRF52840 PS: USBPULLUP/FRAMECNTR are inaccessible in low power.
  * Reproduce the saved TC-01 flags except the corrected false PHY alarm. */
 regs.USBPULLUP=0;regs.FRAMECNTR=0;dma_ep=0x82;ctx.ep0_generation=44;ctx.health_progress=1377;
 r2_usb_health_snapshot(s);assert(s[0]==0x8205ec1f);
 assert(!r2_usb_health_stalled(&h,s,140000));assert(!r2_usb_health_stalled(&h,s,240000));
 /* Pending work/DMA and missing readiness/ENABLE/IRQ still fail in suspend. */
 ctx.health_pending=1;r2_usb_health_snapshot(s);
 assert(!r2_usb_health_stalled(&h,s,250000));assert(r2_usb_health_stalled(&h,s,280000));
 ctx.health_pending=0;
 for(unsigned broken=0;broken<3;broken++){
  ctx.ready=broken!=0;regs.ENABLE=broken!=1;enabled=broken!=2;
  h=(struct r2_usb_health){0};r2_usb_health_snapshot(s);assert(s[0]&512);
  assert(!r2_usb_health_stalled(&h,s,0));assert(r2_usb_health_stalled(&h,s,30000));
 }
 ctx.ready=true;regs.ENABLE=enabled=1;
 /* A bus suspend indication alone cannot excuse a missing active pull-up. */
 suspended=0;bus_suspended=1;h=(struct r2_usb_health){0};
 r2_usb_health_snapshot(s);assert(s[0]&512);
 assert(!r2_usb_health_stalled(&h,s,140000));assert(r2_usb_health_stalled(&h,s,170000));
 /* An awake idle PHY has no queued work or held DMA to monitor. The old
  * policy never noticed a frozen physical frame counter, even with live
  * unrelated software dispatch. The new observer uses no SOF interrupts. */
 suspended=bus_suspended=0;regs.USBPULLUP=1;regs.FRAMECNTR=912;
 h=(struct r2_usb_health){0};struct r2_usb_health old_h={0};
 for(uint32_t ms=0;ms<=30000;ms+=125){
  ++ctx.health_progress;r2_usb_health_snapshot(s);
  assert(!old_usb_health_stalled(&old_h,s,ms));
  assert(r2_usb_health_stalled(&h,s,ms)==(ms==30000));
 }
 /* Ordinary frame wraps keep an active bus healthy. Missing application
  * attachment/readiness or suspend cannot create an active-frame timer. */
 h=(struct r2_usb_health){0};
 for(uint32_t ms=0;ms<=60000;ms+=125){
  regs.FRAMECNTR=(2040+ms)&0x7ff;r2_usb_health_snapshot(s);
  assert(!r2_usb_health_stalled(&h,s,ms));
 }
 uint32_t active=s[0];
 for(unsigned bit=0;bit<14;bit++)if((0x3c17u>>bit)&1u){
  h=(struct r2_usb_health){0};s[0]=active&~(1u<<bit);
  for(uint32_t ms=0;ms<=60000;ms+=125)assert(!r2_usb_health_stalled(&h,s,ms));
 }
 /* Exact full-turn gaps make identical frame values ambiguous. */
 h=(struct r2_usb_health){0};s[0]=active;s[3]=912;
 for(uint32_t ms=0;ms<=61440;ms+=2048)assert(!r2_usb_health_stalled(&h,s,ms));
 h=(struct r2_usb_health){0};
 for(uint32_t elapsed=0;elapsed<=30000;elapsed+=125)
  assert(r2_usb_health_stalled(&h,s,UINT32_MAX-15000u+elapsed)==(elapsed==30000));
 /* Hardware low power after the ISR has observed bus resume is a PHY
  * inconsistency, not legitimate idle suspend. Both actual indications are
  * retained separately in unused UHW1 bits; its32byte contract is unchanged. */
 suspended=1;bus_suspended=0;r2_usb_health_snapshot(s);
 assert((s[0]&((1u<<14)|(1u<<15)))==(1u<<14)&&(s[0]&512));
 h=(struct r2_usb_health){0};
 for(uint32_t ms=0;ms<=30000;ms+=125){
  ++ctx.health_progress;r2_usb_health_snapshot(s);
  assert(r2_usb_health_stalled(&h,s,ms)==(ms==30000));
 }
 bus_suspended=1;regs.USBPULLUP=0;r2_usb_health_snapshot(s);
 h=(struct r2_usb_health){0};
 for(uint32_t ms=0;ms<=120000;ms+=125)assert(!r2_usb_health_stalled(&h,s,ms));
 suspended=0;power=0;r2_usb_health_snapshot(s);assert(!r2_usb_health_stalled(&h,s,200000));
 /* Physical display reads the exact same UHW words and four additional
  * raw registers. Preserve W1C cause/INTEN, owners, failure and IRQ state. */
 regs.EVENTCAUSE=0xfedcba98;regs.INTEN=0x87654321;
 clock_regs.HFCLKSTAT=0x00010001;power_regs.USBREGSTATUS=3;
 struct {uint32_t before;uint32_t words[8];uint32_t after;} guarded={.before=0x12345678,.after=0x87654321};
 uint32_t old_regs[sizeof regs/4];memcpy(old_regs,&regs,sizeof regs);
 unsigned outer=irq_lock();r2_usb_debug_snapshot(guarded.words);assert(irq_depth==1);irq_unlock(outer);
 assert(!irq_depth&&guarded.before==0x12345678&&guarded.after==0x87654321);
 r2_usb_health_snapshot(s);assert(!memcmp(s,guarded.words,sizeof s));
 assert(guarded.words[4]==0xfedcba98&&guarded.words[5]==0x87654321&&guarded.words[6]==0x00010001&&guarded.words[7]==3);
 assert(!memcmp(old_regs,&regs,sizeof regs)&&dma_available==1&&!r2_lifecycle_failure);
 suspended=bus_suspended=1;r2_usb_debug_snapshot(guarded.words);
 assert((guarded.words[0]&(1u<<14))&&(guarded.words[0]&(1u<<15))&&suspended&&bus_suspended);
 /* Pending is independent of enable and peripheral INTEN. The snapshot
  * observes the pending bit; it must never acknowledge or clear the IRQ. */
 uint32_t not_pending=guarded.words[0];unsigned reads=nvic_reads;
 nvic_pending=true;r2_usb_debug_snapshot(guarded.words);
 assert(nvic_reads==reads+1&&nvic_pending&&(guarded.words[0]^not_pending)==(1u<<22));
 assert(guarded.words[0]&(1u<<13));
 enabled=0;r2_usb_debug_snapshot(guarded.words);
 assert(nvic_pending&&(guarded.words[0]&(1u<<22))&&!(guarded.words[0]&(1u<<13)));
 assert(regs.INTEN==0x87654321&&regs.EVENTCAUSE==0xfedcba98&&!irq_depth);
 puts("PASS actual raw snapshot F22 NVIC pending independent of F13 enable/peripheral INTEN; pending remains unchanged");
 /* Overflow schedules REINIT even before ready. It is a lifecycle event,
  * while stale endpoint work must remain dropped. Terminal failure wins. */
 for(unsigned ready=0;ready<2;ready++)for(unsigned failed=0;failed<2;failed++){
  ctx.ready=ready;common_failed=failed;reinitializations=0;
  int old_frees=frees;uint32_t before_regs[sizeof regs/4];memcpy(before_regs,&regs,sizeof regs);
  ev.evt_type=USBD_EVT_REINIT;queued=&ev;usbd_work_handler(&ctx.usb_work);
  assert(reinitializations==!failed&&frees==old_frees+1&&!queued);
  assert(dma_available==1&&!memcmp(before_regs,&regs,sizeof regs));
 }
 common_failed=false;ctx.ready=false;reinitializations=0;
 ev.evt_type=USBD_EVT_EP;queued=&ev;blocked=1;
 usbd_work_handler(&ctx.usb_work);assert(!reinitializations&&!queued);blocked=0;
 puts("PASS existing overflow REINIT dispatch before/after ready; terminal common failure prevents restart; unready endpoint events remain dropped");
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
