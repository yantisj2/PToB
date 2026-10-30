#include "native_transport.hpp"
#include "capture_contract.hpp"
#include "diagnostic_codec.hpp"
#include "native_direct.hpp"
#include "native_diagnostic_tx.hpp"
#include "fault_watchdog.hpp"
#ifdef V12_R2_ENABLED
#include "native_r2.hpp"
#endif
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <cstring>
#ifdef PTOB_W12_STATIC_NIMBLE_HOST
#include "w12_host_task.hpp"
#include "esp_heap_caps.h"
#include <cstdio>
#endif
namespace v12 {
namespace {
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
#ifdef PTOB_W12_STATIC_NIMBLE_HOST
w12::HostTask<CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE> host_task;
#endif
Ring<Report,16> queue;
TransportStatus snapshot;ClientLifecycle client;OwnerLease lease;
uint32_t reports=0,incomplete=0,oversize=0,late=0,api_errors=0,resets=0;
uint64_t lease_end=0,last_api=0,max_api=0;
uint16_t sync_handle=UINT16_MAX;ble_addr_t sync_peer{};uint8_t sync_sid=0;
bool ready=false,command_pending=false,command_stop=false;
uint32_t host_depth=0,stop_generation=0,stop_processed=0;bool stop_exhausted=false;
bool scan_cancel_sent=false,cancel_sent=false,terminate_sent=false;
Mode command_mode=Mode::Full;
ble_npl_event command_event;ble_npl_callout poll_callout;
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
ble_npl_event g1_cleanup_event;
#endif
uint64_t micros(){const int64_t t=esp_timer_get_time();return t>0?uint64_t(t):0;}
void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
void api_time(uint64_t start){last_api=micros()-start;if(last_api>max_api)max_api=last_api;}
void publish(){
  portENTER_CRITICAL(&mux);
  snapshot.client=client;snapshot.reports=reports;snapshot.incomplete=incomplete;
  snapshot.oversize=oversize;snapshot.late=late;snapshot.api_errors=api_errors;
  snapshot.resets=resets;snapshot.queue_drops=queue.drops;snapshot.queue_peak=queue.peak;
  snapshot.host_ready=ready;snapshot.radio_requested=client.requested;
  snapshot.last_api_us=last_api;snapshot.max_api_us=max_api;
  // Successful HCI commands do not independently establish RF silence.
  snapshot.physical_quiet=false;snapshot.host_work_busy=host_depth!=0;
  snapshot.command_pending=command_pending;
  snapshot.off_acknowledged=!lease.exhausted&&!stop_exhausted&&stop_processed==stop_generation&&!host_depth&&!command_pending&&!client.requested&&!client.scan&&!client.pending&&!client.synced;
  if(snapshot.off_acknowledged&&(lease.busy||(lease.generation&&!lease.completed_us)))lease.stopped(micros());
  lease.stop_generation=stop_generation;lease.stop_completed=stop_processed;snapshot.lease=lease;
  portEXIT_CRITICAL(&mux);
}
// Covers yielding HCI calls and callbacks, so stale OFF snapshots cannot
// certify a drain before the native host operation actually returns.
struct HostWork {
  HostWork(){portENTER_CRITICAL(&mux);++host_depth;portEXIT_CRITICAL(&mux);publish();}
  ~HostWork(){portENTER_CRITICAL(&mux);--host_depth;portEXIT_CRITICAL(&mux);publish();}
};
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
void g1_cleanup(ble_npl_event*){HostWork work;r2_host_cleanup();}
#endif
void enqueue(Report& row){portENTER_CRITICAL(&mux);queue.push(row);portEXIT_CRITICAL(&mux);}
int gap(ble_gap_event* event,void*);
void stop_radio(uint64_t now){
  client.stop_begin(now);
  if(client.scan&&!scan_cancel_sent){scan_cancel_sent=true;const uint64_t t=micros();const int rc=ble_gap_disc_cancel();api_time(t);
    if(rc==0||rc==BLE_HS_EALREADY)client.scan=false;else{inc(api_errors);client.fail();}}
  if(client.pending&&!cancel_sent){cancel_sent=true;const uint64_t t=micros();const int rc=ble_gap_periodic_adv_sync_create_cancel();api_time(t);
    // Cancellation success is command admission only. Pending remains until
    // SYNC completion; a racing successful completion must be terminated.
    if(rc==0||rc==BLE_HS_EALREADY)cancel_sent=true;
    else{inc(api_errors);client.fail();}}
  if(client.synced&&sync_handle!=UINT16_MAX&&!terminate_sent){terminate_sent=true;const uint64_t t=micros();
    const int rc=ble_gap_periodic_adv_sync_terminate(sync_handle);api_time(t);
    // Wait for NimBLE's queued SYNC_LOST after successful HCI termination.
    if(rc==0)terminate_sent=true;
    else if(rc==BLE_HS_ENOTCONN){client.synced=false;sync_handle=UINT16_MAX;}
    else if(rc){inc(api_errors);client.fail();}}
  client.drained(micros());
}
void poll(ble_npl_event*){
  HostWork work;
  const uint64_t now=micros();
  if(ready){direct_host_poll();diagnostic_tx_poll();
#ifdef V12_R2_ENABLED
    r2_host_poll();
#endif
  }
  if(lease_end&&now>=lease_end){client.requested=false;lease_end=0;
    portENTER_CRITICAL(&mux);if(lease.stop_for_generation!=lease.generation)lease.stop_request();stop_generation=lease.stop_generation;stop_processed=stop_generation;
    lease.deadline_exceeded=true;portEXIT_CRITICAL(&mux);}
  if(ready&&(client.should_stop(now)||client.state==Lifecycle::Fault))stop_radio(now);
  if(client.late(now))client.fail();
  const auto tx=diagnostic_tx_status();
  if(client.requested&&(tx.requested||tx.periodic||tx.discovery||tx.fault))client.fail();
  if(ready&&client.requested&&(client.state==Lifecycle::Off||client.state==Lifecycle::Quiet)&&client.open(now)){
    scan_cancel_sent=cancel_sent=terminate_sent=false;
    ble_gap_ext_disc_params params{};params.passive=1;params.itvl=32;params.window=32;
    const uint64_t t=micros();
    const int rc=ble_gap_ext_disc(BLE_OWN_ADDR_PUBLIC,0,0,0,0,0,&params,nullptr,gap,nullptr);api_time(t);
    if(rc==0){client.scan=true;if(lease_end&&micros()>=lease_end){client.requested=false;stop_radio(micros());}}
    else{inc(api_errors);client.fail();}
  }
  publish();fault_watchdog_host_progress();ble_npl_callout_reset(&poll_callout,ble_npl_time_ms_to_ticks32(1));
}
int gap(ble_gap_event* event,void*){
  HostWork work;
  const uint64_t now=micros();
  if(lease_end&&now>=lease_end){client.requested=false;stop_radio(now);}
  Report row;row.callback_us=now;row.cycle=client.cycle;
  if(event->type==BLE_GAP_EVENT_EXT_DISC){
    const auto& d=event->ext_disc;
    row.kind=1;row.sid=d.sid;row.address_type=d.addr.type;std::memcpy(row.address_le,d.addr.val,6);row.rssi=d.rssi;row.status=d.data_status;row.actual_length=d.length_data;
    row.metadata_mask=1;row.discovery_props=d.props;row.legacy_event_type=d.legacy_event_type;
    row.primary_phy=d.prim_phy;row.secondary_phy=d.sec_phy;row.advertised_tx_power=d.tx_power;row.periodic_interval=d.periodic_adv_itvl;
    row.length=uint8_t(d.length_data>MaxData?MaxData:d.length_data);
    if(d.data_status!=BLE_HCI_ADV_DATA_STATUS_COMPLETE){inc(incomplete);row.refusal|=2;}
    if(d.length_data>MaxData){inc(oversize);row.refusal|=4;}
    if(row.length)std::memcpy(row.bytes,d.data,row.length);
    enqueue(row);
    // Structural discovery only, no trust/freshness/precision promotion.
    // Initial diagnostic acquisition is finite, unauthenticated and never time/GM authority.
    if(!row.refusal&&client.requested&&client.state==Lifecycle::Acquire&&!client.pending&&!client.synced&&
       d.periodic_adv_itvl==25&&((d.length_data==GrantBytes&&d.data[0]==GrantBytes-1&&d.data[1]==0xff)||
       t096_initial_discovery(d.data,d.length_data,d.sid))){
      ble_gap_periodic_sync_params params{};params.skip=0;params.sync_timeout=500;
      const uint64_t t=micros();const int rc=ble_gap_periodic_adv_sync_create(&d.addr,d.sid,&params,gap,nullptr);api_time(t);
      if(rc==0){client.pending=true;cancel_sent=false;sync_peer=d.addr;sync_sid=d.sid;}else{inc(api_errors);client.fail();}
    }
  }else if(event->type==BLE_GAP_EVENT_PERIODIC_SYNC){
    client.pending=false;cancel_sent=false;row.kind=2;row.status=event->periodic_sync.status;
    row.handle=event->periodic_sync.sync_handle;row.sid=event->periodic_sync.sid;row.address_type=sync_peer.type;std::memcpy(row.address_le,sync_peer.val,6);enqueue(row);
    if(event->periodic_sync.status==0){
      client.synced=true;terminate_sent=false;sync_handle=event->periodic_sync.sync_handle;
      if(!client.requested||client.state==Lifecycle::Stopping||client.state==Lifecycle::Fault||
         (!online(client.mode)&&now>=client.stop_by))stop_radio(now);
      else client.state=Lifecycle::Following;
      // Retain extended scan for grant refresh; both scan and sync stop before
      // quiet. No report mask or duplicate suppression implements quiet.
    }else if(client.state==Lifecycle::Stopping)client.drained(now);
  }else if(event->type==BLE_GAP_EVENT_PERIODIC_REPORT){
    const auto& r=event->periodic_report;row.kind=3;row.handle=r.sync_handle;row.sid=sync_sid;row.address_type=sync_peer.type;std::memcpy(row.address_le,sync_peer.val,6);row.rssi=r.rssi;
    row.status=r.data_status;row.actual_length=r.data_length;
    row.length=uint8_t(r.data_length>MaxData?MaxData:r.data_length);
    if(!client.accept_report(now)){inc(late);row.refusal|=1;}
    if(r.data_status!=BLE_HCI_PERIODIC_DATA_STATUS_COMPLETE){inc(incomplete);row.refusal|=2;}
    if(r.data_length>MaxData){inc(oversize);row.refusal|=4;}
    if(row.length)std::memcpy(row.bytes,r.data,row.length);
    enqueue(row);if(!row.refusal)inc(reports);
  }else if(event->type==BLE_GAP_EVENT_PERIODIC_SYNC_LOST){
    row.kind=4;row.handle=event->periodic_sync_lost.sync_handle;row.status=uint8_t(event->periodic_sync_lost.reason);enqueue(row);
    if(row.handle==sync_handle){client.synced=false;terminate_sent=false;sync_handle=UINT16_MAX;}
    if(client.state==Lifecycle::Following)client.state=Lifecycle::Acquire;
    client.drained(now);
  }else if(event->type==BLE_GAP_EVENT_DISC_COMPLETE){
    client.scan=false;
    if(client.state!=Lifecycle::Stopping&&client.requested)client.fail();
    client.drained(now);
  }
  publish();return 0;
}
void reset(int reason){ready=false;
  // A shared-host reset belongs to admitted owners. An unused legacy RX
  // path remains OFF; the R2 owner retains its own original failure record.
  if(lease.generation||client.requested||client.scan||client.pending||client.synced)client.fail();
  direct_host_reset();diagnostic_tx_reset();inc(resets);
#ifdef V12_R2_ENABLED
#if V12_R2_CONFIG == 13
  r2_host_reset(reason);
#else
  r2_host_reset();
#endif
#endif
  publish();}
void sync(){
  ready=ble_hs_util_ensure_addr(0)==0;
#ifdef V12_R2_ENABLED
  ready=ready&&r2_host_sync();
#endif
  if(!ready)client.fail();
  publish();
}
void host(void*){nimble_port_run();
#ifdef PTOB_W12_STATIC_NIMBLE_HOST
  host_task.exited();
#else
  nimble_port_freertos_deinit();
#endif
}
void command(ble_npl_event*){
  HostWork work;bool stop;Mode mode;uint32_t generation;uint64_t deadline;bool timely=false;
  portENTER_CRITICAL(&mux);stop=command_stop;mode=command_mode;generation=stop_generation;deadline=lease.deadline_us;
  if(!stop)timely=lease.start(micros());
  command_pending=false;portEXIT_CRITICAL(&mux);
  if(stop){client.requested=false;lease_end=0;stop_radio(micros());
    portENTER_CRITICAL(&mux);stop_processed=generation;portEXIT_CRITICAL(&mux);}
  else if(timely&&ready&&client.configure(mode)){
    client.requested=true;lease_end=deadline;
  }else{client.fail();portENTER_CRITICAL(&mux);lease.pending=false;lease.busy=false;lease.completed_us=micros();portEXIT_CRITICAL(&mux);}
  publish();
}
}
bool transport_init(){
  if(nimble_port_init()!=ESP_OK)return false;
#ifdef V12_R2_ENABLED
  if(!r2_host_init())return false;
#endif
  ble_hs_cfg.reset_cb=reset;ble_hs_cfg.sync_cb=sync;
  ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;
  ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_mitm=0;ble_hs_cfg.sm_bonding=0;
  ble_hs_cfg.sm_our_key_dist=ble_hs_cfg.sm_their_key_dist=0;
  diagnostic_tx_init();
  ble_npl_event_init(&command_event,command,nullptr);
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
  ble_npl_event_init(&g1_cleanup_event,g1_cleanup,nullptr);
#endif
  ble_npl_callout_init(&poll_callout,nimble_port_get_dflt_eventq(),poll,nullptr);
  ble_npl_callout_reset(&poll_callout,ble_npl_time_ms_to_ticks32(1));
#ifdef PTOB_W12_STATIC_NIMBLE_HOST
  const bool started=host_task.start(host,configMAX_PRIORITIES-4,NIMBLE_CORE);
  std::printf("W12_NIMBLE_HOST allocation=STATIC started=%u stack_bytes=%u core=%d heap_free=%u largest_block=%u\n",
    unsigned(started),unsigned(CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE),int(NIMBLE_CORE),
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
  return started;
#else
  nimble_port_freertos_init(host);return true;
#endif
}
bool transport_request(Mode mode,uint32_t duration_ms,uint64_t request_id){
  if(mode==Mode::GM||mode==Mode::Relay||duration_ms<1000||duration_ms>10000)return false;
  const auto tx=diagnostic_tx_status();const auto link=direct_status();
  if(tx.periodic||tx.discovery||tx.requested||tx.fault||tx.lease.pending||tx.owner_busy||!tx.off_acknowledged||
     (link.stage!=DirectStage::Off&&mode!=Mode::Full))return false;
  portENTER_CRITICAL(&mux);
  const bool admitted=!command_pending&&snapshot.off_acknowledged&&snapshot.host_ready&&snapshot.client.state==Lifecycle::Off&&lease.admit(micros(),duration_ms,request_id);
  if(admitted){command_pending=true;command_stop=false;command_mode=mode;snapshot.command_pending=true;snapshot.off_acknowledged=false;snapshot.lease=lease;}
  portEXIT_CRITICAL(&mux);
  if(admitted)ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&command_event);
  return admitted;
}
void transport_stop(){
  portENTER_CRITICAL(&mux);command_pending=true;command_stop=true;
  lease.stop_request();stop_generation=lease.stop_generation;stop_exhausted=lease.exhausted;
  snapshot.lease=lease;
  snapshot.command_pending=true;snapshot.off_acknowledged=false;portEXIT_CRITICAL(&mux);
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&command_event);
}
#if defined(V12_R2_ENABLED) && V12_R2_CONFIG == 13
void transport_g1_cleanup_schedule(){ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&g1_cleanup_event);}
#endif
bool transport_pop(Report& row){portENTER_CRITICAL(&mux);const bool have=queue.pop(row);portEXIT_CRITICAL(&mux);return have;}
#ifdef V12_R2_ENABLED
void transport_record(const Report& row){portENTER_CRITICAL(&mux);queue.push(row);portEXIT_CRITICAL(&mux);}
#endif
void transport_observe_lease(uint64_t now){
  portENTER_CRITICAL(&mux);const bool stop=lease.observe(now);
  if(stop){command_pending=command_stop=true;stop_generation=lease.stop_generation;snapshot.command_pending=true;snapshot.off_acknowledged=false;}
  snapshot.lease=lease;portEXIT_CRITICAL(&mux);
  if(stop)ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&command_event);
}
TransportStatus transport_status(){transport_observe_lease(micros());portENTER_CRITICAL(&mux);const auto s=snapshot;portEXIT_CRITICAL(&mux);return s;}
}
