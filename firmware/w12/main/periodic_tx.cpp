// Read-only, pinned periodic TX descriptor probe. Original SDK handler runs
// exactly once with unchanged arguments, including its deferred-data workaround.
// No controller/EM/descriptor/timer/scheduler writes, crypto, formatting,
// allocation or flash writes occur in the hook. The dispatch entry is the
// sole installation change, restored only after the actual TX owner drains.
#include "periodic_tx.hpp"
#if defined(V12_R2_TX_SHADOW) || defined(V12_R2_RC7_TX_ESTIMATE_LAB)
#include "periodic_tx_rom_guard.hpp"
#include "periodic_tx_packed_map_guard.hpp"
#include "periodic_tx_chm_guard.hpp"
#include "../legacy/reference/clock_s3_rx_descriptor.hpp"
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_memory_utils.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if !CONFIG_IDF_TARGET_ESP32S3 || CONFIG_BT_CTRL_PINNED_TO_CORE != 0 || CONFIG_BT_NIMBLE_PINNED_TO_CORE != 0 || CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 240 || CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE < 8192 || !CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM
#error Periodic TX probe requires pinned S3 CPU0 240MHz and reviewed8192B host stack
#endif
#if CONFIG_BT_CTRL_BLE_MAX_ACT_EFF != 6 && CONFIG_BT_CTRL_BLE_MAX_ACT_EFF != 10
#error Periodic TX collection supports only reviewed controller activity budgets6 or10
#endif
extern "C" {
extern void** r_ip_funcs_p;extern void** r_plf_funcs_p;
extern void* volatile lld_per_adv_env[];
void r_lld_per_adv_frm_isr_hack(uint8_t,uint32_t,uint8_t);
void* r_emi_get_mem_addr_by_offset(uint16_t);
bool clock_reference_read(uint64_t*);
bool clock_reference_bt_bracket(uint64_t*,uint64_t*,adv_rx::BleTime*);
}
namespace v12::periodic_tx {
extern "C" void w12_periodic_tx_hook(uint8_t,uint32_t,uint8_t);
namespace {
constexpr unsigned IsrSlot=0x2f0/4, EmiSlot=0xbc/4;
DRAM_ATTR Record rows[Capacity]{};
DRAM_ATTR volatile uint32_t head=0,tail=0,calls=0,recorded=0,dropped=0,fault=0,epoch=0,max_cycles=0;
DRAM_ATTR volatile bool installed=false,armed=false,in_hook=false;
DRAM_ATTR uint8_t expected_bytes[selected_tx::DataBytes]{};
DRAM_ATTR bool expected_valid=false,rom_verified=false;
DRAM_ATTR bool estimate_mode=false;
DRAM_ATTR tx_estimate::BankPool expected_banks{};
DRAM_ATTR selected_tx::Radio radio_rows[Capacity]{};
DRAM_ATTR selected_tx::EventFence event_fence{};
DRAM_ATTR volatile uint32_t reasons[selected_tx::ReasonCount]{};
esp_pm_lock_handle_t cpu_lock=nullptr;
inline __attribute__((always_inline)) void barrier(){asm volatile("memw" ::: "memory");}
inline __attribute__((always_inline)) uint32_t cycles(){uint32_t c;asm volatile("rsr.ccount %0":"=a"(c));return c;}
inline __attribute__((always_inline)) void inc(volatile uint32_t& n){if(n!=UINT32_MAX)n=n+1;}
inline __attribute__((always_inline)) bool range(const void* p,unsigned n){
  const auto a=reinterpret_cast<uintptr_t>(p);
  return n<=0x78000&&a>=0x3fc88000&&a<=0x3fd00000-n;
}
struct Access {
  inline __attribute__((always_inline)) bool environment_address(uint8_t activity,uintptr_t& address){
    if(activity>=CONFIG_BT_CTRL_BLE_MAX_ACT_EFF||
       !range(const_cast<void**>(lld_per_adv_env),4*(activity+1)))return false;
    address=reinterpret_cast<uintptr_t>(lld_per_adv_env[activity]);
    return range(reinterpret_cast<const void*>(address),ContextBytes);
  }
  inline __attribute__((always_inline)) bool em_address(uint16_t offset,uintptr_t& address){
    address=reinterpret_cast<uintptr_t>(r_emi_get_mem_addr_by_offset(offset));return address!=0;
  }
  inline __attribute__((always_inline)) uint8_t read8(uintptr_t a){return *reinterpret_cast<volatile const uint8_t*>(a);}
  inline __attribute__((always_inline)) uint16_t read16(uintptr_t a){return *reinterpret_cast<volatile const uint16_t*>(a);}
  inline __attribute__((always_inline)) uint32_t read32(uintptr_t a){return *reinterpret_cast<volatile const uint32_t*>(a);}
};
// Finite RF-off worker only. Executable ROM permits aligned word reads, not
// byte/halfword reads or memcpy. No register/protection/cache writes occur.
bool rom_load32(uintptr_t address,uint32_t& value){
  if((address&3)||address<0x40000000||address>0x4005fffc)return false;
  value=*reinterpret_cast<volatile const uint32_t*>(address);return true;
}
bool pointers(){return r_ip_funcs_p&&r_plf_funcs_p&&
  r_ip_funcs_p[IsrSlot]==reinterpret_cast<void*>(w12_periodic_tx_hook)&&
  r_plf_funcs_p[EmiSlot]==reinterpret_cast<void*>(r_emi_get_mem_addr_by_offset);}
}
extern "C" void IRAM_ATTR w12_periodic_tx_hook(uint8_t activity,uint32_t kind,uint8_t status){
  const uint32_t begin=cycles();inc(calls);
  // Never prevent the controller's handler, even on an observation fault.
  if(in_hook){fault=3;r_lld_per_adv_frm_isr_hack(activity,kind,status);return;}
  in_hook=true;
  const bool observe=armed&&!fault;
  Record* row=nullptr;
  if(observe){
    if(head==UINT32_MAX||head-tail>=Capacity){inc(dropped);fault=4;armed=false;}
    else {
      row=&rows[head%Capacity];row->epoch=epoch;row->sequence=head+1;
      row->activity=activity;row->kind=kind;row->status=status;row->controller_activity=255;row->expected_slot=255;
      uint64_t tick=0;clock_reference_read(&tick);row->before20=tick;row->after20=0;
      Access access;
      const selected_tx::Scope scope{kind,epoch,activity,status,CONFIG_BT_CTRL_BLE_MAX_ACT_EFF,rom_verified,estimate_mode};
      if(estimate_mode){
        selected_tx::KnownExpected known[tx_estimate::Banks];expected_banks.expectations(known);
        uint8_t matched=255;
        if(selected_tx::collect_known(row->selected,scope,known,tx_estimate::Banks,matched,
             radio_rows[head%Capacity],access,event_fence)&&expected_banks.pin(matched)){
          row->expected_slot=matched;row->controller_activity=activity;
        }else if(row->selected.flags)selected_tx::detail::refuse(row->selected,selected_tx::Reason::Expected);
      }else{
        const selected_tx::Expected expected{expected_bytes,selected_tx::DataBytes,expected_valid};
        if(selected_tx::collect(row->selected,scope,expected,access,event_fence))row->controller_activity=activity;
      }
      if(row->selected.reason<selected_tx::ReasonCount)inc(reasons[row->selected.reason]);
    }
  }
  // This is the exact initialized SDK dispatch target, not its inner ROM
  // handler. Calling the inner handler would omit pending PA data updates.
  r_lld_per_adv_frm_isr_hack(activity,kind,status);
  if(row){uint64_t tick=0;clock_reference_read(&tick);row->after20=tick;
    barrier();head=head+1;inc(recorded);}
  in_hook=false;const uint32_t span=cycles()-begin;if(span>max_cycles)max_cycles=span;
}
namespace {
bool start_mode(const uint8_t* expected,unsigned size,bool estimated){
  if(xPortGetCoreID()!=0||esp_clk_cpu_freq()!=240000000)return false;
  if(installed)return false; // Never reuse an installed scope or frozen payload.
  if(fault||!r_ip_funcs_p||!esp_ptr_internal(r_ip_funcs_p)||!r_plf_funcs_p||
     reinterpret_cast<uintptr_t>(lld_per_adv_env)!=0x3fcefc58||
     r_ip_funcs_p[IsrSlot]!=reinterpret_cast<void*>(r_lld_per_adv_frm_isr_hack)||
     r_plf_funcs_p[EmiSlot]!=reinterpret_cast<void*>(r_emi_get_mem_addr_by_offset)||epoch==UINT32_MAX||head!=tail){fault=1;return false;}
  expected_valid=false;rom_verified=false;
  if(estimated?!tx_estimate::validate(expected,size):!selected_tx::validate_expected(expected,size)){fault=6;return false;}
  if(!selected_tx::rom::match(rom_load32)){fault=5;return false;}
  if(estimated&&(!range(r_ip_funcs_p,0x79c)||
      r_ip_funcs_p[0x798/4]!=reinterpret_cast<void*>(uintptr_t(0x400051c0))||
      r_ip_funcs_p[0x2e8/4]!=reinterpret_cast<void*>(uintptr_t(0x4000456c))||
      !selected_tx::rom::match_radio(rom_load32)||!selected_tx::rom::match_packed_map(rom_load32)||
      !selected_tx::rom::match_chm(rom_load32))){fault=5;return false;}
#if CONFIG_BT_CTRL_BLE_MAX_ACT_EFF == 10
  if(!selected_tx::rom::match_capacity10(rom_load32)){fault=5;return false;}
#endif
  rom_verified=true;
  estimate_mode=estimated;expected_banks.reset();
  if(estimated){if(!expected_banks.prepare(expected,size)){fault=6;return false;}}
  else{for(unsigned i=0;i<selected_tx::DataBytes;i++)expected_bytes[i]=expected[i];expected_valid=true;}
  event_fence={};
  if(!cpu_lock&&esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"r2_txprobe",&cpu_lock)!=ESP_OK){fault=2;return false;}
  if(esp_pm_lock_acquire(cpu_lock)!=ESP_OK){fault=2;return false;}
  armed=false;epoch=epoch+1;
  r_ip_funcs_p[IsrSlot]=reinterpret_cast<void*>(w12_periodic_tx_hook);
  barrier();installed=true;armed=true;return true;
}
}
bool start(const uint8_t* expected,unsigned size){return start_mode(expected,size,false);}
bool stop(){
  if(!installed)return true;
  if(xPortGetCoreID()!=0||in_hook||!pointers())return false;
  armed=false;barrier();r_ip_funcs_p[IsrSlot]=reinterpret_cast<void*>(r_lld_per_adv_frm_isr_hack);
  barrier();installed=false;return esp_pm_lock_release(cpu_lock)==ESP_OK;
}
bool pop(Record& r){
  if(xPortGetCoreID()!=0||estimate_mode)return false;
  const auto end=head;barrier();if(tail==end)return false;
  r=rows[tail%Capacity];barrier();tail=tail+1;return true;
}
bool payload_verified(const Record& r){
  return !estimate_mode&&r.epoch==epoch&&expected_valid&&rom_verified&&
    r.controller_activity==r.activity&&r.activity<CONFIG_BT_CTRL_BLE_MAX_ACT_EFF&&
    r.selected.context_cookie==r.kind&&
    selected_tx::payload_verified(r.selected,expected_bytes,selected_tx::DataBytes);
}
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
bool estimate_owner_active(){return installed&&estimate_mode;}
EstimateQueue estimate_queue(){
  if(xPortGetCoreID()!=0||!installed||!estimate_mode)return {};
  const auto prepared=expected_banks.prepared,current=expected_banks.current;
  barrier();EstimateQueue out{};
  if(prepared<tx_estimate::Banks&&expected_banks.valid[prepared])
    out.prepared=tx_estimate::le32(expected_banks.bytes[prepared]+110);
  if(current<tx_estimate::Banks&&expected_banks.valid[current])
    out.current=tx_estimate::le32(expected_banks.bytes[current]+110);
  barrier();
  if(current!=expected_banks.current)return {};
  return out;
}
bool start_estimated(const uint8_t* initial,unsigned size){return start_mode(initial,size,true);}
bool prepare_estimated_payload(const uint8_t* bytes,unsigned size){
  return xPortGetCoreID()==0&&installed&&armed&&!fault&&estimate_mode&&
    expected_banks.prepare(bytes,size);
}
bool pop_estimated(EstimateRecord& out){
  if(xPortGetCoreID()!=0||!estimate_mode)return false;
  const auto end=head;barrier();if(tail==end)return false;
  const unsigned index=tail%Capacity;
  out={};out.observed=rows[index];
  const auto& row=out.observed;
  const bool verified=row.epoch==epoch&&rom_verified&&row.controller_activity==row.activity&&
    row.activity<CONFIG_BT_CTRL_BLE_MAX_ACT_EFF&&row.selected.context_cookie==row.kind&&
    expected_banks.verify(row.expected_slot,row.selected);
  selected_tx::Radio radio=radio_rows[index];
  // Release only after verification/copy, and before publishing tail. Hook can
  // interrupt this worker but cannot reuse this row until tail advances.
  expected_banks.release(row.expected_slot);barrier();tail=tail+1;
  if(!verified){out.stage=1;return true;}
  tx_estimate::ClockAnchor anchor,best;uint64_t best_width=UINT64_MAX;
  for(unsigned i=0;i<4;++i){
    adv_rx::BleTime time;
    if(!clock_reference_bt_bracket(&anchor.before,&anchor.after,&time)||anchor.after<anchor.before)continue;
    anchor.half_slots=time.half_slots;anchor.half_us=time.half_us;
    const auto width=anchor.after-anchor.before;
    if(width<best_width){best=anchor;best_width=width;}
  }
  if(best_width==UINT64_MAX){out.stage=2;return true;}
  out.anchor=best;
  if(!tx_estimate::programmed_tick(best,row.kind,row.before20,out.tick20)){out.stage=3;return true;}
  if(!tx_estimate::channel(radio,row.selected.event,out.channel)){out.stage=4;return true;}
  out.stage=5;
  out.provenance=tx_estimate::Provenance::ProgrammedHalfSlotAndCsa2;out.estimated=true;
  return true;
}
#endif
Stats status(){return {calls,recorded,dropped,fault,epoch,max_cycles,installed,armed};}
}
#endif
