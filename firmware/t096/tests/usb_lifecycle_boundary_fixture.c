/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Synthetic DMA/clock boundary around unmodified generated driver bodies. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#define CFG_EPIN_CNT 2
#define CFG_EPOUT_CNT 2
#define CFG_EP_ISOIN_CNT 0
#define CFG_EP_ISOOUT_CNT 0
#define NRF_USBD_EPIN(n) (0x80|(n))
#define NRF_USBD_EPOUT(n) (n)
#define BUILD_ASSERT(x) _Static_assert(x,"build")
#define __ASSERT_NO_MSG(x) assert(x)
#define LOG_DBG(...) ((void)0)
#define K_FOREVER 0xffffffffu
#define IRQ_CONNECT(...) ((void)0)
#define DT_INST_IRQN(...) 39
#define DT_INST_IRQ(...) 1
#define COND_CODE_1(...) 0
#define NRFX_POWER_USB_STATE_DISCONNECTED 0
#define NRFX_POWER_USB_EVT_DETECTED 1
struct nrf_usbd_ep_ctx {struct {uint8_t addr;bool en;} cfg;struct {struct {uint8_t *data;} block;uint8_t *data,*curr;uint32_t len;} buf;bool write_in_progress,read_complete,read_pending,trans_zlp;};
struct nrf_usbd_ctx {bool attached,ready;int drv_lock;void *hfxo_mgr;uint32_t ep0_generation;};
static struct nrf_usbd_ctx ctx;
static struct nrf_usbd_ep_ctx ins[9],outs[9],before_ep;
static uint8_t ep_out_bufs[9][64],ep_isoout_bufs[1][64],payload[64],other[64];
static struct nrf_usbd_ep_ctx *watched;
static bool failed,enabled,owned,abort_fails,close_fails,early_mutation,fail_on_lock;
static unsigned aborts,drops,clocks_released,power_disabled,power_enabled,power_events,flushes,irq_depth;
static struct nrf_usbd_ctx *get_usbd_ctx(void){return &ctx;}
static bool r2_usb_common_failed(void){return failed;}
static bool dev_attached(void){return ctx.attached;}
static bool dev_ready(void){return ctx.ready&&!failed;}
static struct nrf_usbd_ep_ctx *in_endpoint_ctx(unsigned i){return &ins[i&15];}
static struct nrf_usbd_ep_ctx *out_endpoint_ctx(unsigned i){return &outs[i&15];}
static struct nrf_usbd_ep_ctx *endpoint_ctx(unsigned i){return i&0x80?in_endpoint_ctx(i):out_endpoint_ctx(i);}
static unsigned ep_addr_to_nrfx(unsigned ep){return ep;}
static bool nrf_usbd_common_is_enabled(void){return enabled;}
static void nrf_usbd_common_ep_abort(unsigned ep){
 if(failed)return;
 ++aborts;struct nrf_usbd_ep_ctx *e=endpoint_ctx(ep);
 if(e==watched&&memcmp(e,&before_ep,sizeof *e))early_mutation=true;
 if(abort_fails){failed=true;return;}
 owned=false;
}
static void nrf_usbd_common_transfer_out_drop(unsigned ep){(void)ep;assert(!owned&&!failed);++drops;}
static void nrf_usbd_common_disable(void){if(close_fails){failed=true;return;}assert(!owned);enabled=false;}
static unsigned irq_lock(void){assert(!irq_depth);return irq_depth++;}
static void irq_unlock(unsigned k){assert(irq_depth==1&&k==0);irq_depth=0;}
static void k_mutex_init(int *p){assert(!*p);*p=0;}
static void k_mutex_lock(int *p,unsigned t){assert(!*p&&t==K_FOREVER);*p=1;if(fail_on_lock)failed=true;}
static void k_mutex_unlock(int *p){assert(*p==1);*p=0;}
static void usbd_evt_flush(void){++flushes;}
static int hfxo_stop(struct nrf_usbd_ctx *p){(void)p;++clocks_released;return 0;}
static void nrfx_power_usbevt_disable(void){++power_disabled;}
static void nrfx_power_usbevt_enable(void){++power_enabled;}
static void *z_nrf_clock_control_get_onoff(unsigned n){(void)n;return &ctx;}
static int usbd_evt_fifo;
static bool k_fifo_is_empty(void *p){(void)p;return true;}
static void usbd_work_schedule(void){}
static unsigned nrfx_power_usbstatus_get(void){return 2;}
static void usb_dc_power_event_handler(unsigned n){assert(n==1);++power_events;}

