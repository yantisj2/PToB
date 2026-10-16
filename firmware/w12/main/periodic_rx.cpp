// Experimental read-only periodic port of reference/rx_capture.cpp. The ABI
// contract binds every dispatch slot and descriptor access to the pinned S3
// archive. Original handlers run exactly once; no radio scheduling/register,
// descriptor, payload, allocator or controller environment is modified.
#include "periodic_rx.hpp"
#include "../legacy/reference/clock_ble_mapping.hpp"
#include "native_transport.hpp"
#include <cstdlib>
#ifdef V12_R2_RX_SHADOW
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_memory_utils.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if !CONFIG_IDF_TARGET_ESP32S3 || CONFIG_BT_CTRL_PINNED_TO_CORE != 0 || CONFIG_BT_NIMBLE_PINNED_TO_CORE != 0 || CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 240 || CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE < 8192
#error Periodic RX shadow requires pinned S3 CPU0 240MHz and measured8192B host stack
#endif
extern "C" {
extern void** r_ip_funcs_p;extern void** r_plf_funcs_p;extern void* p_lld_env;
extern void* volatile lld_sync_env[];
extern uint16_t lld_exp_sync_pos_tab[4];
void r_lld_sync_frm_rx_isr(uint8_t);void r_lld_sync_process_pkt_rx_aux_sync_ind(uint8_t,uint8_t);
void r_lld_rxdesc_free();void* r_emi_get_mem_addr_by_offset(uint16_t);adv_rx::BleTime r_rwip_time_get();
bool clock_reference_read(uint64_t*);
bool clock_reference_bt_bracket(uint64_t*,uint64_t*,adv_rx::BleTime*);
}
namespace v12::periodic_rx {
extern "C" void w12_periodic_rx_hook(uint8_t);
extern "C" void w12_periodic_aux_hook(uint8_t,uint8_t);
extern "C" void w12_periodic_free_hook();
namespace {
constexpr unsigned RxSlot=0x150/4,AuxSlot=0x160/4,FreeSlot=0x2a8/4;
DRAM_ATTR Record records[Capacity]{};DRAM_ATTR Record pending{};
DRAM_ATTR adv_rx::BleAnchor anchor{};
DRAM_ATTR adv_rx::BleMapping clock_mapping{};
DRAM_ATTR int8_t refinement_ticks=0;DRAM_ATTR bool refinement_ready=false;
DRAM_ATTR volatile uint32_t head=0,depth=0,packet_count=0,pending_count=0,epoch=0;
DRAM_ATTR volatile bool armed=false,installed=false;
DRAM_ATTR volatile uint32_t fault=0,calls=0,candidates=0,overflows=0,hook_max=0;
DRAM_ATTR volatile uint32_t aux_calls=0,aux_in_rx=0,aux_armed=0,reason=0,last_first=0,last_index=255,last_control=0,last_status=0,last_header=0,last_offset=0;
uint32_t used[Capacity]{},matched=0,missed=0,ambiguities=0,peak=0;
RfReference last_reference{};
bool acquiring=false;uint16_t handle=UINT16_MAX;uint64_t anchor_us=0,audit_us=0;esp_pm_lock_handle_t cpu_lock=nullptr;
inline __attribute__((always_inline)) void barrier(){asm volatile("memw" ::: "memory");}
inline __attribute__((always_inline)) uint32_t cycles(){uint32_t c;asm volatile("rsr.ccount %0":"=a"(c));return c;}
inline __attribute__((always_inline)) bool range(const void* p,unsigned n){const uintptr_t a=reinterpret_cast<uintptr_t>(p);return n<=0x78000&&a>=0x3fc88000&&a<=0x3fd00000-n;}
bool pointers(){return r_ip_funcs_p&&r_ip_funcs_p[RxSlot]==reinterpret_cast<void*>(w12_periodic_rx_hook)&&r_ip_funcs_p[AuxSlot]==reinterpret_cast<void*>(w12_periodic_aux_hook)&&r_ip_funcs_p[FreeSlot]==reinterpret_cast<void*>(w12_periodic_free_hook);}
inline __attribute__((always_inline)) void increment(volatile uint32_t& value){if(value!=UINT32_MAX)value=value+1;}
inline __attribute__((always_inline)) void copy_record(Record& to,const Record& from){
  to.anchor.before=from.anchor.before;to.anchor.after=from.anchor.after;
  to.anchor.time.half_slots=from.anchor.time.half_slots;to.anchor.time.half_us=from.anchor.time.half_us;
  to.stamp.epoch=from.stamp.epoch;to.stamp.sequence=from.stamp.sequence;
  to.stamp.coarse=from.stamp.coarse;to.stamp.fine=from.stamp.fine;
  to.stamp.raw_metadata=from.stamp.raw_metadata;to.stamp.activity=from.stamp.activity;
  to.stamp.event=from.stamp.event;to.stamp.channel=from.stamp.channel;
  to.stamp.association_observed=from.stamp.association_observed;
  to.stamp.actual=from.stamp.actual;to.observed_us=from.observed_us;
  to.stamp.refinement_ticks=from.stamp.refinement_ticks;to.stamp.refinement_applied=from.stamp.refinement_applied;
  to.stamp.phy=from.stamp.phy;to.stamp.radio_extra_us=from.stamp.radio_extra_us;
  to.stamp.radio_reference_valid=from.stamp.radio_reference_valid;to.stamp.radio_reference_applied=false;
  to.stable=from.stable;to.single=from.single;
  to.length=from.length;
  for(unsigned j=0;j<Bytes;j++)to.bytes[j]=from.bytes[j];
}
}
extern "C" void IRAM_ATTR w12_periodic_free_hook(){
  if(depth)increment(packet_count);
  r_lld_rxdesc_free();
}
extern "C" void IRAM_ATTR w12_periodic_aux_hook(uint8_t activity,uint8_t first){
  increment(aux_calls);if(depth==1)increment(aux_in_rx);if(armed)increment(aux_armed);
  last_first=first;reason=!armed?1:depth!=1?2:first!=1?3:4;
  if(armed&&!fault&&depth==1&&first==1&&range(p_lld_env,217)){
    const uint8_t index=static_cast<volatile uint8_t*>(p_lld_env)[216];
    last_index=index;reason=5;
    if(index<10){auto* base=static_cast<volatile uint16_t*>(r_emi_get_mem_addr_by_offset(0x1000));
      reason=6;
      if(range(const_cast<const uint16_t*>(base),200)){
        auto* d=base+10*index;const uint16_t control=d[0],status=d[1],header=d[2],ext_header=d[8],offset=d[9];
        const unsigned size=header>>8;
        last_control=control;last_status=status;last_header=header;last_offset=offset;
        reason=!(control&0x8000)?7:(status&0x603d)?8:(header&15)!=7?9:!size||size>MaxPdu?10:
          offset<0x1400||offset>=0xcc00||(offset&1023)+size>1024?11:12;
        if((control&0x8000)&&!(status&0x603d)&&(header&15)==7&&size<=MaxPdu&&size&&
           offset>=0x1400&&offset<0xcc00&&(offset&1023)+size<=1024){
          auto* p=static_cast<volatile uint8_t*>(r_emi_get_mem_addr_by_offset(offset));unsigned ad=0;
          if(range(const_cast<const uint8_t*>(p),size-1)&&descriptor_payload(uint8_t(ext_header),p,size,ad)){
            reason=13;
            increment(candidates);increment(pending_count);
            if(pending_count==1){pending.anchor.before=anchor.before;pending.anchor.after=anchor.after;
              pending.anchor.time.half_slots=anchor.time.half_slots;pending.anchor.time.half_us=anchor.time.half_us;
              pending.stamp.epoch=epoch;pending.stamp.activity=activity;pending.stamp.raw_metadata=d[3];
              pending.stamp.coarse=uint32_t(d[4])|(uint32_t(d[5])<<16);pending.stamp.fine=d[6];
              pending.stamp.actual=true;pending.observed_us=anchor_us;
              pending.stamp.refinement_ticks=refinement_ticks;pending.stamp.refinement_applied=refinement_ready;
              pending.stamp.event=0;pending.stamp.channel=255;pending.stamp.association_observed=false;
              pending.stamp.phy=255;pending.stamp.radio_reference_valid=false;pending.stamp.radio_extra_us=0;
              // The pinned packet checker copies this same activity's+74
              // counter into its received-PDU report. Retain a stable bounded
              // candidate; do not assert event/channel/edge validity here.
              volatile uint16_t* event=nullptr;void* environment=nullptr;
              if(activity<CONFIG_BT_CTRL_BLE_MAX_ACT_EFF&&range(const_cast<void**>(lld_sync_env),4*(activity+1))){
                environment=lld_sync_env[activity];
                if(range(environment,89)){
                  event=reinterpret_cast<volatile uint16_t*>(static_cast<uint8_t*>(environment)+74);
                  pending.stamp.event=*event;
                  const auto phy=static_cast<volatile uint8_t*>(environment)[88];
                  if(phy<4)pending.stamp.phy=phy;
                  if(phy==0&&reinterpret_cast<uintptr_t>(lld_exp_sync_pos_tab)==0x3fcefc80&&range(lld_exp_sync_pos_tab,8)){
                    const auto* table=static_cast<const volatile uint16_t*>(lld_exp_sync_pos_tab);
                    const auto position=table[0];barrier();
                    if(position>=40&&position<=167&&position==table[0]){
                      pending.stamp.radio_extra_us=uint8_t(position-40);pending.stamp.radio_reference_valid=true;
                    }
                  }
                }
              }
              const unsigned channel=pending.stamp.raw_metadata>>8;
              if(channel<=36)pending.stamp.channel=uint8_t(channel);
              pending.length=uint8_t(size-1-ad);
              for(unsigned j=0;j<Bytes;j++)pending.bytes[j]=j<pending.length?p[ad+j]:0;
              barrier();bool stable=control==d[0]&&status==d[1]&&header==d[2]&&ext_header==d[8]&&offset==d[9]&&pending.stamp.raw_metadata==d[3]&&
                pending.stamp.coarse==(uint32_t(d[4])|(uint32_t(d[5])<<16))&&pending.stamp.fine==d[6];
              for(unsigned j=0;j<pending.length;j++)stable=stable&&pending.bytes[j]==p[ad+j];
              if(pending.stamp.radio_reference_valid&&static_cast<const volatile uint16_t*>(lld_exp_sync_pos_tab)[0]!=unsigned(pending.stamp.radio_extra_us)+40)
                pending.stamp.radio_reference_valid=false;
              pending.stamp.association_observed=event&&channel<=36&&lld_sync_env[activity]==environment&&*event==pending.stamp.event&&
                (pending.stamp.phy==255||static_cast<volatile uint8_t*>(environment)[88]==pending.stamp.phy);
              pending.stable=stable;
            }
          }
        }
      }
    }
  }
  r_lld_sync_process_pkt_rx_aux_sync_ind(activity,first);
}
extern "C" void IRAM_ATTR w12_periodic_rx_hook(uint8_t activity){
  const uint32_t begin=cycles();increment(calls);
  if(depth){fault=3;r_lld_sync_frm_rx_isr(activity);return;}
  depth=1;packet_count=0;pending_count=0;r_lld_sync_frm_rx_isr(activity);depth=0;
  if(pending_count&&armed&&!fault){
    if(head==UINT32_MAX){fault=4;armed=false;}
    else {auto& row=records[head%Capacity];
      // Retain duplicate identities until their association window expires.
      if(head>=Capacity&&anchor_us>=row.observed_us&&anchor_us-row.observed_us<=MaxAgeUs){increment(overflows);fault=4;armed=false;}
      else {pending.stamp.sequence=head+1;pending.single=packet_count==1&&pending_count==1;
        copy_record(row,pending);barrier();head=head+1;}
    }
  }
  const uint32_t elapsed=cycles()-begin;if(elapsed>hook_max)hook_max=elapsed;
}
void poll(){
  if(!installed||xPortGetCoreID()!=0)return;
  const uint64_t now=uint64_t(esp_timer_get_time());
  if(now<anchor_us||now-anchor_us<25000)return;
  armed=false;barrier();
  if(fault)return;
  if(!pointers()||esp_clk_cpu_freq()!=240000000){fault=1;return;}
  adv_rx::BleAnchor next{};
  for(unsigned i=0;i<4;i++){adv_rx::BleAnchor a{};
    if(!clock_reference_bt_bracket(&a.before,&a.after,&a.time)||a.after<a.before)continue;
    if(!next.before||a.after-a.before<next.after-next.before)next=a;}
  if(!next.before||next.after-next.before>400||next.time.half_slots>adv_rx::BleClockMask||next.time.half_us>624)return;
  if(anchor.before&&now-anchor_us<=MaxAgeUs){uint64_t predicted=0;uint32_t width=0;
    if(!adv_rx::descriptor_map(anchor,next.time.half_slots,uint16_t(624-next.time.half_us),predicted,width)||
       std::llabs(int64_t(next.before+(next.after-next.before)/2)-int64_t(predicted))>
         int64_t((anchor.after-anchor.before+next.after-next.before)/2+20)){fault=5;return;}}
  int32_t correction=0;
  refinement_ready=clock_mapping.observe(next,correction);
  refinement_ticks=int8_t(correction);
  // One bounded worker-only audit per second, no extra RF. Neither bracket
  // supplies a TX timestamp or qualified RF calibration.
  if(!audit_us||now-audit_us>=1000000){
    adv_rx::BleAnchor probe{};uint64_t fast_before=0,fast_after=0;
    if(clock_reference_read(&probe.before)&&clock_reference_bt_bracket(&fast_before,&fast_after,&probe.time)&&clock_reference_read(&probe.after)){
      Report r{};r.kind=18;r.callback_us=now;r.actual_length=r.length=41;r.bytes[0]=1;
      const uint64_t values[]={probe.before,fast_before,fast_after,probe.after,probe.time.half_slots,probe.time.half_us};
      const unsigned widths[]={8,8,8,8,4,4};unsigned offset=1;
      for(unsigned j=0;j<6;j++){for(unsigned k=0;k<widths[j];k++)r.bytes[offset+k]=uint8_t(values[j]>>(8*k));offset+=widths[j];}
      transport_record(r);
    }
    audit_us=now;
  }
  anchor=next;anchor_us=now;barrier();armed=handle!=UINT16_MAX||acquiring;
}
bool start(){
  if(xPortGetCoreID()!=0||esp_clk_cpu_freq()!=240000000)return false;
  if(installed)return !fault;
  if(fault||!r_ip_funcs_p||!esp_ptr_internal(r_ip_funcs_p)||!r_plf_funcs_p||
     reinterpret_cast<uintptr_t>(lld_sync_env)!=0x3fcefc30||
     r_ip_funcs_p[RxSlot]!=reinterpret_cast<void*>(r_lld_sync_frm_rx_isr)||
     r_ip_funcs_p[AuxSlot]!=reinterpret_cast<void*>(r_lld_sync_process_pkt_rx_aux_sync_ind)||
     r_ip_funcs_p[FreeSlot]!=reinterpret_cast<void*>(r_lld_rxdesc_free)||
     r_plf_funcs_p[0xbc/4]!=reinterpret_cast<void*>(r_emi_get_mem_addr_by_offset)){fault=1;return false;}
  if(!cpu_lock&&esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"r2_rx",&cpu_lock)!=ESP_OK){fault=2;return false;}
  if(esp_pm_lock_acquire(cpu_lock)!=ESP_OK){fault=2;return false;}
  r_ip_funcs_p[RxSlot]=reinterpret_cast<void*>(w12_periodic_rx_hook);
  r_ip_funcs_p[AuxSlot]=reinterpret_cast<void*>(w12_periodic_aux_hook);
  r_ip_funcs_p[FreeSlot]=reinterpret_cast<void*>(w12_periodic_free_hook);
  barrier();installed=true;poll();return !fault;
}
uint32_t scope(uint16_t next,bool pending_acquire){
  armed=false;barrier();handle=next;acquiring=pending_acquire&&next==UINT16_MAX;head=0;for(auto& n:used)n=0;
  last_reference={};
  clock_mapping.reset();refinement_ready=false;refinement_ticks=0;
  if(epoch==UINT32_MAX){fault=4;return 0;}epoch=epoch+1;
  if(installed&&!fault){anchor_us=0;poll();}
  return epoch;
}
bool adopt(uint16_t next,uint32_t expected){
  // Only the host owner's actual matching SYNC success binds tentative rows.
  // Keep the same acquisition epoch; the establishing PDU predates this callback.
  if(!installed||fault||!acquiring||handle!=UINT16_MAX||next==UINT16_MAX||expected!=epoch)return false;
  handle=next;acquiring=false;barrier();return true;
}
bool stop(){
  // Called only by the host owner after scan, sync, ACL and advertising drain.
  if(!installed)return true;
  if(xPortGetCoreID()!=0||depth||!pointers())return false;
  armed=false;barrier();r_ip_funcs_p[RxSlot]=reinterpret_cast<void*>(r_lld_sync_frm_rx_isr);
  r_ip_funcs_p[AuxSlot]=reinterpret_cast<void*>(r_lld_sync_process_pkt_rx_aux_sync_ind);
  r_ip_funcs_p[FreeSlot]=reinterpret_cast<void*>(r_lld_rxdesc_free);barrier();installed=false;
  scope(UINT16_MAX);return esp_pm_lock_release(cpu_lock)==ESP_OK;
}
bool match(uint16_t selected,const uint8_t* bytes,size_t size,uint64_t callback20,Stamp& result){
  result={};if(!installed||fault||xPortGetCoreID()!=0){++missed;return false;}
  const uint32_t before=head;barrier();const unsigned count=before<Capacity?before:Capacity;
  uint32_t sequence=0;bool ambiguous=false;
  const bool ok=select(records,count,selected,handle,epoch,bytes,size,uint64_t(esp_timer_get_time()),callback20,result,sequence,ambiguous);
  barrier();if(ambiguous)++ambiguities;
  if(!ok||head!=before||fault||used[(sequence-1)%Capacity]==sequence){++missed;result={};return false;}
  used[(sequence-1)%Capacity]=sequence;++matched;if(count>peak)peak=count;
  last_reference={result.radio_reference_applied,result.phy,result.radio_extra_us,result.epoch};return true;
}
RfReference rf_reference(){
  if(!installed||fault||xPortGetCoreID()!=0||last_reference.epoch!=epoch)return {};
  return last_reference;
}
Stats status(){return {calls,candidates,matched,missed,ambiguities,overflows,fault,epoch,peak,hook_max,installed,armed,
  aux_calls,aux_in_rx,aux_armed,reason,last_first,last_index,last_control,last_status,last_header,last_offset};}
}
#endif
