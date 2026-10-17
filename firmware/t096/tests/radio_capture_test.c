/* Actual ISR adapter with only public nRF/PPI boundary replaced. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#define NRFX_SUCCESS 0x0bad0000u
#define NRF_RADIO ((void *)1)
#define NRF_TIMER3 ((void *)2)
#define NRF_RADIO_EVENT_ADDRESS 1
#define NRF_TIMER_CC_CHANNEL1 1
typedef unsigned nrf_ppi_channel_t;
static bool address=true,route_on,disable_fault;
static uint32_t cc=123;
static unsigned irq_lock(void){return 0;}
static void irq_unlock(unsigned k){(void)k;}
static unsigned nrfx_ppi_channel_enable(unsigned ch){assert(ch==6);route_on=true;return NRFX_SUCCESS;}
static unsigned nrfx_ppi_channel_disable(unsigned ch){assert(ch==6);if(disable_fault)return 1;route_on=false;return NRFX_SUCCESS;}
static bool nrf_radio_event_check(void *reg,unsigned event){assert(reg==NRF_RADIO&&event==1);return address;}
static uint32_t nrf_timer_cc_get(void *reg,unsigned channel){assert(reg==NRF_TIMER3&&channel==1);return cc;}
#include "../src/radio_capture.c"
int main(void){
 uint8_t pdu[161]={7,159,0,157,255,255,255};int owner,other;
 assert(!r2_radio_capture_enable(1,6));
 r2_radio_capture_arm(&owner,pdu,65535,36,1,true);assert(route_on);
 r2_radio_capture_metadata(&other,100,200,300,400);
 r2_radio_capture_metadata(&owner,1700,1000,63,218);
 assert(r2_radio_capture_disable()==-EBUSY&&route_on);
 r2_radio_capture_complete(&owner,true,true);assert(!route_on);
 struct r2_radio_record out;assert(r2_radio_capture_pop(&out)&&out.address_raw==123&&out.raw==123&&out.epoch==1&&out.owner==(uintptr_t)&owner&&out.length==158&&out.event==65535&&out.channel==36&&out.tx&&!memcmp(out.ad,pdu+3,158));
 assert(out.reservation_us==1700&&out.prepare_us==1000&&out.widening_us==63&&out.receive_window_us==218);
 assert(!r2_radio_capture_disable());assert(!r2_radio_capture_enable(2,6));
 r2_radio_capture_arm(&owner,pdu,0,0,1,false);r2_radio_capture_abort(&other);assert(route_on);
 disable_fault=true;r2_radio_capture_complete(&owner,true,true);assert(route_on&&r2_radio_capture_disable()==-EBUSY);
 disable_fault=false;r2_radio_capture_abort(&owner);assert(!route_on);assert(!r2_radio_capture_disable());
 assert(!r2_radio_capture_enable(3,6));
 r2_radio_capture_arm(&owner,pdu,0,0,1,false);r2_radio_capture_complete(&owner,true,false);assert(!r2_radio_capture_pop(&out));
 address=false;r2_radio_capture_arm(&owner,pdu,0,0,1,true);r2_radio_capture_complete(&owner,true,true);assert(!r2_radio_capture_pop(&out));address=true;
 for(unsigned i=0;i<17;i++){cc=i;r2_radio_capture_arm(&owner,pdu,i,0,1,true);r2_radio_capture_complete(&owner,true,true);}
 struct r2_radio_capture_stats s;r2_radio_capture_stats(&s);assert(s.overflow==1&&!s.armed);
 for(unsigned i=0;i<16;i++)assert(r2_radio_capture_pop(&out)&&out.address_raw==i&&out.raw==i&&out.epoch==3);
 assert(!r2_radio_capture_pop(&out)&&!r2_radio_capture_disable());
 assert(!r2_radio_capture_enable(4,6));
 uint8_t relay_pdu[193]={7,191,0,189,255,255,255};
 relay_pdu[192]=0xa5;
 r2_radio_capture_arm(&other,relay_pdu,7,13,1,false);
 assert(r2_radio_capture_flush()==-EBUSY&&route_on);
 r2_radio_capture_complete(&other,true,true);
 assert(r2_radio_capture_pop(&out)&&out.length==190&&out.owner==(uintptr_t)&other&&out.ad[189]==0xa5&&!out.tx&&out.address_raw==cc&&out.raw==cc);
 assert(!out.reservation_us&&!out.prepare_us&&!out.widening_us&&!out.receive_window_us);
 r2_radio_capture_arm(&other,relay_pdu,8,13,1,false);r2_radio_capture_complete(&other,true,true);
 assert(r2_radio_capture_flush()==1&&!r2_radio_capture_pop(&out)&&!r2_radio_capture_flush());
 relay_pdu[1]=192;r2_radio_capture_arm(&other,relay_pdu,8,13,1,false);
 r2_radio_capture_complete(&other,true,true);assert(!r2_radio_capture_pop(&out));
 assert(!r2_radio_capture_disable());
 assert(r2_radio_address_reference(UINT32_MAX-5,true)==UINT32_MAX-5);
 assert(r2_radio_address_reference(100,false)==100);
 for(unsigned i=0;i<2;i++){cc=i?UINT32_MAX:0;r2_radio_capture_enable(5+i,6);r2_radio_capture_arm(&owner,pdu,0,0,1,false);r2_radio_capture_complete(&owner,true,true);assert(r2_radio_capture_pop(&out)&&out.address_raw==cc&&out.raw==cc);assert(!r2_radio_capture_disable());}
 struct r2_radio_rx_stats before; r2_radio_capture_stats(&s);before=s.rx;
 assert(!r2_radio_capture_enable(10,6));
 r2_radio_capture_arm(&owner,pdu,10,3,1,false);
 r2_radio_capture_abort_site(&other,R2_CAPTURE_ABORT_ACTIVE_CANCEL);assert(route_on);
 r2_radio_capture_abort_site(&owner,R2_CAPTURE_ABORT_ACTIVE_CANCEL);
 r2_radio_capture_stats(&s);assert(s.rx.aborted==before.aborted+1);
 assert(s.rx.last_abort==((UINT64_C(10)<<32)|(UINT64_C(1)<<24)|(UINT64_C(3)<<16)|10));
 assert(s.rx.last_abort_owner==(uint32_t)(uintptr_t)&owner);
 r2_radio_capture_arm(&owner,pdu,11,4,1,false);address=false;r2_radio_capture_complete(&owner,false,false);
 r2_radio_capture_arm(&owner,pdu,12,4,1,false);r2_radio_capture_complete(&owner,true,false);
 address=true;r2_radio_capture_arm(&owner,pdu,13,4,1,false);r2_radio_capture_complete(&owner,true,false);
 pdu[0]=0;r2_radio_capture_arm(&owner,pdu,14,4,1,false);r2_radio_capture_complete(&owner,true,true);pdu[0]=7;
 r2_radio_capture_arm(&owner,pdu,15,4,1,false);r2_radio_capture_complete(&owner,true,true);assert(r2_radio_capture_pop(&out));
 r2_radio_capture_arm(&owner,pdu,16,4,1,true);r2_radio_capture_abort_site(&owner,R2_CAPTURE_ABORT_TX_CHAIN);
 r2_radio_capture_stats(&s);
 assert(s.rx.armed==before.armed+6&&s.rx.completed==before.completed+1&&s.rx.aborted==before.aborted+1);
 assert(s.rx.no_done==before.no_done+1&&s.rx.no_address==before.no_address+1&&s.rx.bad_crc==before.bad_crc+1&&s.rx.malformed==before.malformed+1);
 assert(s.rx.last_complete==((UINT64_C(10)<<32)|(UINT64_C(4)<<16)|15));
 assert(s.rx.last_arm==s.rx.last_complete&&s.rx.last_arm_owner==(uint32_t)(uintptr_t)&owner);
 r2_radio_capture_arm(&owner,pdu,17,4,1,false);r2_radio_capture_abort_site(&owner,R2_CAPTURE_ABORT_RX_DONE);
 r2_radio_capture_stats(&s);assert(s.rx.aborted==before.aborted+2&&((s.rx.last_abort>>24)&255)==4);
 for(unsigned i=0;i<17;i++){r2_radio_capture_arm(&owner,pdu,20+i,4,1,false);r2_radio_capture_complete(&owner,true,true);}
 r2_radio_capture_stats(&s);assert(s.rx.overflow==before.overflow+1&&s.rx.completed==before.completed+17);
 assert(r2_radio_capture_flush()==16);
 rx_stats.aborted=UINT32_MAX;r2_radio_capture_arm(&owner,pdu,40,4,1,false);r2_radio_capture_abort(&owner);
 r2_radio_capture_stats(&s);assert(s.rx.aborted==UINT32_MAX&&!s.armed);
 assert(!r2_radio_capture_disable());
 struct r2_rx_loss_capsule capsule;
 assert(r2_radio_capture_loss(&capsule)&&capsule.count&&capsule.frozen&&sizeof capsule<=160);
 uint32_t generation=capsule.generation;
 assert(!r2_radio_capture_enable(99,6));assert(!r2_radio_capture_loss(&capsule));
 r2_radio_capture_schedule(&owner,100,2,0,false);
 assert(r2_radio_capture_loss(&capsule)&&capsule.lazy==1&&capsule.outcome[0]==64&&capsule.generation==generation+1);
 r2_radio_capture_schedule(&other,101,0,0,true);assert(r2_radio_capture_loss(&capsule)&&capsule.queued==1&&capsule.count==1);
 assert(!r2_radio_capture_disable());
 puts("PASS actual radio ISR capture: exact PDU/CC, CRC/address, bounded overflow, RX-vs-TX outcomes/causal abort sites/priority classification/saturation, failed-disable ownership");
}
