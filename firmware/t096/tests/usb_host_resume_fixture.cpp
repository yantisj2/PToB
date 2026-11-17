/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <nrf52840_bitfields.h>
#define unlikely(x) (x)
#define USBD_IRQn 39
#define BIT(x) (1u<<(x))
#define K_MSEC(x) (x)
#define LOG_DBG(...) ((void)0)
#define __ASSERT_NO_MSG(x) assert(x)
#define NRF_USBD_COMMON_EPIN0 0x80u
#define NRF_USBD_COMMON_EPOUT0 0u
enum {NRF_USBD_COMMON_EVT_SUSPEND,NRF_USBD_COMMON_EVT_RESUME,NRF_USBD_COMMON_EVT_WUREQ};
typedef struct {unsigned type;} nrf_usbd_common_evt_t;
static uint32_t interrupt_mask;
static bool ready_after_cause_read;
static uint32_t usb_event;
struct W1C {uint32_t value;operator uint32_t()volatile{
 uint32_t sampled=value;
 if(ready_after_cause_read){ready_after_cause_read=false;
  value|=USBD_EVENTCAUSE_USBWUALLOWED_Msk;usb_event=1;}
 return sampled;}
 void operator=(uint32_t mask)volatile{value&=~mask;}};
struct MaskRW {operator uint32_t()const volatile{return interrupt_mask;}
 void operator=(uint32_t mask)volatile{interrupt_mask=mask;}};
struct MaskSet {operator uint32_t()const volatile{return interrupt_mask;}
 void operator=(uint32_t mask)volatile{interrupt_mask|=mask;}};
struct MaskClear {void operator=(uint32_t mask)volatile{interrupt_mask&=~mask;}};
static bool instant_ready;
static unsigned lowpower_entries,wake_writes;
static W1C eventcause;
struct LowPower {uint32_t value;operator uint32_t()const volatile{return value;}
 void operator=(uint32_t v)volatile{value=v;if(v)++lowpower_entries;
  else{++wake_writes;if(instant_ready)eventcause.value|=USBD_EVENTCAUSE_USBWUALLOWED_Msk;}}};