/* PTOB_USB_DRIVER_BODIES */

static void setup(unsigned ep){
 memset(ins,0,sizeof ins);memset(outs,0,sizeof outs);
 for(unsigned i=0;i<9;i++){ins[i].cfg.addr=0x80|i;outs[i].cfg.addr=i;}
 ctx=(struct nrf_usbd_ctx){.attached=true,.ready=true,.ep0_generation=UINT32_MAX};
 enabled=owned=true;failed=abort_fails=close_fails=early_mutation=fail_on_lock=false;
 aborts=drops=clocks_released=power_disabled=power_enabled=power_events=flushes=0;
 watched=endpoint_ctx(ep);watched->cfg.en=true;
 watched->buf.block.data=payload;watched->buf.data=other;watched->buf.curr=other+7;watched->buf.len=23;
 watched->write_in_progress=!!(ep&0x80);watched->read_pending=!(ep&0x80);watched->trans_zlp=true;
 before_ep=*watched;memset(payload,0xa5,sizeof payload);memset(other,0x5a,sizeof other);
}
int main(int argc,char **argv){assert(argc==2);int mode=atoi(argv[1]);unsigned ep=(mode&1)?0:0x80;setup(ep);
 if(mode<4){
  abort_fails=mode>=2;ep_ctx_reset(watched);
#ifdef OLD_NEGATIVE
  assert(owned||early_mutation);puts("OLD_RESET_PUBLISHES_BEFORE_DMA_RETIREMENT");return 42;
#else
  assert(aborts==1&&!early_mutation);
  if(abort_fails){assert(failed&&owned&&!memcmp(watched,&before_ep,sizeof before_ep));/* Late hardware completion can still target unchanged storage. */other[7]=0x33;assert(watched->buf.curr==other+7&&watched->buf.len==23);}
  else{assert(!failed&&!owned&&watched->buf.data==payload&&watched->buf.curr==payload&&!watched->buf.len&&watched->read_complete&&!watched->read_pending&&!watched->write_in_progress);}
#endif
 }else if(mode==4){
  abort_fails=true;struct nrf_usbd_ep_ctx old_next=ins[1];assert(eps_ctx_init()==-EIO);assert(aborts==1&&!early_mutation&&owned);assert(!memcmp(&ins[1],&old_next,sizeof old_next)&&!outs[0].buf.block.data);
 }else if(mode>=5&&mode<=8){
  abort_fails=mode==6||mode==7;fail_on_lock=mode==8;int rc=usb_dc_ep_flush(ep);
  if(abort_fails||fail_on_lock){assert(rc==-EIO&&!memcmp(watched,&before_ep,sizeof before_ep)&&owned&&!drops);}
  else{assert(!rc&&!owned&&aborts==1&&drops==1&&!watched->buf.len&&watched->buf.curr==other);}
  assert(!ctx.drv_lock&&!early_mutation);
 }else if(mode==9){
  close_fails=true;assert(usb_dc_detach()==-EIO);assert(ctx.attached&&!ctx.ready&&ctx.ep0_generation==1&&!ctx.drv_lock&&enabled&&owned&&!clocks_released&&!power_disabled);assert(usb_dc_attach()==-EIO&&!power_enabled&&!power_events);
 }else if(mode==10){
  owned=false;assert(!usb_dc_reset());assert(ctx.attached&&!ctx.ready&&ctx.ep0_generation==1&&!enabled&&clocks_released==1&&power_disabled==1&&power_enabled==1&&power_events==1);
 }else if(mode==11){
  ctx.attached=false;abort_fails=true;assert(usb_dc_attach()==-EIO);assert(!ctx.attached&&failed&&owned&&!early_mutation&&!power_events);
 }else assert(false);
 puts("PASS exact driver buffer/lifecycle fence");return 0;
}
