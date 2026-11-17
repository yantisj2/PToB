/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define __ASSERT_NO_MSG(x) assert(x)
#define NRF_USBD_COMMON_USE_WORKAROUND_FOR_ANOMALY_211 1
#define NRF_USBD_COMMON_EPIN0 0x80u
#define NRF_USBD_COMMON_EPOUT0 0u
#define NRF_USBD_COMMON_EPIN8 0x88u
#define NRF_USBD_COMMON_EP_IS_OUT(ep) (!((ep)&0x80u))
#define NRF_USBD_COMMON_EP_IS_IN(ep) (!!((ep)&0x80u))
#define NRF_USBD_COMMON_EP_NUM(ep) ((ep)&15u)
#define NRF_USBD_COMMON_EP_ABORTED 1
#define NRF_USBD_COMMON_EP_OK 0
#define NRF_USBD_COMMON_EPIN_CNT 9u
#define NRF_USBD_COMMON_EPIN_BITPOS_0 0u
#define USBD_LOWPOWER_LOWPOWER_LowPower 1u
#define USBD_LOWPOWER_LOWPOWER_ForceNormal 0u
#define USBD_LOWPOWER_LOWPOWER_Pos 0u
#define USBD_EVENTCAUSE_RESUME_Msk 2u
#define USBD_EVENTCAUSE_READY_Msk 1u
#define USBD_EVENTCAUSE_USBWUALLOWED_Msk 4u
#define USBD_ISOSPLIT_SPLIT_HalfIN 0u
#define USBD_ISOSPLIT_SPLIT_Pos 0u
#define USBD_ISOINCONFIG_RESPONSE_ZeroData 0u
#define USBD_ISOINCONFIG_RESPONSE_NoResp 1u
#define USBD_ISOINCONFIG_RESPONSE_Pos 0u
#define CONFIG_NRF_USBD_ISO_IN_ZLP 0
#define IS_ENABLED(x) (x)
#define K_FOREVER 0xffffffffu
#define K_NO_WAIT 0
#define K_MSEC(x) (x)
#define USBD_INTEN_SOF_Msk 2u
#define USBD_IRQn 39
#define BIT(x) (1u<<(x))
enum {NRFX_DRV_STATE_UNINITIALIZED,NRFX_DRV_STATE_INITIALIZED,NRFX_DRV_STATE_POWERED_ON};
typedef unsigned nrf_usbd_common_ep_t;
typedef struct {unsigned type;} nrf_usbd_common_evt_t;
#define NRF_USBD_COMMON_EP_TRANSFER_EVENT(evt,ep,status) nrf_usbd_common_evt_t evt={0}
typedef struct {union {uint8_t *rx;uint32_t addr;} p_data;size_t size;} nrf_usbd_common_ep_transfer_t;
typedef nrf_usbd_common_ep_transfer_t nrf_usbd_common_transfer_t;
typedef int nrfx_err_t;
enum {NRFX_SUCCESS,NRFX_ERROR_INVALID_STATE,NRFX_ERROR_INVALID_ADDR,NRFX_ERROR_BUSY};
#define NRF_USBD_COMMON_FAILED_TRANSFERS_DEBUG 0
#define NRF_USBD_COMMON_DMAREQ_PROCESS_DEBUG 0
#define NRF_USBD_COMMON_ISO_DEBUG 0
#define NRF_USBD_COMMON_EP_IS_ISO(ep) ((ep&15)>=8)
#define NRF_USBD_COMMON_EPIN_BIT_MASK 0xffffu
#define NRF_USBD_COMMON_EP_OVERLOAD 2
#define NRFX_USBD_CONFIG_DMASCHEDULER_ISO_BOOST 0
#define USBD_EPISO_BIT_MASK 0
#define LOG_DBG(...) ((void)0)
typedef struct {unsigned status,max_packet_size,transfer_cnt;bool more_transactions;nrf_usbd_common_ep_transfer_t transfer_state;} usbd_ep_state_t;
static usbd_ep_state_t ep_state;
static volatile unsigned dma_available;
static bool m_bus_suspend,irq_enabled,affected171,affected211,affected223,m_first_enable,r2_wakeup_requested,r2_sof_requested,r2_host_resume_pending;
static unsigned r2_host_resume_inten;
static unsigned m_drv_state,m_dma_odd,r2_dma_completions,m_ep_dma_waiting,m_ep_ready,m_last_setup_dir,dma_ep;
static unsigned now_ms,wake_at,ready_at,done_at,normal_dma_at,delayed,stops,starts,aborts,clears;
static unsigned race,removed_at,mask_depth,lp_reads,invalid_access,drives,begins171,ends171,fault_at;
static bool wake_missing,ready_missing,parity_missing,removed,inside_reset;
static void boundary(unsigned);
struct W1C {
 volatile uint32_t value;
 operator uint32_t() const volatile {return value;}
 volatile uint32_t *operator&() volatile {return &value;}
 void operator=(uint32_t mask) volatile {value&=~mask;if(mask==4)boundary(5);}
};
struct LowPower {
 volatile uint32_t value;
 operator uint32_t() const volatile {return value;}
 void operator=(uint32_t v) volatile;
};
struct Enable {
 volatile uint32_t value;
 operator uint32_t() const volatile {return value;}
 void operator=(uint32_t v) volatile;
};
struct Drive {volatile uint32_t value;void operator=(uint32_t v) volatile {value=v;++drives;}};
static struct {
 W1C EVENTCAUSE;
 LowPower LOWPOWER;
 Enable ENABLE;
 volatile uint32_t USBPULLUP,INTEN,INTENSET,EVENTS_SOF,ISOSPLIT,ISOINCONFIG,EPINEN,EPOUTEN;
 volatile uint32_t EVENTS_ENDEPIN[9];
 volatile uint32_t DPDMVALUE;
 Drive TASKS_DPDMDRIVE;
} regs;
#define NRF_USBD (&regs)
typedef unsigned atomic_t;
static atomic_t r2_lifecycle_failure;
static unsigned atomic_get(atomic_t *p){return *p;}
static bool atomic_cas(atomic_t *p,unsigned old,unsigned value){if(*p!=old)return false;*p=value;return true;}
static void host_resume(void){regs.LOWPOWER.value=0;regs.EVENTCAUSE.value|=2;wake_at=0;}
static void boundary(unsigned point){
 if(race==point){race=0;host_resume();}
 if(removed_at==point){removed_at=0;removed=true;}
}
void LowPower::operator=(uint32_t v) volatile {
 boundary(6);bool was=value;value=v;
 if(was&&!v&&!wake_missing)wake_at=now_ms+2;
}
void Enable::operator=(uint32_t v) volatile {
 value=v;
 if(v&&!ready_missing)ready_at=now_ms+2;
 if(!v){regs.LOWPOWER.value=0;wake_at=ready_at=done_at=0;}
}
static void nrf_usbd_dma_finished(unsigned);
static void r2_usb_fail(unsigned);
static void k_sleep(unsigned ms){
 assert(!mask_depth);now_ms+=ms;boundary(7);
 if(wake_at&&now_ms>=wake_at){regs.EVENTCAUSE.value|=4;wake_at=0;}
 if(ready_at&&now_ms>=ready_at){regs.EVENTCAUSE.value|=1;ready_at=0;}
 if(done_at&&now_ms>=done_at){regs.EVENTS_ENDEPIN[0]=1;done_at=0;}
 if(normal_dma_at&&now_ms>=normal_dma_at&&irq_enabled){normal_dma_at=0;nrf_usbd_dma_finished(dma_ep);}
 if(fault_at&&now_ms>=fault_at){fault_at=0;r2_usb_fail(2);}
}
static int64_t k_uptime_get(void){return now_ms;}
static unsigned irq_lock(void){boundary(2);return mask_depth++;}
static void irq_unlock(unsigned key){assert(mask_depth==key+1);mask_depth=key;}
static void NVIC_ClearPendingIRQ(unsigned irq){(void)irq;}
static bool irq_is_enabled(unsigned irq){(void)irq;return irq_enabled;}
static void irq_disable(unsigned irq){(void)irq;irq_enabled=false;boundary(3);}
static void usbd_ep_abort_all(void){assert(dma_available==0);++stops;}
static int k_sem_take(volatile unsigned *sem,unsigned timeout){
 unsigned at=now_ms;
 while(!*sem){if(timeout!=K_FOREVER&&now_ms-at>=timeout)return -1;k_sleep(1);}
 --*sem;return 0;
}
static unsigned k_sem_count_get(volatile unsigned *sem){return *sem;}
static void k_sem_give(volatile unsigned *sem){assert(*sem==0);++*sem;}
static bool nrf_usbd_common_errata_211(void){return affected211;}
static bool nrf_usbd_common_errata_187(void){return false;}
static bool nrf_usbd_common_errata_171(void){return affected171;}
static bool nrf_usbd_common_errata_166(void){return false;}
static bool nrf_usbd_common_errata_223(void){return affected223;}
static bool nrf_usbd_common_is_started(void){return m_drv_state==NRFX_DRV_STATE_POWERED_ON&&irq_enabled;}
static bool nrfx_is_in_ram(void *p){(void)p;return true;}
static void usbd_errata_171_begin(void){assert(affected171);++begins171;}
static void usbd_errata_171_end(void){assert(affected171);++ends171;}
static void usbd_errata_187_211_begin(void){}
static void usbd_errata_187_211_end(void){}
static void __DSB(void){}
static void __ISB(void){}
static void usbd_dma_pending_clear(void){++clears;}
static usbd_ep_state_t *ep_state_access(unsigned ep){(void)ep;return &ep_state;}
static unsigned usbd_ep_amount_get(unsigned ep){(void)ep;return 1;}
static unsigned ep2bit(unsigned ep){return (ep&15)+((ep&0x80)?0:16);}
static void m_event_handler(const nrf_usbd_common_evt_t *evt){(void)evt;}
static void nrf_usbd_common_setup_data_clear(void){}
static bool nrf_usbd_common_suspend_check(void){
 ++lp_reads;boundary(lp_reads==1?1:4);return regs.LOWPOWER.value!=0;
}
static void usbd_ep_dma_start(unsigned ep,uint32_t addr,size_t size){
 (void)addr;assert((ep&0x80)&&size==1&&dma_available==0);++starts;
 if(regs.LOWPOWER.value||!regs.ENABLE.value||wake_at){++invalid_access;return;}
 if(!parity_missing)done_at=now_ms+delayed;
 if(!delayed&&!parity_missing){regs.EVENTS_ENDEPIN[0]=1;done_at=0;}
}
static void usbd_ep_abort(unsigned ep){(void)ep;assert(dma_available==0);++aborts;}
static void usbd_int_rise(void){}
static bool nrf_usbd_common_feeder(nrf_usbd_common_ep_transfer_t *t,nrf_usbd_common_ep_transfer_t *s,unsigned max){(void)s;(void)max;t->p_data.addr=1;t->size=1;return false;}
static bool nrf_usbd_common_consumer(nrf_usbd_common_ep_transfer_t *t,nrf_usbd_common_ep_transfer_t *s,unsigned max,unsigned rx){(void)s;(void)max;(void)rx;t->p_data.rx=NULL;t->size=0;return false;}
static unsigned nrf_usbd_common_epout_size_get(unsigned ep){(void)ep;return 0;}
static unsigned usbd_dma_scheduler_algorithm(unsigned mask){return __builtin_ctz(mask);}
static unsigned bit2ep(unsigned pos){return pos<16?pos|0x80:pos-16;}
static void usbd_dma_pending_set(void){}
#define USBD_DPDMVALUE_STATE_Resume 1u
#define USBD_DPDMVALUE_STATE_Pos 0u
#define NRF_USBD_COMMON_EVT_SUSPEND 1u
#define NRF_USBD_COMMON_EVT_RESUME 2u
#define NRF_USBD_COMMON_EVT_WUREQ 3u
#define USBD_EVENTCAUSE_ISOOUTCRC_Msk 8u
#define USBD_EVENTCAUSE_SUSPEND_Msk 16u
#include R2_COMMON_BODIES
static void reset(void){
 regs.EVENTCAUSE.value=regs.LOWPOWER.value=0;regs.ENABLE.value=regs.USBPULLUP=regs.INTEN=1;
 for(unsigned i=0;i<9;i++)regs.EVENTS_ENDEPIN[i]=0;
 m_bus_suspend=r2_wakeup_requested=r2_host_resume_pending=r2_sof_requested=false;r2_host_resume_inten=0;irq_enabled=true;affected171=affected223=false;affected211=true;m_first_enable=true;
 dma_available=1;m_dma_odd=1;m_drv_state=NRFX_DRV_STATE_POWERED_ON;dma_ep=0x81;
 r2_lifecycle_failure=0;now_ms=wake_at=ready_at=done_at=normal_dma_at=delayed=0;
 stops=starts=aborts=clears=lp_reads=invalid_access=drives=begins171=ends171=0;
 race=removed_at=mask_depth=fault_at=0;removed=wake_missing=ready_missing=parity_missing=false;
}
static void closed(void){
 assert(!r2_lifecycle_failure&&!regs.ENABLE.value&&!irq_enabled&&dma_available==1);
 assert(m_drv_state==NRFX_DRV_STATE_INITIALIZED&&!m_dma_odd&&stops==1);
 assert(!invalid_access&&!drives&&now_ms<=40);
 assert(begins171==ends171);
}
int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"negative-suspend")){
  reset();regs.LOWPOWER.value=1;m_bus_suspend=true;nrf_usbd_common_disable();
  assert(false&&"old suspended odd disable must not complete");
 }
 if(argc>1&&!strcmp(argv[1],"negative-held")){
  reset();dma_available=0;nrf_usbd_common_disable();assert(false);
 }
 if(argc>1&&!strcmp(argv[1],"negative-ready")){
  reset();regs.ENABLE.value=0;m_drv_state=NRFX_DRV_STATE_INITIALIZED;ready_missing=true;
  nrf_usbd_common_enable();assert(false);
 }
 unsigned cases=0;
 reset();r2_host_resume_pending=true;ep_state.more_transactions=true;m_ep_dma_waiting=m_ep_ready=1;
 usbd_dmareq_process();assert(!starts&&dma_available==1);++cases;

 for(unsigned a=0;a<2;a++)for(unsigned r=0;r<=7;r++)for(unsigned cut=0;cut<=7;cut++){
  reset();regs.LOWPOWER.value=1;m_bus_suspend=true;affected171=a;race=r;removed_at=cut;delayed=3;
  nrf_usbd_common_disable();closed();++cases;
  /* No pending WUALLOWED survives to a future start. */
  assert(!(regs.EVENTCAUSE.value&4));
  m_bus_suspend=true;ev_usbevent_handler();assert(!drives); /* next suspend cannot consume an old WUREQ */
 }
 reset();r2_host_resume_pending=true;r2_host_resume_inten=0x1dffffd;wake_at=2;delayed=3;
 nrf_usbd_common_disable();closed();assert(!r2_host_resume_pending&&!r2_host_resume_inten);++cases;
 reset();delayed=19;nrf_usbd_common_disable();closed();++cases;
 reset();dma_available=0;normal_dma_at=19;delayed=0;
 nrf_usbd_common_disable();closed();assert(r2_dma_completions);++cases;
 reset();dma_available=0;nrf_usbd_common_disable();
 assert(r2_lifecycle_failure==1&&now_ms==20&&dma_available==0&&irq_enabled&&regs.ENABLE.value);
 assert(!starts&&!stops);normal_dma_at=25;k_sleep(5);
 assert(dma_available==1&&r2_lifecycle_failure==1); /* actual late completion only */
 m_ep_dma_waiting=m_ep_ready=2;ep_state.more_transactions=true;
 usbd_dmareq_process();assert(!starts); /* late completion may not launch queued DMA */
 nrf_usbd_common_transfer_t offered={};
 assert(nrf_usbd_common_ep_transfer(0x81,&offered)==NRFX_ERROR_INVALID_STATE&&!starts);
 nrf_usbd_common_disable();assert(!stops);++cases;
 reset();dma_available=0;fault_at=5;normal_dma_at=10;
 assert(!r2_usb_dma_take()&&now_ms==10&&dma_available==1&&r2_lifecycle_failure==2);
 r2_usb_fail(4);assert(r2_lifecycle_failure==2);++cases;
 reset();irq_enabled=false;m_dma_odd=0;nrf_usbd_common_disable();
 assert(!stops&&!r2_lifecycle_failure&&!regs.ENABLE.value&&dma_available==1);++cases;
 reset();regs.LOWPOWER.value=1;m_bus_suspend=true;wake_missing=true;nrf_usbd_common_disable();
 assert(r2_lifecycle_failure==2&&now_ms==20&&dma_available==0&&!irq_enabled&&regs.ENABLE.value);
 assert(!starts&&!stops&&!invalid_access&&!drives);++cases;
 reset();parity_missing=true;nrf_usbd_common_disable();
 assert(r2_lifecycle_failure==3&&now_ms==20&&dma_available==0&&m_dma_odd==1&&!irq_enabled&&regs.ENABLE.value);
 regs.EVENTS_ENDEPIN[0]=1;nrf_usbd_common_disable();
 assert(r2_lifecycle_failure==3&&dma_available==0&&m_dma_odd==1);++cases;
 for(unsigned a=0;a<2;a++)for(unsigned b=0;b<2;b++){
  reset();regs.ENABLE.value=0;m_drv_state=NRFX_DRV_STATE_INITIALIZED;affected171=a;affected223=b;
  nrf_usbd_common_enable();assert(m_drv_state==NRFX_DRV_STATE_POWERED_ON&&!r2_lifecycle_failure);
  assert(begins171==ends171&&now_ms<=4);++cases;
 }
 reset();regs.ENABLE.value=0;m_drv_state=NRFX_DRV_STATE_INITIALIZED;ready_missing=true;affected171=true;
 nrf_usbd_common_enable();assert(r2_lifecycle_failure==4&&now_ms==20&&!regs.ENABLE.value&&dma_available==1);
 assert(m_drv_state==NRFX_DRV_STATE_INITIALIZED&&begins171==ends171);++cases;
 reset();m_bus_suspend=true;assert(nrf_usbd_common_suspend()&&regs.LOWPOWER.value&&dma_available==1);
 assert(!r2_lifecycle_failure&&!drives&&!stops);++cases;
 reset();regs.LOWPOWER.value=1;m_bus_suspend=true;nrf_usbd_common_disable();closed();
 nrf_usbd_common_enable();irq_enabled=true;m_bus_suspend=true;regs.LOWPOWER.value=1;
 regs.EVENTCAUSE.value|=4;ev_usbevent_handler();assert(!drives);++cases;
 reset();regs.LOWPOWER.value=1;m_bus_suspend=true;
 assert(nrf_usbd_common_wakeup_req()&&r2_wakeup_requested);k_sleep(2);
 ev_usbevent_handler();assert(drives==1&&!r2_wakeup_requested);++cases;
 printf("PASS %u exact common lifecycle cases: W1C events,host-resume/removal boundaries,local-close171/211,late actual DMA,missingPHY/parity/READY; only explicit requested DPDM drive\n",cases);
}