static unsigned drives;
struct Drive {void operator=(uint32_t v)volatile{if(v)++drives;}};
static struct {
 W1C &EVENTCAUSE=eventcause;
 LowPower LOWPOWER;
 MaskRW INTEN;
 MaskSet INTENSET;
 MaskClear INTENCLR;
 uint32_t &EVENTS_USBEVENT=usb_event;
 uint32_t ENABLE,USBPULLUP,EVENTS_SOF,EVENTS_USBRESET,EVENTS_EPDATA;
 uint32_t EVENTS_EP0DATADONE,EVENTS_EP0SETUP,EPDATASTATUS,DPDMVALUE;
 Drive TASKS_DPDMDRIVE;
} regs;
#define NRF_USBD (&regs)
static bool m_bus_suspend,r2_wakeup_requested,r2_sof_requested,r2_host_resume_pending;
static uint32_t r2_host_resume_inten;
static unsigned failure,dma_available=1,irq_depth,dma_dispatches,setup_dispatches;
static unsigned resume_events,suspend_events,remote_events,begin171,end171;
enum {NRFX_DRV_STATE_POWERED_ON=1};
static unsigned m_drv_state=NRFX_DRV_STATE_POWERED_ON;
static bool enabled=true,affected171;
static void irq_enable(unsigned irq){assert(irq==USBD_IRQn);enabled=true;}
static unsigned dma_ep=0x82,m_last_setup_dir;
static int64_t now_ms;
static unsigned irq_lock(void){return irq_depth++;}
static void irq_unlock(unsigned key){assert(irq_depth==key+1);irq_depth=key;}
static bool r2_usb_common_failed(void){return failure!=0;}
static bool nrf_usbd_common_is_started(void){return enabled&&regs.ENABLE;}
static bool nrf_usbd_common_suspend_check(void){return regs.LOWPOWER.value!=0;}
static bool nrf_usbd_common_errata_171(void){return affected171;}
static void usbd_errata_171_begin(void){assert(affected171);++begin171;}
static void usbd_errata_171_end(void){assert(affected171);++end171;}
static bool r2_usb_dma_take(void){if(failure||!dma_available)return false;dma_available=0;return true;}
static void k_sem_give(unsigned *p){assert(!*p);*p=1;}
static unsigned k_sem_count_get(unsigned *p){return *p;}
static void m_event_handler(const nrf_usbd_common_evt_t *e){
 if(e->type==NRF_USBD_COMMON_EVT_RESUME)++resume_events;
 else if(e->type==NRF_USBD_COMMON_EVT_SUSPEND)++suspend_events;
 else ++remote_events;
}
static volatile uint32_t dma_end;
static volatile uint32_t *usbd_ep_to_endevent(unsigned){return &dma_end;}
static unsigned ep2bit(unsigned ep){return (ep&15)+((ep&0x80)?0:16);}
static void nrf_usbd_dma_finished(unsigned){assert(!r2_host_resume_pending);}
static void ev_epdata_handler(uint32_t){assert(!r2_host_resume_pending);}
static void ev_usbreset_handler(void){m_bus_suspend=false;r2_wakeup_requested=false;}
static void ev_setup_handler(void){assert(!r2_host_resume_pending);++setup_dispatches;}
static void ev_sof_handler(void){}
static void usbd_dmareq_process(void){assert(!r2_host_resume_pending);++dma_dispatches;}
/* PTOB_USB_HOST_RESUME_BODIES */
static void reset(bool requested=false){
 eventcause.value=regs.LOWPOWER.value=0;regs.ENABLE=1;
 regs.EVENTS_SOF=regs.EVENTS_USBEVENT=regs.EVENTS_USBRESET=0;
 regs.EVENTS_EPDATA=regs.EVENTS_EP0DATADONE=regs.EVENTS_EP0SETUP=regs.EPDATASTATUS=0;
 interrupt_mask=0;
 m_bus_suspend=r2_wakeup_requested=r2_host_resume_pending=false;
 r2_sof_requested=!requested;r2_host_resume_pending=true;r2_host_resume_inten=123;failure=0;dma_available=1;
 dma_dispatches=setup_dispatches=resume_events=suspend_events=remote_events=0;
 begin171=end171=lowpower_entries=wake_writes=drives=0;
 enabled=true;instant_ready=false;affected171=false;irq_depth=0;now_ms=0;
 ready_after_cause_read=false;
 nrf_usbd_common_start(requested);
 assert(r2_sof_requested==requested&&!r2_host_resume_pending&&!r2_host_resume_inten);
 assert(interrupt_mask==(0x01dffffdu|(requested?USBD_INTEN_SOF_Msk:0)));
}
static void suspend(void){
 m_bus_suspend=true;regs.EVENTS_SOF=1; // stale pre-idle frame
 assert(nrf_usbd_common_suspend());
 assert(regs.LOWPOWER.value&&dma_available==1&&!regs.EVENTS_SOF);
 assert(interrupt_mask&USBD_INTEN_SOF_Msk);
}
static void sof(void){regs.EVENTS_SOF=1;nrf_usbd_common_irq_handler();}
static void ready(void){eventcause.value|=USBD_EVENTCAUSE_USBWUALLOWED_Msk;
 regs.EVENTS_USBEVENT=1;nrf_usbd_common_irq_handler();}
