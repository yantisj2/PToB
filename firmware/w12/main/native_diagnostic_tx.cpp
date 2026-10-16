#include "native_diagnostic_tx.hpp"
#include "client_lifecycle.hpp"
#include "diagnostic_codec.hpp"
#include "native_transport.hpp"
#include "native_direct.hpp"
#include "periodic_tx.hpp"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <cstring>
namespace v12 {
namespace {
portMUX_TYPE tx_mux=portMUX_INITIALIZER_UNLOCKED;
DiagnosticTxStatus owned,snapshot;bool pending=false,stop_pending=false;
uint16_t next_bytes=0;OwnerLease lease;bool periodic_stop_sent=false,discovery_stop_sent=false;
uint8_t factory[6]{};uint64_t boot_session=0;uint32_t run_id=0;
uint64_t micros(){const int64_t now=esp_timer_get_time();return now>0?uint64_t(now):0;}
void publish(){portENTER_CRITICAL(&tx_mux);
  const auto probe=periodic_tx::status();
  const bool probe_owner=!periodic_tx::estimate_owner_active()&&(probe.installed||probe.armed);
  owned.off_acknowledged=!pending&&!stop_pending&&!owned.owner_busy&&!owned.periodic&&!owned.discovery&&!owned.requested&&!lease.exhausted&&!probe_owner;
  if(owned.off_acknowledged&&(lease.busy||lease.stop_completed!=lease.stop_generation))lease.stopped(micros());
  owned.lease=lease;owned.until_us=lease.deadline_us;owned.run=run_id;snapshot=owned;portEXIT_CRITICAL(&tx_mux);}
void error(){if(owned.api_errors!=UINT32_MAX)++owned.api_errors;owned.fault=true;owned.requested=false;}
void stop(){
  owned.requested=false;
  if(owned.periodic&&!periodic_stop_sent){periodic_stop_sent=true;const int rc=ble_gap_periodic_adv_stop(0);
    if(!rc||rc==BLE_HS_EALREADY)owned.periodic=false;else error();}
  if(owned.discovery&&!discovery_stop_sent){discovery_stop_sent=true;const int rc=ble_gap_ext_adv_stop(0);
    if(!rc||rc==BLE_HS_EALREADY)owned.discovery=false;else error();}
  if(!owned.periodic&&!owned.discovery&&!periodic_tx::estimate_owner_active()&&!periodic_tx::stop())error();
}
bool api(int rc){if(!rc)return true;error();stop();return false;}
int gap(ble_gap_event* e,void*){
  if(e->type==BLE_GAP_EVENT_ADV_COMPLETE&&e->adv_complete.instance==0){owned.discovery=false;
    if(owned.completed!=UINT32_MAX)++owned.completed;
    owned.completion_reason_available=true;owned.completion_reason=e->adv_complete.reason;
    owned.reported_completed_events=e->adv_complete.num_ext_adv_events;
    owned.completed_events_available=false; // Our max_events argument is zero.
    stop();publish();}return 0;
}
bool start(uint16_t bytes,uint64_t deadline){
  periodic_stop_sent=discovery_stop_sent=false;owned.bytes=bytes;
  owned.selected_tx_power=127;owned.selected_power_available=false;owned.public_address_available=false;
  std::memset(owned.public_address_le,0,sizeof(owned.public_address_le));
  owned.completion_reason_available=false;owned.completion_reason=0;owned.reported_completed_events=0;owned.completed_events_available=false;
  uint8_t discovery[GrantBytes],payload[MaxData];
  if(run_id==UINT32_MAX){error();return false;}++run_id;
  const uint8_t wire=!std::strcmp(V12_UNIT,"C3")?5:2;
  if(!diagnostic_pack(discovery,sizeof(discovery),0xf1,wire,factory,boot_session,V12_BUILD_ID,V12_SOURCE_REVISION,run_id)||
     !diagnostic_pack(payload,bytes,0xf0,wire,factory,boot_session,V12_BUILD_ID,V12_SOURCE_REVISION,run_id)){error();return false;}
  ble_gap_ext_adv_params ad{};ad.own_addr_type=BLE_OWN_ADDR_PUBLIC;ad.itvl_min=ad.itvl_max=50;
  ad.primary_phy=ad.secondary_phy=BLE_HCI_LE_PHY_1M;ad.sid=0;ad.tx_power=0;
  ble_gap_periodic_adv_params pa{};
#ifdef V12_R2_TX_SHADOW
  // Match the RC7 16Hz PA interval in this explicitly non-timing probe.
  pa.itvl_min=pa.itvl_max=50;
#else
  pa.itvl_min=pa.itvl_max=25;
#endif
  int8_t selected_power=127;
  if(!api(ble_gap_ext_adv_configure(0,&ad,&selected_power,gap,nullptr)))return false;
  owned.selected_tx_power=selected_power;owned.selected_power_available=selected_power!=127;
  int nrpa=1;
  if(!api(ble_hs_id_copy_addr(BLE_ADDR_PUBLIC,owned.public_address_le,&nrpa)))return false;
  if(nrpa){error();return false;}owned.public_address_available=true;
  os_mbuf* discover=ble_hs_mbuf_from_flat(discovery,sizeof(discovery));if(!discover){error();return false;}
  if(!api(ble_gap_ext_adv_set_data(0,discover))||!api(ble_gap_periodic_adv_configure(0,&pa)))return false;
  os_mbuf* precision=ble_hs_mbuf_from_flat(payload,bytes);if(!precision){error();return false;}
  if(!api(ble_gap_periodic_adv_set_data(0,precision)))return false;
  if(!periodic_tx::start(payload,bytes)){error();stop();return false;}
  if(micros()>=deadline){portENTER_CRITICAL(&tx_mux);lease.deadline_exceeded=true;portEXIT_CRITICAL(&tx_mux);error();return false;}
  if(!api(ble_gap_periodic_adv_start(0)))return false;
  owned.periodic=true;
  if(micros()>=deadline){portENTER_CRITICAL(&tx_mux);lease.deadline_exceeded=true;portEXIT_CRITICAL(&tx_mux);error();stop();return false;}
  const uint64_t now=micros();
  if(now>=deadline){portENTER_CRITICAL(&tx_mux);lease.deadline_exceeded=true;portEXIT_CRITICAL(&tx_mux);error();stop();return false;}
  const uint64_t remaining=deadline-now;
  if(remaining<10000){error();stop();return false;}
  // Controller-owned extended duration adds a secondary finite guard. Periodic
  // advertising has no public duration API and is stopped by the host lease.
  if(!api(ble_gap_ext_adv_start(0,int(remaining/10000),0)))return false;
  owned.discovery=owned.requested=true;
  if(micros()>=deadline){portENTER_CRITICAL(&tx_mux);lease.deadline_exceeded=true;portEXIT_CRITICAL(&tx_mux);error();stop();return false;}
  return true;
}
}
void diagnostic_tx_identity(const uint8_t mac[6],uint64_t session){std::memcpy(factory,mac,6);boot_session=session;}
void diagnostic_tx_init(){publish();}
void diagnostic_tx_poll(){
  bool request,off,timely=false;uint16_t bytes;uint64_t deadline;
  portENTER_CRITICAL(&tx_mux);request=pending;off=stop_pending;bytes=next_bytes;deadline=lease.deadline_us;
  pending=stop_pending=false;owned.owner_busy=true;
  if(request&&!off)timely=lease.start(micros());
  portEXIT_CRITICAL(&tx_mux);publish();
  if(off||(owned.requested&&micros()>=deadline)){
    if(!off){portENTER_CRITICAL(&tx_mux);lease.stop_request();lease.deadline_exceeded=true;portEXIT_CRITICAL(&tx_mux);}
    stop();
  }
  if(request&&!off&&!owned.fault&&!owned.periodic&&!owned.discovery){
    const auto rx=transport_status();const auto link=direct_status();
    if(!timely||rx.command_pending||rx.lease.pending||rx.radio_requested||rx.client.scan||rx.client.pending||rx.client.synced||link.stage!=DirectStage::Off)error();
    else start(bytes,deadline);
  }
  // Bounded selected-PDU serialization. Controller completion/event metadata
  // and callback brackets are diagnostic observations, never RC7 timestamps.
#ifdef V12_R2_TX_SHADOW
  if(!periodic_tx::estimate_owner_active()){
  periodic_tx::Record observed{};
  for(unsigned i=0;i<periodic_tx::Capacity&&periodic_tx::pop(observed);i++){
    // The worker repeats the frozen-byte check; a refusal never emits bytes
    // from unknown controller memory. Metadata is kept to diagnose the refusal.
    const bool verified=periodic_tx::payload_verified(observed);
    constexpr unsigned Raw=222; //32-byte metadata plus190-byte selected data.
    for(unsigned fragment=0;fragment<2;fragment++){
      Report r{};r.kind=19;r.callback_us=micros();r.actual_length=r.length=190;
      r.bytes[0]=2;r.bytes[1]=uint8_t(fragment);r.bytes[2]=observed.activity;
      r.bytes[3]=observed.status;r.bytes[4]=observed.controller_activity;
      r.bytes[5]=observed.selected.reason;
      const uint64_t values[]={observed.sequence,observed.epoch,observed.before20,observed.after20,observed.kind};
      const unsigned offsets[]={6,10,14,22,30},widths[]={4,4,8,8,4};
      for(unsigned j=0;j<5;j++)for(unsigned k=0;k<widths[j];k++)r.bytes[offsets[j]+k]=uint8_t(values[j]>>(8*k));
      for(unsigned j=0;j<156;j++){
        const unsigned at=fragment*156+j;if(at>=Raw)break;
        r.bytes[34+j]=selected_tx::wire_byte(observed.selected,at,verified);
      }
      transport_record(r);
    }
  }
  }
#endif
  if(!periodic_tx::estimate_owner_active()&&periodic_tx::status().fault&&(owned.periodic||owned.discovery)){error();stop();}
  portENTER_CRITICAL(&tx_mux);owned.owner_busy=false;portEXIT_CRITICAL(&tx_mux);publish();
}
void diagnostic_tx_reset(){if(lease.generation||pending||owned.requested||owned.periodic||owned.discovery)owned.fault=true;owned.requested=false;owned.periodic=owned.discovery=false;publish();}
bool diagnostic_tx_request(uint16_t bytes,uint32_t duration,uint64_t request_id){
  bool size=bytes==RootBytes||bytes==RelayBytes;
#ifdef V12_R2_TX_SHADOW
  size=bytes==190; // Narrow known-PDU probe only, never RC7 timing.
#endif
  if(!size||duration<1000||duration>10000)return false;
  const auto rx=transport_status();const auto link=direct_status();
  if(!rx.host_ready||!rx.off_acknowledged||rx.command_pending||rx.lease.pending||rx.client.state!=Lifecycle::Off||link.stage!=DirectStage::Off)return false;
  portENTER_CRITICAL(&tx_mux);const bool ok=!pending&&!stop_pending&&!snapshot.owner_busy&&snapshot.off_acknowledged&&!snapshot.fault&&!snapshot.periodic&&!snapshot.discovery&&lease.admit(micros(),duration,request_id);
  if(ok){pending=true;next_bytes=bytes;snapshot.lease=lease;snapshot.off_acknowledged=false;}portEXIT_CRITICAL(&tx_mux);return ok;
}
void diagnostic_tx_stop(){portENTER_CRITICAL(&tx_mux);stop_pending=true;pending=false;lease.stop_request();snapshot.lease=lease;snapshot.off_acknowledged=false;portEXIT_CRITICAL(&tx_mux);}
void diagnostic_tx_observe_lease(uint64_t now){
  portENTER_CRITICAL(&tx_mux);if(lease.observe(now)){stop_pending=true;pending=false;snapshot.off_acknowledged=false;}
  snapshot.lease=lease;portEXIT_CRITICAL(&tx_mux);
}
DiagnosticTxStatus diagnostic_tx_status(){diagnostic_tx_observe_lease(micros());portENTER_CRITICAL(&tx_mux);const auto status=snapshot;portEXIT_CRITICAL(&tx_mux);return status;}
}
