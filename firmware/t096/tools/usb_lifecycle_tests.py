#!/usr/bin/env python3
"""Exact production USB restart/power bodies with fake peripheral boundaries.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
from usb_ep0_recovery import generate
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
out=a.out.resolve();out.mkdir(exist_ok=False);generate(a.deps/'zephyr',out/'fixed.c');body=(out/'fixed.c').read_text()
def fn(name):
 m=re.search(r'^.*?\b'+name+r'\([^;]*?\)\s*\{',body,re.M);assert m,name
 end=body.index('\n}',body.index('{',m.start()))+2
 return body[m.start():end]
src='''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define __ASSERT_NO_MSG(x) assert(x)
#define CONFIG_USB_DEVICE_SOF 0
#define IS_ENABLED(x) (x)
#define NRFX_SUCCESS 0
enum {USBD_ATTACHED,USBD_POWERED,USBD_DETACHED,USBD_SUSPENDED,USBD_RESUMED};
enum {USB_DC_CONNECTED,USB_DC_DISCONNECTED,USB_DC_SUSPEND,USB_DC_RESUME};
enum {NRFX_POWER_USB_STATE_DISCONNECTED,NRFX_POWER_USB_STATE_CONNECTED,NRFX_POWER_USB_STATE_READY};
typedef int nrfx_err_t;
struct usbd_pwr_event {int state;};
struct nrf_usbd_ctx {uint32_t ep0_generation;bool ready,attached;void (*status_cb)(int,void *);};
static struct nrf_usbd_ctx ctx;
static int power,second_power,enabled,initialized,starts,flushes,notifications[4],init_error,blocked;
static jmp_buf dma;
static struct nrf_usbd_ctx *get_usbd_ctx(void){return &ctx;}
static unsigned irq_lock(void){return 0;}
static void irq_unlock(unsigned k){(void)k;}
static void nrfx_power_usbevt_disable(void){}
static void nrfx_power_usbevt_enable(void){}
static void nrf_usbd_common_disable(void){if(blocked)longjmp(dma,1);enabled=0;}
static void nrf_usbd_common_uninit(void){initialized=0;}
static void usbd_evt_flush(void){++flushes;}
static int eps_ctx_init(void){assert(!enabled);return 0;}
static void usbd_event_handler(void){}
static int nrf_usbd_common_init(void (*p)(void)){(void)p;if(!init_error)initialized=1;return init_error;}
static int nrfx_power_usbstatus_get(void){int p=power;if(second_power>=0){power=second_power;second_power=-1;}return p;}
static bool nrf_usbd_common_is_enabled(void){return enabled;}
static void nrf_usbd_common_enable(void){assert(initialized&&!enabled);enabled=1;}
static int hfxo_start(struct nrf_usbd_ctx *p){(void)p;return 0;}
static int hfxo_stop(struct nrf_usbd_ctx *p){(void)p;return 0;}
static void usbd_enable_endpoints(struct nrf_usbd_ctx *p){(void)p;assert(initialized&&enabled);}
static void nrf_usbd_common_start(bool sof){(void)sof;assert(initialized&&enabled);++starts;}
static void nrf_usbd_common_suspend(void){}
static bool dev_ready(void){return ctx.ready;}
static void status(int n,void *p){(void)p;++notifications[n];}
'''+fn('usbd_work_process_pwr_events')+'\n'+fn('usbd_reinit')+'''
static void reset(void){ctx=(struct nrf_usbd_ctx){.ep0_generation=7,.attached=true,.ready=true,.status_cb=status};power=NRFX_POWER_USB_STATE_READY;second_power=-1;initialized=enabled=1;starts=flushes=init_error=blocked=0;for(int i=0;i<4;i++)notifications[i]=0;}
int main(void){
 reset();usbd_reinit();assert(ctx.ready&&ctx.attached&&ctx.ep0_generation==8&&starts==1&&flushes==1&&notifications[USB_DC_CONNECTED]==1&&notifications[USB_DC_DISCONNECTED]==1);
 struct usbd_pwr_event ready={.state=USBD_POWERED};usbd_work_process_pwr_events(&ready);assert(starts==1); /* queued duplicate */
 usbd_reinit();assert(starts==2&&ctx.ep0_generation==9);
 reset();power=NRFX_POWER_USB_STATE_DISCONNECTED;usbd_reinit();assert(!ctx.ready&&!enabled&&!starts&&initialized);
 reset();power=NRFX_POWER_USB_STATE_CONNECTED;usbd_reinit();assert(!ctx.ready&&enabled&&!starts);power=NRFX_POWER_USB_STATE_READY;usbd_work_process_pwr_events(&ready);assert(ctx.ready&&starts==1);
 reset();second_power=NRFX_POWER_USB_STATE_DISCONNECTED;usbd_reinit();assert(!ctx.ready&&!starts);
 reset();init_error=1;usbd_reinit();assert(!ctx.ready&&!enabled&&!starts&&!initialized);
 reset();ctx.ep0_generation=UINT32_MAX;usbd_reinit();assert(ctx.ep0_generation==1);
 reset();blocked=1;if(!setjmp(dma)){usbd_reinit();assert(0);}assert(!ctx.ready&&enabled&&!flushes&&ctx.ep0_generation==8); /* No fictitious DMA retirement or buffer reuse. */
 puts("PASS exact USB power replay/reset-generation fence: ready/not-ready/disconnected/removal/duplicate/init failure; missing DMA completion remains an explicitly uncorrected wait");
}
'''
(out/'exact.c').write_text(src);cmd=[os.environ.get('CC') or shutil.which('clang'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'exact.c'),'-o',str(out/'test')]
passed=False
try:
 with (out/'test.log').open('x') as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'command':cmd,'generated_sha256':hashlib.sha256(body.encode()).hexdigest()},indent=2)+'\n')
print('PASS exact USB lifecycle tests')