static void resumed(bool requested){
 assert(!m_bus_suspend&&!r2_host_resume_pending&&!r2_wakeup_requested);
 assert(!regs.LOWPOWER.value&&resume_events==1&&!remote_events&&!drives);
 assert(bool(interrupt_mask&USBD_INTEN_SOF_Msk)==requested);
 assert(begin171==end171&&dma_available==1&&!irq_depth);
}
int main(void){
 unsigned cases=0;
 for(unsigned requested=0;requested<2;requested++)for(unsigned affected=0;affected<2;affected++){
  reset(requested);affected171=affected;suspend();
  // Idle alone neither wakes nor drives the host, even after many IRQ entries.
  for(unsigned n=0;n<20;n++){nrf_usbd_common_irq_handler();assert(regs.LOWPOWER.value&&!wake_writes&&!resume_events);}
  // First fresh host frame initiates only local wake. No endpoint work can run
  // until the independent PHY-ready boundary, even if events are already latched.
  unsigned old_mask=interrupt_mask;regs.EVENTS_EP0SETUP=1;sof();
  assert(r2_host_resume_pending&&!regs.LOWPOWER.value&&m_bus_suspend);
  assert(interrupt_mask==USBD_INTEN_USBEVENT_Msk&&!setup_dispatches);
  assert(!r2_wakeup_requested&&!drives&&!nrf_usbd_common_suspend());
  unsigned dispatches=dma_dispatches;
  sof();assert(dma_dispatches==dispatches&&!setup_dispatches);
  // An unrelated/stale USBEVENT cannot spin the ISR or fake PHY readiness.
  eventcause.value=USBD_EVENTCAUSE_ISOOUTCRC_Msk;regs.EVENTS_USBEVENT=1;
  nrf_usbd_common_irq_handler();
  assert(r2_host_resume_pending&&!regs.EVENTS_USBEVENT);
  ready();resumed(requested);assert(setup_dispatches==1&&dma_dispatches>dispatches);
  assert(interrupt_mask==(old_mask&~(requested?0u:USBD_INTEN_SOF_Msk)));++cases;
  // Completion in the same ISR is not consumed/lost before the wake fence.
  reset(requested);affected171=affected;suspend();instant_ready=true;
  sof();assert(r2_host_resume_pending);ready();resumed(requested);++cases;
  // Host RESUME can supply the ready proof without generating remote wake.
  reset(requested);affected171=affected;suspend();sof();
  eventcause.value|=USBD_EVENTCAUSE_RESUME_Msk;regs.EVENTS_USBEVENT=1;
  nrf_usbd_common_irq_handler();resumed(requested);++cases;
  // A first frame before low-power entry cancels a stale queued suspend.
  reset(requested);m_bus_suspend=true;sof();resumed(requested);
  assert(!nrf_usbd_common_suspend()&&!lowpower_entries);++cases;
 }
 for(unsigned variant=0;variant<3;variant++){
  reset();suspend();if(variant==0)failure=2;if(variant==1)enabled=false;if(variant==2)regs.ENABLE=0;
  sof();assert(regs.LOWPOWER.value&&!wake_writes&&!resume_events&&!drives);++cases;
 }
 reset();suspend();sof();for(unsigned n=0;n<100;n++)sof();
 assert(r2_host_resume_pending&&!dma_dispatches&&!setup_dispatches&&!drives);++cases;
 // A readiness edge arriving just after the cause sample keeps its IRQ.
 reset();suspend();sof();regs.EVENTS_USBEVENT=1;
 eventcause.value=USBD_EVENTCAUSE_ISOOUTCRC_Msk;ready_after_cause_read=true;
 nrf_usbd_common_irq_handler();assert(r2_host_resume_pending&&regs.EVENTS_USBEVENT);
 nrf_usbd_common_irq_handler();resumed(false);++cases;
 // A new legitimate suspend during MAC wake is retained through readiness.
 reset();suspend();sof();eventcause.value=USBD_EVENTCAUSE_SUSPEND_Msk;
 regs.EVENTS_USBEVENT=1;nrf_usbd_common_irq_handler();
 assert(r2_host_resume_pending&&!regs.EVENTS_USBEVENT);
 regs.EVENTS_SOF=1;ready();assert(!r2_host_resume_pending&&m_bus_suspend&&resume_events==1&&suspend_events==1);
 assert(nrf_usbd_common_suspend()&&regs.LOWPOWER.value&&!drives);++cases;
 // Repeated cycles stay in one boot and retain the caller's SOF policy.
 for(unsigned requested=0;requested<2;requested++){
  reset(requested);
  for(unsigned cycle=0;cycle<100;cycle++){
   suspend();sof();ready();
   assert(!m_bus_suspend&&!r2_host_resume_pending&&resume_events==cycle+1&&!drives);
   assert(bool(interrupt_mask&USBD_INTEN_SOF_Msk)==bool(requested));++cases;
  }
 }
 // A terminal fault cannot leave a pending IRQ storm that starves its watchdog.
 reset();suspend();sof();failure=2;regs.EVENTS_USBEVENT=1;
 nrf_usbd_common_irq_handler();assert(!interrupt_mask&&r2_host_resume_pending&&!drives);++cases;
 printf("PASS %u actual host-SOF/IRQ/MAC-ready boundary cases; no hardware\n",cases);
}
