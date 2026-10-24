// G1 public client: one transient outgoing ACL, no control participant or source.
// One NimBLE host-event owner. Callbacks copy fixed records; crypto runs here.
#include "native_r2.hpp"
#include "native_device_binding.hpp"
#include "native_transport.hpp"
#include "native_direct.hpp"
#include "native_diagnostic_tx.hpp"
#include "periodic_rx.hpp"
#ifdef PTOB_RC9
#include "rc9/client.hpp"
#ifdef PTOB_W12_LORA_RX
#include "w12_lora.hpp"
#include "sync_watchdog.hpp"
#include "rc9_source_policy.hpp"
#endif
#endif
#ifdef PTOB_N30
#include "fine_backend_api.hpp"
#elif defined(PTOB_W12_FINE_CLOCK)
#include "w12_fine_clock.hpp"
#endif
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
#include "bounded_log.hpp"
#include "periodic_tx.hpp"
#include "rc7_relay_estimate.hpp"
#include "rc7_relay_hold.hpp"
extern "C" {
#include "r2/rc7/include/rc_slots.h"
}
#endif
#include "r2_discipline.hpp"
#include "g1_client_policy.hpp"
#include "g1_direct_session.hpp"
#include "g1_admission_policy.hpp"
#include "g1_retained_estimate.hpp"
#include "g1_receive_loss.hpp"
#include "g1_corrected_clock.hpp"
#include "g1_output_correction.hpp"
#include "rc7_provisional_config.hpp"
#include "g1_parent_selection.hpp"
#include "g1_gm_discovery.hpp"
#include "rc7_path_snapshot.hpp"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_bt.h"
#include "freertos/FreeRTOS.h"
#ifdef V12_R2_RX_SHADOW
#include "freertos/task.h"
extern "C" bool clock_reference_read(uint64_t*);
#endif
#include <cstring>
extern "C" {
#include "group_join.h"
#include "group_precision.h"
}
namespace v12 {
namespace {
struct r2_group_enrollment {r2_group_config group;uint64_t identity;char name[25];uint32_t map_bound_us;};
#include "r2_bench_enrollment.h"
#ifndef V12_R2_G1_LISTENING_PERCENT
#define V12_R2_G1_LISTENING_PERCENT 50
#endif
static_assert(V12_R2_G1_LISTENING_PERCENT==50||V12_R2_G1_LISTENING_PERCENT==100,"G1 supports only reviewed50/100 listening profiles");
constexpr uint16_t None=UINT16_MAX,PrecisionInterval=50;
static_assert(BLE_HS_ETIMEOUT_HCI==19,"Pinned host timeout domain changed");
const ble_uuid128_t service=BLE_UUID128_INIT(0x5c,0xd4,0x4e,0x7d,0x8a,0x18,0x4b,0x97,0x75,0x47,0xef,0xa6,0x00,0x04,0x0d,0x85);
const ble_uuid128_t data_uuid=BLE_UUID128_INIT(0x5c,0xd4,0x4e,0x7d,0x8a,0x18,0x4b,0x97,0x75,0x47,0xef,0xa6,0x01,0x04,0x0d,0x85);
const ble_uuid128_t rc_service=BLE_UUID128_INIT(0x07,0x00,0xbc,0x51,0xe2,0xd4,0x38,0x8c,0x94,0x4a,0xc4,0x76,0x38,0xe5,0xf6,0x48);
const ble_uuid128_t rc_data_uuid=BLE_UUID128_INIT(0x07,0x00,0xbd,0x51,0xe2,0xd4,0x38,0x8c,0x94,0x4a,0xc4,0x76,0x38,0xe5,0xf6,0x48);
#ifdef PTOB_RC9
const ble_uuid128_t rc9_service=BLE_UUID128_INIT(0x09,0x00,0xbc,0x51,0xe2,0xd4,0x38,0x8c,0x94,0x4a,0xc4,0x76,0x38,0xe5,0xf6,0x48);
const ble_uuid128_t rc9_data_uuid=BLE_UUID128_INIT(0x0a,0x00,0xbd,0x51,0xe2,0xd4,0x38,0x8c,0x94,0x4a,0xc4,0x76,0x38,0xe5,0xf6,0x48);
#endif
struct Peer {
  uint64_t identity=0,attempt_until=0,gatt_until=0,close_at=0,drain_until=0;
  uint64_t request_at=0,descriptor_at=0,probe_at=0,terminate_at=0;
  uint16_t interval=6,latency=0,supervision=10;
  uint32_t incarnation=0;uint16_t handle=None,value=0,start=0,end=0,mtu=0;
  unsigned stage=0;bool connecting=false,pending=false,ready=false,dead=false,terminating=false;
  ble_addr_t address{};r2_join_exchange exchange{};uint8_t send_kind=0;bool relay=false,rescan=false;uint64_t read_at=0;
  bool selection_accepted=false,selection_reported=false;
  bool rc9=false;
  G1ParentReason parent_reason=G1ParentReason::Startup;
};
struct Incoming {uint64_t at=0;uint32_t incarnation=0;uint8_t bytes[RC_DYNAMIC_CONTROL_BYTES]{};uint16_t length=R2_GROUP_JOIN_BYTES;};
struct Discovery {int8_t rssi=127,tx_power=127;uint64_t at=0;ble_addr_t address{};uint16_t interval=0;uint8_t sid=0,props=0,legacy_event_type=0,primary_phy=0,secondary_phy=0,length=0;uint8_t bytes[191]{};};
struct Periodic {uint64_t at=0,identity=0;uint8_t bytes[190]{};uint8_t length=0;periodic_rx::Stamp capture{};
#ifdef PTOB_N30
 uint32_t signal_epoch=0;int8_t rssi=127;
#endif
};
#ifdef PTOB_RC9
rc9::Client rc9_client;
Rc9Status rc9_published;
uint64_t rc9_target=0,rc9_retry_at=0;
struct Rc9Locator {Discovery discovery{};uint64_t id=0,boot=0,seen=0;};
Rc9Locator rc9_locators[4]{};
bool rc9_bound(){return rc9_client.view(uint64_t(esp_timer_get_time())).bound;}
bool rc9_server_selected(uint64_t);
uint64_t rc9_server_demand_until();
uint8_t rc9_server_clients(uint64_t);
uint8_t rc9_server_flags();
void rc9_publish_status(uint64_t);
#endif
#ifdef PTOB_N30
n30::SignalStrength signal_strength;
void signal_received(const Periodic& row,uint64_t now,uint32_t epoch){if(row.signal_epoch==epoch)signal_strength.observe(row.identity,row.signal_epoch,row.rssi,row.at,now);}
#endif
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
Periodic fine_pending{};bool fine_waiting=false;
#endif
struct Recent {uint64_t identity=0,until=0;};
using Anchor=G1Anchor;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
R2Status snap;OwnerLease lease;R2ClientService client;Peer peer;
G1Diagnostics diagnostic;
Ring<Incoming,8> incoming;Ring<Discovery,16> discoveries;Ring<Periodic,8> periodic;
Recent recent[8]{};r2_join_display display[8]{};Anchor anchor;G1StrictDiscipline discipline;
G1PhaseLearning phase_learning;
G1DirectSession direct_session;
G1CoarseHold coarse_hold;
G1CorrectedClock corrected_clock;
G1AdmissionPolicy admission_policy;G1RetainedEstimate retained_estimate;G1ReceiveLoss receive_loss;
uint32_t retained_accepted=0;bool corrected_was_live=false;
Rc7Client rc_client;
#ifndef V12_G1_FAILED_PARENT_RETRY_US
#define V12_G1_FAILED_PARENT_RETRY_US 0
#endif
G1ParentSelection parent_selection{V12_G1_FAILED_PARENT_RETRY_US};
G1GmDiscovery gm_discovery;
G1ParentJournal parent_journal;
// A callback records one bounded outcome; only the serialized worker updates
// advisory server history. The next attempt cannot start before its retry gap.
struct ParentFailure {uint64_t identity=0,at=0;bool relay=false;};
ParentFailure parent_failure;
bool clear_parent_history=false;
RcParentDiagnostic parent_diagnostic;
Discovery locators[8]{};uint64_t locator_ids[8]{};
#ifdef V12_R2_RC7_EXCLUDE_DIRECT_GM
bool exclude_direct_gm=true;
#else
bool exclude_direct_gm=false;
#endif
// The old compile flag supplies only a compatibility boot default. Runtime
// changes are queued by main and applied solely after the worker's original
// parent-rebind drain. Diagnostic locator pins remain a separate policy.
bool relay_only_requested=exclude_direct_gm,upstream_mode_change=false;
bool rc_bound(){return rc_client.receiver.clock.bound;}
bool usable_utc(){return !rc_bound()||rc_client.utc_known;}
G1ResetRecovery recovery;
bool corrected_phase_aligned=false;
R2Mode mode=R2Mode::Off,requested_mode=R2Mode::Off;
bool initialized=false,host_ready=false,command_pending=false,stop_pending=false,closing=false,fault=false;
bool boot_client_attempted=false,boot_client_cancelled=false,active_used=false;
bool scan=false,connecting_cancelled=false,sync_pending=false,synced=false,sync_selected=false,sync_cancelled=false,sync_terminating=false;
bool sync_cleanup_queued=false,sync_cleanup_requested=false,window_faulted=false;
bool member=false;uint64_t boot_id=0,nonce=0,sequence=0,joined_peer=0,joined_boot=0;
uint64_t admission_root=0,admission_until=0; // Authenticated locator hint, never time authority.
uint64_t close_until=0,next_poll=0,retry_at=0,scan_retry_at=0,scan_retry_until=0;
uint64_t sync_identity=0,sync_acquire_until=0,sync_drain_until=0;
uint8_t sync_selected_sid=12;
uint32_t sync_capture_epoch=0;
uint64_t sync_requested_at=0,sync_owner_window_start=UINT64_MAX;uint8_t sync_phy=0,sync_clock_accuracy=255;
ble_addr_t own_address{},sync_address{};uint16_t sync_handle=None,observed_mtu=0;
uint8_t last_gatt_stage=0;
uint32_t incarnation=0,rx=0,tx=0,auth=0,rejected=0,join_attempts=0,join_accepts=0,join_busy=0,pa_reports=0,p1_ok=0,p1_bad=0,discovery_count=0;
uint32_t api_errors=0,setup_retries=0,peer_losses=0,pending_disconnects=0,pending_link_teardowns=0,cancel_calls=0,host_stack_min=UINT32_MAX;
int32_t cancel_rc=0,last_disconnect_reason=0;uint64_t last_disconnect_at=0;bool sdk_connect_pending=false;uint64_t host_stack_read_us=0;
uint32_t adv_callbacks=0,adv_incomplete=0,adv_long=0,adv_nonrandom=0,discovery_checked=0,group_records=0,last_key_epoch=0;
uint64_t last_adv_at=0,last_group_id=0,last_group_at=0;
uint64_t radio_resume_at=0;
uint8_t listening_percent=V12_R2_G1_LISTENING_PERCENT;
uint32_t window_phase=0,cold_windows=0;
uint64_t window_start=UINT64_MAX,window_closed_at=0,window_last_poll=0,window_owned_us=0,window_max_owned_us=0;
uint64_t last_origin_ns=0;uint32_t windows=0,window_misses=0,quiet_faults=0,refresh_attempts=0,relay_ok=0,relay_bad=0,relay_train=0;
bool window_corrected=false;
R2Api last_api=R2Api::None,first_error_api=R2Api::None;int32_t last_api_rc=0,first_error_rc=0;uint64_t last_api_at=0,first_error_at=0;
uint8_t first_fault_site=0;
bool parent_scope(){return rc_bound()?sync_identity==joined_peer&&joined_peer==rc_client.receiver.path.node[rc_client.receiver.path.depth].id:anchor.root==joined_peer&&anchor.boot==joined_boot;}
uint64_t micros(){const int64_t n=esp_timer_get_time();return n>0?uint64_t(n):0;}
void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
void note(int rc,R2Api site){last_api=site;last_api_rc=rc;last_api_at=micros();}
void error(int rc,R2Api site,uint8_t detail=0){if(!rc)return;note(rc,site);if(!first_error_at){first_error_api=site;first_error_rc=rc;first_error_at=micros();first_fault_site=detail;}inc(api_errors);fault=true;
  if(rc==BLE_HS_ETIMEOUT_HCI&&site==R2Api::SyncCancel){portENTER_CRITICAL(&mux);recovery.timeout(client.generation,mode==R2Mode::Client&&!boot_client_cancelled);portEXIT_CRITICAL(&mux);}
}
bool transient(int rc,R2Api site){if(rc==BLE_HS_EBUSY||rc==BLE_HS_ENOMEM||rc==BLE_HS_EPREEMPTED){note(rc,site);inc(setup_retries);return true;}return false;}
bool same_address(const ble_addr_t& a,const ble_addr_t& b){return a.type==b.type&&!std::memcmp(a.val,b.val,6);}
ble_addr_t canonical(uint64_t id){uint8_t key[16];r2_group_pair_key(key,&provision.group,id,id);ble_addr_t a{};a.type=BLE_ADDR_RANDOM;std::memcpy(a.val,key,6);a.val[5]|=0xc0;return a;}
bool full_listening(){return mode==R2Mode::Client&&listening_percent==100;}
bool client_window_active(uint64_t now,uint32_t phase){return full_listening()||G1Window::active(now,phase);}
bool anchor_valid(uint64_t now){return anchor.valid(now);}
bool following(uint64_t now){return usable_utc()&&anchor.live(now);}
// Common current-parent authority checks; scheduling never adds permission.
bool current_parent_time(uint64_t now){
  if(mode!=R2Mode::Client||closing||fault||parent_diagnostic.pending()||!joined_peer||
     !following(now)||!corrected_clock.covers(now,now)||
     !parent_selection.accepts(joined_peer,rc_bound())||(exclude_direct_gm&&!rc_bound()))return false;
  // Intentional50% quiet retires the PA incarnation, not the authenticated
  // admission or its still-valid held model. Check that binding separately.
  if(sync_identity&&sync_identity!=joined_peer)return false;
  if(rc_bound()){
    const auto& path=rc_client.receiver.path;
    if(path.depth<1||path.depth>2||now>=rc_client.lease_until||
       path.node[path.depth].id!=joined_peer||path.node[path.depth].boot!=joined_boot||
       path.node[0].id!=anchor.root||path.node[0].boot!=anchor.boot||path.source_epoch!=anchor.source_epoch)return false;
  }else if(anchor.root!=joined_peer||anchor.boot!=joined_boot)return false;
  return true;
}
bool lock_protected_quality(const G1RelativeQuality& q,const G1ClockPrediction& clock,uint64_t tick,uint64_t now){
  // Same raw sub-1us/one-second hold prerequisites as the lock timer.
  if(!current_parent_time(now))return false;
  const auto until=anchor.deadline();
  return tick&&until>now&&q.locked(tick,clock)&&q.hold_ms(tick,clock,until-now)>=1000;
}
// Reserve an actually arriving authenticated precision stream independently
// of its forecast quality. A short quality dip must not force broad discovery
// just to renew this same parent. This is scheduling only: lock, downstream
// eligibility and every source/model/replay deadline retain their own gates.
bool precision_reserved(uint64_t now){
  if(!full_listening()||!synced||sync_terminating||sync_cleanup_queued||
     sync_identity!=joined_peer||!current_parent_time(now))return false;
  const auto received=parent_selection.quality().last_rx;
  if(!received||now<received||now-received>=250000)return false;
#ifdef V12_R2_RX_SHADOW
  uint64_t tick=0;const auto clock=corrected_clock.prediction();
  return clock.tick&&clock_reference_read(&tick)&&tick>=clock.tick;
#else
  return false;
#endif
}
bool lock_protected(uint64_t now){
#ifdef V12_R2_RX_SHADOW
  uint64_t tick=0;if(!clock_reference_read(&tick))return false;
  return lock_protected_quality(corrected_clock.quality(now,tick),corrected_clock.prediction(),tick,now);
#else
  (void)now;return false;
#endif
}
bool selection_protected(uint64_t now){
  // Once N30 exhausts its lock trial, even a brief raw-lock return must leave
  // discovery/admission room for alternatives until sustained recovery.
  if(V12_G1_FAILED_PARENT_RETRY_US&&parent_selection.needs_alternative())return false;
  return lock_protected(now)||(precision_reserved(now)&&!parent_selection.needs_alternative());
}
int64_t coarse_ns(uint64_t now){if(!usable_utc()||!anchor_valid(now)||anchor.utc>uint64_t(INT64_MAX)/1000||now-anchor.at>uint64_t(INT64_MAX)/1000-anchor.utc)return -1;return int64_t((anchor.utc+now-anchor.at)*1000);}
void discipline_scope(uint64_t now){discipline.scope({anchor.root,anchor.boot,anchor.until,anchor.source_epoch,anchor.publication,periodic_rx::status().epoch,0,following(now)&&!closing&&!fault,true});}
bool timely(){const auto now=micros();portENTER_CRITICAL(&mux);const bool ok=!parent_diagnostic.pending()&&parent_diagnostic.state!=4&&!stop_pending&&!fault&&(mode==R2Mode::Client?client.running&&!client.stopping&&!client.deadline_exceeded:lease.busy&&lease.deadline_us>=2000000&&now<lease.deadline_us-2000000);portEXIT_CRITICAL(&mux);return ok;}
uint32_t admission_random(){uint32_t value=0;esp_fill_random(&value,sizeof value);return value;}
void admission_failed(uint64_t now){
  const auto replay=(!peer.relay&&peer.identity==joined_peer&&precision_reserved(now))?anchor.deadline():0;
  admission_policy.failed(now,admission_random(),replay,full_listening()&&!peer.relay);
}
void failed_admission(uint64_t now){
  if(!peer.identity||peer.selection_accepted||peer.selection_reported||closing||fault||stop_pending||parent_diagnostic.pending()||mode!=R2Mode::Client)return;
  // Deliberate profile quiet, explicit stop and rebind are local scheduling,
  // not observations that the remote server failed.
  if(!full_listening()&&now-G1Window::start(now,window_phase)>=400000)return;
#ifdef PTOB_RC9
  if(peer.rc9){peer.selection_reported=true;rc9_retry_at=now+1000000;return;}
#endif
  peer.selection_reported=true;admission_failed(now);
  if(!parent_failure.identity)parent_failure={peer.identity,now,peer.relay};
}
bool relay_slot_scanning();
void relay_fence();void relay_close(uint64_t now,bool preserve_demand=false);bool relay_owned();bool relay_acl_available();void relay_poll(uint64_t now,bool upstream_due,bool admitting=false);
void parent_record(G1ParentReason reason,uint64_t now);
void begin_close(uint64_t now){if(closing)return;closing=true;
#ifdef PTOB_RC9
 rc9_client.fence();
#endif
corrected_was_live=false;retained_estimate.clear();relay_fence();member=false;anchor={};direct_session.clear();rc_client.fence();coarse_hold.clear();corrected_clock.clear();corrected_phase_aligned=false;close_until=now+2000000;portENTER_CRITICAL(&mux);if(mode==R2Mode::Client){client.stop(now);close_until=client.stop_deadline_us;}else if(lease.deadline_us&&lease.deadline_us<close_until)close_until=lease.deadline_us;portEXIT_CRITICAL(&mux);parent_record(G1ParentReason::Stop,now);}
void stop_scan(){if(!scan)return;const int rc=ble_gap_disc_cancel();if(!rc||rc==BLE_HS_EALREADY)scan=false;else error(rc,R2Api::ScanStop);}
int gap(ble_gap_event*,void*);
struct LinkContext {uint32_t generation=0;
  uint64_t identity=0,window_start=0,request_at=0;ble_addr_t address{};uint16_t interval=0,latency=0,supervision=0;
  uint8_t stage=0,owners=0,pa_phy=0,pa_clock_accuracy=0;};
LinkContext link_context(uint64_t now){LinkContext c;
  c.generation=peer.incarnation;c.identity=peer.identity;c.address=peer.address;
  c.window_start=G1Window::start(now,window_phase);c.request_at=peer.request_at;
  c.interval=peer.interval;c.latency=peer.latency;c.supervision=peer.supervision;c.stage=uint8_t(peer.stage);
  c.owners=uint8_t(scan)|uint8_t(peer.connecting)<<1|uint8_t(peer.handle!=None)<<2|uint8_t(sync_pending)<<3|uint8_t(synced)<<4|uint8_t(peer.terminating)<<5;
  return c;}
void admission_trace(uint8_t,uint16_t,int,const LinkContext* =nullptr);
LinkContext sync_context(){
  auto context=link_context(micros());context.generation=0;context.identity=sync_identity;
  context.address=sync_address;context.request_at=sync_requested_at;context.interval=PrecisionInterval;
  context.window_start=sync_owner_window_start==UINT64_MAX?G1Window::start(sync_requested_at,window_phase):sync_owner_window_start;
  context.latency=0;context.supervision=100;context.stage=synced?2:sync_pending?1:0;
  context.pa_phy=sync_phy;context.pa_clock_accuracy=sync_clock_accuracy;return context;
}
void sync_trace(uint8_t event,uint16_t handle,int result,const LinkContext* saved=nullptr){
  const auto context=saved?*saved:sync_context();admission_trace(event,handle,result,&context);
}
void start_scan(bool gm_observation=false){
  if(!timely()||(!gm_observation&&!relay_slot_scanning()&&selection_protected(micros()))||micros()<scan_retry_at||(mode==R2Mode::Client&&!client_window_active(micros(),window_phase)))return;
  const auto now=micros();if(!scan_retry_until)scan_retry_until=now+2000000;
  if(now>=scan_retry_until){error(BLE_HS_ETIMEOUT,R2Api::ScanStart);return;}
  ble_gap_ext_disc_params p{};p.passive=1;p.itvl=32;p.window=32;
  const int rc=ble_gap_ext_disc(BLE_OWN_ADDR_RANDOM,0,0,0,0,0,&p,nullptr,gap,nullptr);
  if(!rc||(rc==BLE_HS_EALREADY&&ble_gap_disc_active())){scan=true;scan_retry_at=scan_retry_until=0;}
  else if(transient(rc,R2Api::ScanStart))scan_retry_at=now+100000;
  else error(rc,R2Api::ScanStart);
}
void terminate_peer(uint64_t now){
  if(peer.handle==None||peer.terminating)return;
  peer.ready=false;const auto h=peer.handle;const auto generation=peer.incarnation;
  const auto context=link_context(now);peer.terminate_at=now;
  // A public HCI call can yield while NimBLE finishes the delayed CONNECT for
  // an adopted ACL. Install its terminating owner and original deadline first;
  // that callback may acknowledge cleanup, but cannot restart GATT or the drain.
  peer.terminating=true;
  if(!peer.drain_until||peer.drain_until>now+2000000)peer.drain_until=now+2000000;
  const int rc=ble_gap_terminate(h,BLE_ERR_REM_USER_CONN_TERM);
  admission_trace(7,h,rc,&context);
  if(peer.handle!=h||peer.incarnation!=generation)return;
  if(!rc||rc==BLE_HS_EALREADY||rc==BLE_HS_ENOTCONN){if(rc==BLE_HS_ENOTCONN){note(rc,R2Api::Disconnect);inc(peer_losses);peer.dead=true;}}
  else{peer.terminating=false;error(rc,R2Api::Disconnect);}
}
void observed_descriptor(const ble_gap_conn_desc& d,uint64_t now){
  peer.interval=d.conn_itvl;peer.latency=d.conn_latency;peer.supervision=d.supervision_timeout;
  if(peer.descriptor_at)return;
  peer.descriptor_at=now;inc(diagnostic.descriptor_seen);admission_trace(13,d.conn_handle,0);
}
// Read-only public observation separates controller ACL presence from delayed
// SDK CONNECT. It neither adopts the handle nor changes controller ownership.
void probe_pending_link(uint64_t now){
  if(!peer.connecting||peer.descriptor_at||now<peer.probe_at)return;
  peer.probe_at=now+10000;if(ble_gap_conn_active())return;
  ble_gap_conn_desc d{};
  if(!ble_gap_conn_find_by_addr(&peer.address,&d)&&d.role==BLE_GAP_ROLE_MASTER&&
     d.conn_handle!=None&&same_address(d.peer_id_addr,peer.address)){
    const int level=int(esp_ble_tx_power_get_enhanced(ESP_BLE_ENHANCED_PWR_TYPE_CONN,d.conn_handle));
    diagnostic.conn_tx_dbm=level>=0&&level<=15?int8_t(level==15?20:-24+3*level):127;
    observed_descriptor(d,now);
  }
}
// A physical ACL can exist before the SDK posts CONNECT (remote feature/version
// exchange). Only a fresh public descriptor of this exact canonical master peer
// can be adopted for cleanup; it never grants readiness or join authority.
bool cleanup_pending_link(uint64_t now){
  if(!peer.connecting||ble_gap_conn_active())return false;
  ble_gap_conn_desc d{};
  if(ble_gap_conn_find_by_addr(&peer.address,&d)||d.role!=BLE_GAP_ROLE_MASTER||d.conn_handle==None||!same_address(d.peer_id_addr,peer.address))return false;
  observed_descriptor(d,now);
  const auto generation=peer.incarnation;const auto original=peer.attempt_until+2000000;
  if(!full_listening()&&now-G1Window::start(now,window_phase)>=200000)peer.selection_reported=true;
  peer.connecting=false;peer.handle=d.conn_handle;peer.dead=true;inc(pending_link_teardowns);
  if(!peer.drain_until||peer.drain_until>original)peer.drain_until=original;
  admission_trace(6,d.conn_handle,int(now-G1Window::start(now,window_phase)));
  terminate_peer(now);
  if(peer.incarnation==generation&&peer.terminating&&peer.drain_until>original)peer.drain_until=original;
  return true;
}
void sync_quiet_return(const LinkContext& context){
  const auto now=micros();
  // A yielding SDK call can span entire windows while this host worker cannot
  // poll. Preserve the original owner/window, even if a callback closes it.
  if(mode==R2Mode::Client&&!full_listening()&&context.owners&&context.window_start!=UINT64_MAX&&
     now>=context.window_start+G1Window::Quiet&&!window_faulted){
    window_faulted=true;admission_trace(8,sync_handle,int(now-context.window_start),&context);
    inc(quiet_faults);error(BLE_HS_ETIMEOUT,R2Api::Disconnect);
  }
}
void cancel_sync(uint64_t now){
  sync_selected=false;
  if((sync_pending&&!sync_cancelled)||(synced&&!sync_terminating)){
    if(!sync_drain_until)sync_drain_until=now+2000000;
    if(!sync_cleanup_requested)periodic_rx::scope(None); // Fence once; actual controller drain remains owned.
    sync_cleanup_requested=true;
    if(!sync_cleanup_queued){sync_cleanup_queued=true;sync_trace(22,sync_handle,0);transport_g1_cleanup_schedule();}
  }
}
void cancel_sync_worker(uint64_t now){
  if(sync_pending&&!sync_cancelled){sync_cancelled=true;if(!sync_drain_until)sync_drain_until=now+2000000;
    const auto context=sync_context();const auto handle=sync_handle;
    sync_trace(23,handle,0,&context);
    const int rc=ble_gap_periodic_adv_sync_create_cancel();sync_trace(18,handle,rc,&context);
    // The controller can complete create just before it sees CANCEL while the
    // SDK still has the establishment event queued. Retain the pending owner
    // and its original drain deadline until that actual callback arrives. No
    // retry, fake OFF or additional deadline; missing closure faults at500ms.
    if(rc==BLE_HS_HCI_ERR(0x0c))note(rc,R2Api::SyncCancel);
    else if(rc&&rc!=BLE_HS_EALREADY)error(rc,R2Api::SyncCancel);
    sync_quiet_return(context);
  }
  if(synced&&!sync_terminating){sync_terminating=true;if(!sync_drain_until)sync_drain_until=now+2000000;
    const auto context=sync_context();const auto handle=sync_handle;
    sync_trace(24,handle,0,&context);
    const int rc=ble_gap_periodic_adv_sync_terminate(handle);sync_trace(19,handle,rc,&context);
    if(rc==BLE_HS_ENOTCONN){synced=false;sync_terminating=false;sync_handle=None;periodic_rx::scope(None);}else if(rc)error(rc,R2Api::SyncTerminate);
    sync_quiet_return(context);}
}
void close_poll(uint64_t now){
  relay_close(now);stop_scan();cleanup_pending_link(now);terminate_peer(now);cancel_sync(now);
  if(peer.connecting&&!connecting_cancelled){const int rc=ble_gap_conn_cancel();if(!rc||rc==BLE_HS_EALREADY)connecting_cancelled=true;else error(rc,R2Api::ConnectCancel);}
  if(!relay_owned()&&!scan&&peer.handle==None&&!peer.connecting&&!sync_pending&&!synced){if(!periodic_rx::stop()){fault=true;return;}const bool service_mode=mode==R2Mode::Client;mode=R2Mode::Off;closing=false;portENTER_CRITICAL(&mux);if(service_mode)client.stopped(micros());else lease.stopped(micros());portEXIT_CRITICAL(&mux);}
  else if(now>=close_until)fault=true;
}
void* callback_arg(){return reinterpret_cast<void*>(uintptr_t(peer.incarnation));}
// Bounded host-callback diagnostics. Kind8 retains event byte + LE32 result,
// then version2 fixed context. No formatting/allocation; main task renders it.
// timestamps are API observations, never RF or timing-reference captures.
void admission_trace(uint8_t event,uint16_t handle,int result,const LinkContext* saved){
  Report r{};r.kind=8;r.callback_us=micros();const auto c=saved?*saved:link_context(r.callback_us);
  r.cycle=c.generation;r.handle=handle;r.address_type=c.address.type;std::memcpy(r.address_le,c.address.val,6);
  r.actual_length=r.length=32;r.bytes[0]=event;
  for(unsigned i=0;i<4;i++)r.bytes[i+1]=uint8_t(uint32_t(result)>>(8*i));
  r.bytes[5]=2;r.bytes[6]=c.stage;r.bytes[7]=c.owners;
  const auto relative=r.callback_us>=c.window_start&&r.callback_us-c.window_start<=UINT32_MAX?uint32_t(r.callback_us-c.window_start):UINT32_MAX;
  const auto elapsed=c.request_at&&r.callback_us>=c.request_at&&r.callback_us-c.request_at<=UINT32_MAX?uint32_t(r.callback_us-c.request_at):0;
  for(unsigned i=0;i<4;i++){r.bytes[8+i]=uint8_t(relative>>(8*i));r.bytes[12+i]=uint8_t(elapsed>>(8*i));}
  for(unsigned i=0;i<8;i++)r.bytes[16+i]=uint8_t(c.identity>>(8*i));
  for(unsigned i=0;i<2;i++){r.bytes[24+i]=uint8_t(c.interval>>(8*i));r.bytes[26+i]=uint8_t(c.latency>>(8*i));r.bytes[28+i]=uint8_t(c.supervision>>(8*i));}
  if(event>=15&&event<=20){r.bytes[30]=c.pa_phy;r.bytes[31]=c.pa_clock_accuracy;}
  inc(diagnostic.traces);transport_record(r);
}
bool live(uint16_t h,void* arg){return peer.handle==h&&peer.incarnation==reinterpret_cast<uintptr_t>(arg)&&!peer.dead&&!peer.terminating;}
int mtu_done(uint16_t h,const ble_gatt_error* e,uint16_t,void* arg){if(!live(h,arg))return 0;peer.pending=false;const auto mtu=ble_att_mtu(h);admission_trace(9,h,int((uint32_t(mtu)<<16)|uint16_t(e->status)));if((!e->status||e->status==BLE_HS_EALREADY)&&mtu>=226){peer.mtu=observed_mtu=mtu;peer.stage=2;}else peer.dead=true;return 0;}
int rc_read_done(uint16_t h,const ble_gatt_error* e,ble_gatt_attr* a,void* arg){
  if(!live(h,arg))return 1;
  admission_trace(25,h,int((uint32_t(a&&a->om?OS_MBUF_PKTLEN(a->om):0)<<16)|uint16_t(e->status)));
  if(e->status){peer.pending=false;peer.dead=true;return 1;}
  if(!a||!a->om||OS_MBUF_PKTLEN(a->om)!=RC_DYNAMIC_CONTROL_BYTES){peer.pending=false;peer.dead=true;return 1;}
  Incoming row{};row.at=micros();row.incarnation=peer.incarnation;row.length=RC_DYNAMIC_CONTROL_BYTES;
  if(os_mbuf_copydata(a->om,0,row.length,row.bytes)||!incoming.push(row)){peer.pending=false;peer.dead=true;return 1;}
  if(peer.stage==2){peer.value=a->handle;}
  peer.read_at=micros()+20000;inc(rx);return 1;
}
int rc_written(uint16_t h,const ble_gatt_error* e,ble_gatt_attr*,void* arg){
  if(!live(h,arg)){return 0;}
  peer.pending=false;if(e->status){peer.dead=true;return 0;}
  peer.ready=false;peer.stage=6;peer.read_at=micros()+20000;return 0;
}
int marker_done(uint16_t h,const ble_gatt_error* e,ble_gatt_attr* a,void* arg){
  if(!live(h,arg))return 1;
  admission_trace(4,h,e->status);
  static const uint8_t expected[5]={'R','2','G','1',1};uint8_t value[5]{};
  if(!e->status&&a&&a->handle&&a->om&&OS_MBUF_PKTLEN(a->om)==sizeof value&&
     !os_mbuf_copydata(a->om,0,sizeof value,value)&&!std::memcmp(value,expected,sizeof value)){
    peer.value=a->handle;peer.pending=false;peer.ready=true;peer.stage=5;peer.send_kind=R2_JOIN_REQUEST;return 1;
  }
  // This untrusted public marker locates only a handle. Unsupported peers
  // retain exact service/characteristic discovery; JOIN still authenticates.
  if(e->status){peer.pending=false;peer.stage=3;}return 0;
}
// NimBLE's discovery callback nonzero result ends that exact procedure.
// Stop at the first exact service/characteristic; do not spend another ATT
// round trip enumerating beyond an already-found endpoint. Handles remain
// per-connection, and only JOIN authentication establishes trust.
int service_done(uint16_t h,const ble_gatt_error* e,const ble_gatt_svc* s,void* arg){
  if(!live(h,arg))return 1;
  if(!e->status&&s&&s->start_handle&&s->end_handle>=s->start_handle){peer.start=s->start_handle;peer.end=s->end_handle;peer.pending=false;peer.stage=4;return 1;}
  if(e->status){peer.pending=false;peer.dead=true;}return 0;
}
int char_done(uint16_t h,const ble_gatt_error* e,const ble_gatt_chr* c,void* arg){
  if(!live(h,arg))return 1;
  const auto* expected=peer.relay?&rc_data_uuid.u:&data_uuid.u;
#ifdef PTOB_RC9
  if(peer.rc9)expected=&rc9_data_uuid.u;
#endif
  const bool properties=c&&(peer.relay?(c->properties&0x0a)==0x0a:(c->properties&0x04)!=0);
  if(!e->status&&c&&c->val_handle&&!ble_uuid_cmp(&c->uuid.u,expected)&&properties){
    peer.value=c->val_handle;peer.pending=false;
    if(peer.relay){peer.ready=false;peer.stage=2;}
    else{peer.ready=true;peer.stage=5;peer.send_kind=R2_JOIN_REQUEST;}
    return 1;}
  if(e->status){peer.pending=false;peer.dead=true;}return 0;
}
int access(uint16_t h,uint16_t,ble_gatt_access_ctxt* ctx,void*){
  // The serving T096 uses this public locator before replying to JOIN. It is
  // only endpoint discovery: no source, membership or key is exposed/granted.
  if(ctx->op==BLE_GATT_ACCESS_OP_READ_CHR){
    admission_trace(3,h,0);
    static const uint8_t marker[5]={'R','2','G','1',1};
    return os_mbuf_append(ctx->om,marker,sizeof marker)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
  }
  if(mode!=R2Mode::Client||closing||peer.handle!=h||peer.dead||peer.terminating||ctx->op!=BLE_GATT_ACCESS_OP_WRITE_CHR||OS_MBUF_PKTLEN(ctx->om)!=R2_GROUP_JOIN_BYTES)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  Incoming row{};row.at=micros();row.incarnation=peer.incarnation;if(os_mbuf_copydata(ctx->om,0,R2_GROUP_JOIN_BYTES,row.bytes))return BLE_ATT_ERR_UNLIKELY;
  if(!incoming.push(row))return BLE_ATT_ERR_INSUFFICIENT_RES;
  Report r{};r.kind=7;r.callback_us=row.at;r.handle=h;r.address_type=peer.address.type;std::memcpy(r.address_le,peer.address.val,6);r.actual_length=r.length=R2_GROUP_JOIN_BYTES;std::memcpy(r.bytes,row.bytes,R2_GROUP_JOIN_BYTES);transport_record(r);inc(rx);return 0;
}
#include "rc7_relay_native.inc"
const ble_gatt_chr_def chars[]={[]{ble_gatt_chr_def c{};c.uuid=&data_uuid.u;c.access_cb=access;c.flags=BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_READ;return c;}(),{}};
const ble_gatt_svc_def services[]={{.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service.u,.includes=nullptr,.characteristics=chars},
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
{.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&rc_service.u,.includes=nullptr,.characteristics=relay_chars},
#if defined(PTOB_RC9) && defined(PTOB_W12_LORA_RX)
{.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&rc9_service.u,.includes=nullptr,.characteristics=rc9_chars},
#endif
#endif
{}};
int gap(ble_gap_event* e,void* arg){
  const uint64_t now=micros();
  if(e->type==BLE_GAP_EVENT_CONNECT){
    if(reinterpret_cast<uintptr_t>(arg)!=peer.incarnation){inc(rejected);return 0;}
    admission_trace(1,e->connect.conn_handle,e->connect.status);
    if(e->connect.status){inc(diagnostic.connect_failed);inc(diagnostic.peer(peer.identity).failures);}
    // Cleanup can discover and adopt the exact ACL before NimBLE delivers
    // CONNECT. Its delayed success acknowledges that same terminating owner;
    // it must not resurrect GATT or replace the original drain deadline.
    if(!e->connect.status&&peer.handle!=None){
      ble_gap_conn_desc d{};
      if(peer.dead&&peer.terminating&&e->connect.conn_handle==peer.handle&&
         !ble_gap_conn_find(peer.handle,&d)&&d.role==BLE_GAP_ROLE_MASTER&&
         same_address(d.peer_id_addr,peer.address)){inc(diagnostic.connect_drain);return 0;}
      error(BLE_HS_EBUSY,R2Api::Connect,1);return 0;
    }
    const bool was_cancelled=connecting_cancelled;connecting_cancelled=false;peer.connecting=false;
    if(e->connect.status){if(!was_cancelled)failed_admission(now);peer.identity=0;retry_at=now+200000;return 0;}
    ble_gap_conn_desc d{};const bool match=peer.identity&&!ble_gap_conn_find(e->connect.conn_handle,&d)&&same_address(d.peer_id_addr,peer.address)&&peer.handle==None;
    if(peer.handle!=None){error(BLE_HS_EBUSY,R2Api::Connect,1);return 0;}
    peer.handle=e->connect.conn_handle;peer.stage=1;peer.gatt_until=full_listening()&&!peer.relay?peer.exchange.until:now+2000000;
    if(match)observed_descriptor(d,now);

    if(!match||mode!=R2Mode::Client||closing||!peer.exchange.until||now>=peer.exchange.until||now>=peer.attempt_until||(!full_listening()&&now-G1Window::start(now,window_phase)>=400000))peer.dead=true;
    else inc(diagnostic.connect_ok);
  }else if(e->type==BLE_GAP_EVENT_DISCONNECT){
    const auto& d=e->disconnect.conn;
    const bool owned=reinterpret_cast<uintptr_t>(arg)==peer.incarnation&&peer.identity&&d.role==BLE_GAP_ROLE_MASTER&&same_address(d.peer_id_addr,peer.address);
    if(owned&&(d.conn_handle==peer.handle||(peer.connecting&&peer.handle==None))){
      if(!connecting_cancelled)failed_admission(now);
      admission_trace(2,d.conn_handle,e->disconnect.reason);
      if(peer.terminate_at&&now>=peer.terminate_at&&now-peer.terminate_at<=UINT32_MAX&&now-peer.terminate_at>diagnostic.disconnect_max_us)diagnostic.disconnect_max_us=uint32_t(now-peer.terminate_at);
      if(peer.connecting)inc(pending_disconnects);
      inc(peer_losses);
      last_disconnect_reason=e->disconnect.reason;last_disconnect_at=now;
      last_gatt_stage=uint8_t(peer.stage);peer={};connecting_cancelled=false;retry_at=now+200000;
    }
  }
  else if(e->type==BLE_GAP_EVENT_EXT_DISC){
    const auto& d=e->ext_disc;inc(adv_callbacks);last_adv_at=now;
    if(d.data_status!=BLE_HCI_ADV_DATA_STATUS_COMPLETE){inc(adv_incomplete);return 0;}
    if(d.length_data>191){inc(adv_long);return 0;}
    if(d.addr.type!=BLE_ADDR_RANDOM&&!(d.addr.type==BLE_ADDR_PUBLIC&&(d.sid==12||d.sid==13))){inc(adv_nonrandom);return 0;}
    Discovery row{};row.rssi=d.rssi;row.tx_power=d.tx_power;row.at=now;row.address=d.addr;row.interval=d.periodic_adv_itvl;row.sid=d.sid;row.props=d.props;row.legacy_event_type=d.legacy_event_type;row.primary_phy=d.prim_phy;row.secondary_phy=d.sec_phy;row.length=uint8_t(d.length_data);if(row.length)std::memcpy(row.bytes,d.data,row.length);discoveries.push(row);
  }else if(e->type==BLE_GAP_EVENT_DISC_COMPLETE)scan=false;
  else if(e->type==BLE_GAP_EVENT_PERIODIC_SYNC){
    if(reinterpret_cast<uintptr_t>(arg)!=sync_capture_epoch){error(BLE_HS_EBUSY,R2Api::SyncCreate);return 0;}
    bool source_owner=anchor_valid(now)&&parent_scope();
#ifdef PTOB_RC9
    const auto admitted_rc9=rc9_client.view(now);
    source_owner|=admitted_rc9.bound&&admitted_rc9.server==sync_identity&&admitted_rc9.server_boot==joined_boot;
#endif
    const bool capture_owner=sync_pending&&
      !sync_cancelled&&!sync_cleanup_requested&&!closing&&!fault&&
      (full_listening()||sync_owner_window_start==window_start)&&client_window_active(now,window_phase)&&
      source_owner;
    const auto completion_context=sync_context();
    sync_phy=e->periodic_sync.adv_phy;sync_clock_accuracy=e->periodic_sync.adv_clk_accuracy;
    sync_trace(17,e->periodic_sync.sync_handle,e->periodic_sync.status);
    sync_pending=sync_selected=false;sync_cancelled=false;
    if(!e->periodic_sync.status){sync_handle=e->periodic_sync.sync_handle;synced=true;sync_terminating=false;if(e->periodic_sync.sid!=sync_selected_sid||e->periodic_sync.per_adv_ival!=50||!same_address(e->periodic_sync.adv_addr,sync_address))fault=true;if(capture_owner&&!fault){
#ifdef V12_R2_RX_SHADOW
      if(!periodic_rx::adopt(sync_handle,sync_capture_epoch))fault=true;
#endif
    }else periodic_rx::scope(None);}
    else {periodic_rx::scope(None);sync_identity=0;retry_at=now+200000;}
    sync_quiet_return(completion_context);
  }else if(e->type==BLE_GAP_EVENT_PERIODIC_SYNC_LOST){sync_trace(20,e->periodic_sync_lost.sync_handle,0);if(reinterpret_cast<uintptr_t>(arg)==sync_capture_epoch&&e->periodic_sync_lost.sync_handle==sync_handle){const auto completion_context=sync_context();sync_quiet_return(completion_context);sync_handle=None;synced=false;sync_terminating=false;sync_identity=0;periodic_rx::scope(None);}}
  else if(e->type==BLE_GAP_EVENT_PERIODIC_REPORT){const auto& p=e->periodic_report;
    if(!closing&&!sync_terminating&&synced&&p.sync_handle==sync_handle&&reinterpret_cast<uintptr_t>(arg)==sync_capture_epoch){inc(pa_reports);Report r{};r.kind=6;r.callback_us=now;r.handle=p.sync_handle;r.sid=sync_selected_sid;r.address_type=sync_address.type;std::memcpy(r.address_le,sync_address.val,6);r.rssi=p.rssi;r.status=p.data_status;r.actual_length=p.data_length;r.length=uint8_t(p.data_length>MaxData?MaxData:p.data_length);if(fault)r.refusal|=4;if(r.length)std::memcpy(r.bytes,p.data,r.length);
      if(p.data_status!=BLE_HCI_PERIODIC_DATA_STATUS_COMPLETE||(p.data_length!=158&&p.data_length!=190)||
         (mode==R2Mode::Client&&!full_listening()&&(!client_window_active(now,window_phase)||now-window_start>=G1Window::Nominal)))r.refusal=2;
#ifdef V12_R2_RX_SHADOW
      uint64_t tick=0;clock_reference_read(&tick);
#ifdef PTOB_N30
      n30_probe::packet_rssi=p.rssi;
#endif
      if(!r.refusal)periodic_rx::match(p.sync_handle,p.data,p.data_length,tick,r.rx_stamp);
#endif
      transport_record(r);if(!r.refusal){Periodic row{};row.at=now;row.identity=sync_identity;row.capture=r.rx_stamp;row.length=uint8_t(p.data_length);std::memcpy(row.bytes,p.data,p.data_length);
#ifdef PTOB_N30
       row.signal_epoch=sync_capture_epoch;row.rssi=p.rssi;
#endif
       periodic.push(row);}else inc(p1_bad);
    }
  }
  return 0;
}
uint64_t discovery_identity(const Discovery& d,G1Reason* reason=nullptr){
  auto refuse=[reason](G1Reason r){if(reason)*reason=r;return UINT64_C(0);};
  if(!(d.props&1)||exclude_direct_gm)return refuse(G1Reason::Flags);
  for(unsigned i=0;i<d.length;){const unsigned n=d.bytes[i];if(!n||i+n>=d.length)return refuse(G1Reason::Malformed);
    const auto* p=d.bytes+i+2;
    if(n==24&&d.bytes[i+1]==0xff&&p[0]=='G'&&p[1]=='1'){
      if((p[2]&63)!=1||(p[2]&0x40))return refuse(G1Reason::Flags);
      if(r2_group_get(p+3,8)!=provision.group.id)return refuse(G1Reason::Group);
      if(r2_group_get(p+11,4)!=provision.group.epoch)return refuse(G1Reason::Epoch);
      const auto id=r2_group_get(p+15,8);
      if(!id||id==provision.identity)return refuse(G1Reason::Identity);
      if(!same_address(d.address,canonical(id)))return refuse(G1Reason::Address);
      return id;}
    i+=n+1;
  }return 0;
}
// Diagnostic observations grant no eligibility. Read only a complete bounded
// manufacturer field; retain the existing authentication/address filters.
void observe_group(const Discovery& d){
  inc(discovery_checked);
  for(unsigned i=0;i<d.length;){const unsigned n=d.bytes[i];if(!n||i+n>=d.length)return;
    const auto* p=d.bytes+i+2;
    if(n==24&&d.bytes[i+1]==0xff&&p[0]=='G'&&p[1]=='1'){
      inc(group_records);last_group_id=r2_group_get(p+3,8);last_key_epoch=uint32_t(r2_group_get(p+11,4));last_group_at=d.at;return;
    }
    i+=n+1;
  }
}
bool cooled(uint64_t id,uint64_t now){for(const auto& r:recent)if(r.identity==id&&now<r.until)return true;return false;}
void cooldown(uint64_t id,uint64_t now,uint64_t duration){unsigned slot=0;for(unsigned i=0;i<8;i++){if(recent[i].identity==id){slot=i;break;}if(recent[i].until<recent[slot].until)slot=i;}recent[slot]={id,now+duration};}
// Only the exact normalized request below may contain the controller's
// Connection Parameters rejection (HCI0x3b). NimBLE resets its unaccepted
// master procedure before returning this error. This is a failed admission,
// not evidence of an accepted ACL, timing authority or a diagnosed controller.
// Other controller errors and malformed requests retain terminal fault handling.
bool connect_parameter_refusal(int rc,const ble_gap_conn_params& p){
  return rc==BLE_HS_HCI_ERR(0x3b)&&p.scan_itvl==32&&
    (p.scan_window==8||p.scan_window==32)&&p.itvl_min==6&&p.itvl_max==6&&
    !p.latency&&(p.supervision_timeout==10||p.supervision_timeout==50)&&
    !p.min_ce_len&&!p.max_ce_len;
}
void connect(const Discovery& d,uint64_t id,uint64_t now,bool relay=false,bool is_rc9=false){
  if((!is_rc9&&!parent_selection.retry_allowed(id,relay,now))||!admission_policy.allowed(now,full_listening()&&!relay))return;
  relay_poll(now,true,true);if(!relay_acl_available())return;stop_scan();if(scan||!timely())return;
  if(nonce==UINT64_MAX){fault=true;return;}++nonce;
  if(incarnation==UINT32_MAX){fault=true;return;}
  const bool rescan=false; // Poor delivery never establishes slot contention.
  if(!is_rc9)parent_selection.attempted(id,relay,now);
  peer={};peer.identity=id;peer.address=d.address;peer.incarnation=++incarnation;peer.request_at=now;peer.relay=relay;peer.rescan=rescan;
  peer.rc9=is_rc9;
  const auto selection=parent_selection.selection_view(now);
  peer.parent_reason=parent_selection.pinned()?G1ParentReason::Manual:exclude_direct_gm?G1ParentReason::RelayOnly:
    gm_discovery.active(now)&&id==anchor.root&&!relay?G1ParentReason::ReturnGM:
    selection.reason==G1ParentSelection::Decision::RecoverGM?G1ParentReason::RecoverGM:
    selection.reason==G1ParentSelection::Decision::ReturnGM?G1ParentReason::ReturnGM:
    selection.reason==G1ParentSelection::Decision::HoldGM?G1ParentReason::HoldGM:
    selection.reason==G1ParentSelection::Decision::LockTrial?G1ParentReason::LockTrial:
    parent_selection.quality().poor?G1ParentReason::LinkPoor:
    !joined_peer?G1ParentReason::Startup:!following(now)?G1ParentReason::ClockUnavailable:G1ParentReason::BestCandidate;
  if(relay)peer.exchange.until=now+2000000;
  else if(r2_group_join_begin(&peer.exchange,false,provision.identity,id,boot_id,nonce,now)){fault=true;return;}
  peer.attempt_until=now+(full_listening()&&!relay?4000000:2000000);cooldown(id,now,5000000);inc(join_attempts);if(anchor_valid(now))inc(refresh_attempts);
  ble_gap_conn_params p{};p.itvl_min=p.itvl_max=6;p.supervision_timeout=full_listening()&&!relay?50:10;
  // BLE100 direct control tolerates500ms supervision; BLE50 and RC7 retain
  // 100ms so their quiet/owner arbitration stays unchanged. Precision keeps
  // its separate capture/freshness limits and drains before control work.
  p.scan_itvl=32;p.scan_window=precision_reserved(micros())?8:32;
  // With a current precision stream, request5ms/20ms initiation windows,
  // leaving gaps for periodic reception. Cold acquisition keeps20ms/20ms.
  // Direct BLE100 initiation has4s within the original5s exchange;
  // a scanner window is not a controller priority or RF delivery guarantee.
  // Actual cancellation/drain still has its original2s bound. Disable only
  // the duplicate SDK host timer; HCI create itself has no duration parameter.
  // Require the real completion callback, never just the cancel command ACK.
  // Install pending/incarnation before the yielding public API. A completion
  // delivered before return must never be overwritten back to pending.
  admission_policy.started(now);
  peer.connecting=true;connecting_cancelled=false;const auto generation=peer.incarnation;
  inc(diagnostic.peer(id).attempts);const auto context=link_context(now);admission_trace(11,None,0,&context);
  const int rc=ble_gap_connect(BLE_OWN_ADDR_RANDOM,&d.address,BLE_HS_FOREVER,&p,gap,callback_arg());
  admission_trace(12,None,rc,&context);
  if(rc&&peer.incarnation==generation){
    const bool refused=connect_parameter_refusal(rc,p)&&peer.connecting&&
      peer.handle==None&&!peer.dead&&!peer.terminating&&!connecting_cancelled;
    if(refused){note(rc,R2Api::Connect);inc(setup_retries);inc(diagnostic.connect_failed);inc(diagnostic.peer(id).failures);}
    failed_admission(now);peer.connecting=false;peer.identity=0;retry_at=now+200000;
    if(!refused&&!transient(rc,R2Api::Connect)&&rc!=BLE_HS_EALREADY)error(rc,R2Api::Connect);
  }
}
void gatt_poll(uint64_t now){
  if(peer.handle==None)return;
  if(peer.dead||peer.terminating||now>=peer.exchange.until||(!peer.ready&&now>=peer.gatt_until)||(peer.close_at&&now>=peer.close_at)){if(!peer.terminating)failed_admission(now);terminate_peer(now);return;}
  if(!peer.ready&&!peer.pending){int rc=0;const auto h=peer.handle;const auto generation=peer.incarnation;void* arg=callback_arg();R2Api site=R2Api::None;peer.pending=true;
    // ATT performs ordinary LL fragmentation. This transient admission needs
    // its negotiated ATT MTU, not an extra LL data-length procedure. The latter
    // raced a vanished ACL in retained boot f4c9319f7acf3cd7 (HCI 0x0c followed
    // by supervision DISCONNECT) and unnecessarily faulted the whole service.
    if(peer.stage==1){site=R2Api::Mtu;rc=ble_gattc_exchange_mtu(h,mtu_done,arg);if(rc==BLE_HS_EALREADY&&live(h,arg)&&ble_att_mtu(h)>=226){peer.pending=false;peer.mtu=observed_mtu=ble_att_mtu(h);peer.stage=2;rc=0;}}
    else if(peer.stage==2){site=R2Api::Service;
      // Pinned S3 ReadByType uses a19-byte flat buffer. Discover the RC
      // endpoint, then normal READ carries its exact212-byte capability.
      if(peer.relay){const auto* selected=&rc_service.u;
#ifdef PTOB_RC9
       if(peer.rc9)selected=&rc9_service.u;
#endif
       rc=peer.value?ble_gattc_read(h,peer.value,rc_read_done,arg):ble_gattc_disc_svc_by_uuid(h,selected,service_done,arg);
      }
      else rc=ble_gattc_read_by_uuid(h,1,UINT16_MAX,&data_uuid.u,marker_done,arg);}
    else if(peer.relay&&peer.stage==6){if(now<peer.read_at){peer.pending=false;return;}site=R2Api::Service;rc=ble_gattc_read(h,peer.value,rc_read_done,arg);}
    else if(peer.stage==3){site=R2Api::Service;rc=ble_gattc_disc_svc_by_uuid(h,&service.u,service_done,arg);}
    else if(peer.stage==4){site=R2Api::Characteristics;rc=ble_gattc_disc_all_chrs(h,peer.start,peer.end,char_done,arg);}
    else rc=BLE_HS_EINVAL;
    if(rc){note(rc,site);if(peer.handle==h&&peer.incarnation==generation){peer.pending=false;if(rc==BLE_HS_ENOTCONN){inc(peer_losses);peer.dead=true;}else if(!transient(rc,site)){peer.dead=true;error(rc,site);}}}
  }
}
void send_join(uint64_t now){
  if(!peer.ready||!peer.send_kind||peer.dead||peer.terminating||now>=peer.exchange.until||!timely())return;
  if(sequence==UINT64_MAX){fault=true;return;}
  const uint8_t kind=peer.send_kind;r2_join_record r{};r.kind=kind;r.sender=provision.identity;r.receiver=peer.identity;r.sender_boot=boot_id;r.receiver_boot=peer.exchange.peer_boot;r.client_nonce=peer.exchange.nonce;r.server_nonce=peer.exchange.remote_nonce;r.sequence=sequence+1;r.created_us=now;r.echo_us=kind==R2_JOIN_PROOF?peer.exchange.challenge_at:0;std::memcpy(r.name,provision.name,25);
  uint8_t wire[R2_GROUP_JOIN_BYTES];if(r2_group_join_encode(wire,&r,&provision.group)){fault=true;return;}
  const auto h=peer.handle;const auto generation=peer.incarnation;const auto context=link_context(now);const int rc=ble_gattc_write_no_rsp_flat(h,peer.value,wire,R2_GROUP_JOIN_BYTES);
  admission_trace(5,h,rc,&context);
  if(!rc&&peer.handle==h&&peer.incarnation==generation&&!peer.dead){sequence=r.sequence;if(kind==R2_JOIN_REQUEST)peer.exchange.request_at=now;else peer.exchange.proof_at=now;peer.send_kind=0;inc(tx);}
  else if(rc){note(rc,R2Api::ControlWrite);if(rc==BLE_HS_ENOTCONN){peer.dead=true;inc(peer_losses);}else if(!transient(rc,R2Api::ControlWrite))error(rc,R2Api::ControlWrite);}
}
void changed_scope(uint64_t now,G1Reason reason=G1Reason::PeriodicScope,bool preserve_clock=false,G1ParentReason parent_reason=G1ParentReason::SourceChange);
void forget_leader(uint64_t now,bool keep_admission=false);
void cancel_connect(uint64_t now);
#include "rc9_client_native.inc"
void parent_record(G1ParentReason reason,uint64_t now){
  G1ParentScope scope{};
  if(joined_peer&&(member||anchor.source||rc_bound())){
    scope.peer=joined_peer;scope.boot=joined_boot;scope.root=anchor.root;scope.root_boot=anchor.boot;
    scope.source=anchor.source_epoch;scope.train=anchor.publication;scope.relay=rc_bound();
    scope.depth=scope.relay?rc_client.receiver.path.depth:0;
  }
  const auto selection=parent_selection.selection_view(now);
  portENTER_CRITICAL(&mux);
  const auto sequence=parent_journal.observe(scope,now,reason,uint8_t(selection.reason),selection.current_score,selection.best_score,parent_selection.quality().poor);
  portEXIT_CRITICAL(&mux);
  if(sequence){Report r{};r.kind=21;r.callback_us=now;r.actual_length=r.length=4;
    for(unsigned i=0;i<4;i++)r.bytes[i]=uint8_t(sequence>>(8*i));
    transport_record(r);}
}
void scope_trace(const Anchor& old,const Anchor& next,G1Reason reason,uint64_t now){
  Report r{};r.kind=10;r.callback_us=now;r.actual_length=r.length=58;r.bytes[0]=1;r.bytes[1]=uint8_t(reason);
  const uint64_t values[]={old.root,old.boot,old.source_epoch,old.publication,next.root,next.boot,next.source_epoch,next.publication,next.until};
  const unsigned offsets[]={2,10,18,22,26,34,42,46,50},widths[]={8,8,4,4,8,8,4,4,8};
  for(unsigned i=0;i<9;i++)for(unsigned j=0;j<widths[i];j++)r.bytes[offsets[i]+j]=uint8_t(values[i]>>(8*j));
  transport_record(r);
}
void receive_rc_control(const Incoming& row,uint64_t now){
  peer.pending=false;
#ifdef PTOB_RC9
  if(peer.rc9){rc9_receive_control(row,now);return;}
#endif
  rc7_control c{};
  if(row.length!=212||rc7_control_decode(&c,row.bytes,row.length,&provision.group)||
     !rc_client.path_usable(c.path,provision.identity)){inc(rejected);peer.dead=true;return;}
  inc(auth);
  if(peer.stage==2&&c.type==RC7_CAPABILITY&&c.sender==peer.identity){
    ++rc_client.capabilities;
    const bool rescan=peer.rescan;
    if(!rc_client.make_request(c,provision.identity,boot_id,nonce,now,rescan)){inc(rejected);peer.dead=true;return;}
    if(rescan)rc_client.retry_at=now+10000000;
    peer.stage=5;peer.ready=true;peer.send_kind=RC7_REQUEST;
  }else if(peer.stage==6&&rc_client.response_matches(c,now)){
    ++rc_client.responses;
    if(c.ready){
      uint64_t tick=0;
#ifdef V12_R2_RX_SHADOW
      clock_reference_read(&tick);
#endif
      const bool same=rc_bound()&&rc7_path_same(&rc_client.receiver.path,&c.path);
      if(!same)changed_scope(now);
      if(!rc_client.bind(c,now,tick)){inc(rejected);peer.dead=true;return;}
      direct_session.clear();
      if(!same){
        anchor={true,c.path.node[0].id,c.path.node[0].boot,0,now,0,c.path.source_epoch,c.path.node[0].train,0};
        corrected_clock.withdraw();
      }
      // Full exchange widens the conservative replay interval. Admission never
      // supplies UTC, corrected delivery, or a new source envelope.
      const auto& a=rc_client.receiver.clock.admission;
      const auto uncertainty=uint32_t((a.root_hi_ns-a.root_lo_ns+1999)/2000);
      uint64_t mapped=0;
      const bool live_map=same&&rc_client.utc_known&&corrected_clock.deliver(tick,now,mapped);
      if(!same||live_map){anchor.at=now;anchor.uncertainty=uncertainty;if(live_map)anchor.utc=mapped;}
      else if(uncertainty>anchor.uncertainty)anchor.uncertainty=uncertainty;
      // A new lease cannot rebase old UTC when its corrected map expired.
      anchor.context_until=now+c.lease_us;anchor.drift_ppm=301;
      corrected_clock.scope(anchor);
      joined_peer=c.sender;joined_boot=c.sender_boot;member=true;sync_address=peer.address;
      parent_selection.bind(c.sender,c.sender_boot,c.path.node[c.path.depth].epoch,now);
      parent_selection.authenticated(c.sender,true,c.sender_boot,c.path.node[0].id,c.path.node[0].boot,c.path.source_epoch,now);
      peer.selection_accepted=true;
      parent_record(peer.parent_reason,now);
      inc(join_accepts);peer.close_at=now+2000;
    }else{
      ++rc_client.pending;if(rc_client.request.operation)changed_scope(now);
      // An authenticated pending demand is successful negotiation, not a
      // failed admission. Keep its locator through bounded timing acquisition
      // instead of penalizing it at the planned transient-ACL disconnect.
      peer.selection_reported=true;parent_selection.pending(peer.identity,now);
      cooldown(peer.identity,now,1000000);rc_client.renew_at=now+1000000;peer.close_at=now+2000;
    }
  }else if(peer.stage==6&&c.type==RC7_CAPABILITY){peer.read_at=now+20000;}
  else{inc(rejected);peer.dead=true;}
}
void send_rc_request(uint64_t now){
  if(!peer.relay||peer.stage!=5||!peer.send_kind||peer.pending||peer.dead||peer.terminating||!timely())return;
#ifdef PTOB_RC9
  if(peer.rc9){rc9_send_request(now);return;}
#endif
  uint8_t wire[212];if(rc7_control_encode(wire,&rc_client.request,&provision.group)){peer.dead=true;inc(rejected);return;}
  peer.pending=true;peer.send_kind=0;rc_client.request_at=now;
  const auto h=peer.handle;const auto generation=peer.incarnation;
  const int rc=ble_gattc_write_flat(h,peer.value,wire,sizeof wire,rc_written,callback_arg());
  admission_trace(5,h,rc);
  if(!rc){inc(tx);++rc_client.requests;}else if(peer.handle==h&&peer.incarnation==generation){peer.pending=false;peer.dead=true;note(rc,R2Api::ControlWrite);}
}
void receive_join(uint64_t now){Incoming row{};for(unsigned i=0;i<8&&incoming.pop(row);i++){
  r2_join_record r{};if(row.incarnation!=peer.incarnation||peer.handle==None||peer.dead||peer.terminating||now<row.at||now-row.at>=(full_listening()&&!peer.relay?1000000:50000)||now>=peer.exchange.until){inc(rejected);diagnostic.reason(G1Reason::JoinStale,now);continue;}
  if(peer.relay){receive_rc_control(row,now);continue;}
  if(exclude_direct_gm||row.length!=R2_GROUP_JOIN_BYTES){inc(rejected);peer.dead=true;continue;}
  if(r2_group_join_decode(&r,row.bytes,R2_GROUP_JOIN_BYTES,&provision.group)){inc(rejected);diagnostic.reason(G1Reason::JoinDecode,now);continue;}
  if(r.kind==R2_JOIN_BUSY){if(r.sender==peer.identity&&r.receiver==provision.identity&&r.receiver_boot==boot_id&&r.client_nonce==peer.exchange.nonce){inc(join_busy);peer.dead=true;diagnostic.reason(G1Reason::JoinBusy,now);}else{inc(rejected);diagnostic.reason(G1Reason::JoinExchange,now);}continue;}
  if(r2_group_join_receive(&peer.exchange,&r,row.at)){inc(rejected);diagnostic.reason(G1Reason::JoinExchange,now);continue;}inc(auth);
  if(r.kind==R2_JOIN_CHALLENGE){if((anchor.source&&joined_peer==r.sender&&joined_boot!=r.sender_boot)||
      (coarse_hold.saved.source&&coarse_hold.saved.root==r.sender&&coarse_hold.saved.boot!=r.sender_boot)){forget_leader(now,true);}peer.send_kind=R2_JOIN_PROOF;}
  else if(r.kind==R2_JOIN_ACCEPT){const auto previous_peer=joined_peer,previous_boot=joined_boot;const bool previous_member=member;
    inc(join_accepts);inc(diagnostic.peer(r.sender).accepts);r2_group_join_remember(display,&peer.exchange,now);peer.close_at=now+2000;
    const auto reply=g1_admission_reply(r);r2_join_time time{};
    const bool timed=r2_group_join_time(&time,&r,peer.exchange.proof_at,row.at,provision.map_bound_us);
    admission_policy.reply(timed?reply:reply==G1AdmissionReply::Direct?G1AdmissionReply::Unusable:reply);
    // An authenticated zero-root NO_SOURCE reply is ordinary source loss,
    // not a foreign-root follower and not a reason to discard the old fit/PA.
    // All unusable replies preserve the current binding and original deadlines.
    if(!timed){
      peer.selection_accepted=true;admission_failed(now);
      cooldown(r.sender,now,5000000);
      if(reply==G1AdmissionReply::ForeignRoot)parent_selection.cooldown(r.sender,now,60000000);
      if(!previous_member&&!anchor.source&&!rc_bound()){member=true;joined_peer=r.sender;joined_boot=r.sender_boot;}
      diagnostic.reason(G1Reason::JoinTime,now);continue;
    }
    // A newly authenticated direct leader retires all old estimator and
    // selection state before its coarse bootstrap is installed.
    if(r.root==r.sender&&r.root_boot==r.sender_boot&&
       (anchor.root!=r.root||anchor.boot!=r.root_boot||anchor.source_epoch!=r.source_epoch||anchor.publication!=r.publication))forget_leader(now,true);
    member=true;joined_peer=r.sender;joined_boot=r.sender_boot;cooldown(r.sender,now,5000000);
    if(timed){
      if(r.root!=r.sender||r.root_boot!=r.sender_boot){
        peer.selection_accepted=true;admission_failed(now);
        if(reply==G1AdmissionReply::ForeignRoot)parent_selection.cooldown(r.sender,now,60000000);
#ifdef PTOB_RC9
        // A root locator is not replacement timing. Keep the fallback lease,
        // sample and PA owner until a direct normal root actually admits us.
        if(rc9_client.view(now).bound){
          joined_peer=previous_peer;joined_boot=previous_boot;member=previous_member;
          admission_root=r.root;admission_until=time.until;
          parent_selection.reobserve_gm(r.root,now);
          diagnostic.reason(G1Reason::RootHint,now);continue;
        }
#endif
        // A cohort peer may describe a different serving root. This direct
        // client must authenticate that root itself before showing FOLLOW or
        // selecting its direct periodic train. Retain only a short locator
        // preference bounded by this already-signed source permission.
        // A locator selects acquisition, never the clock. Preserve the
        // already valid old-origin HOLD through a new-root locator, as the
        // T096 presentation anchor survives root transitions. Its original
        // replay budget and permission stay unchanged; only direct new-root
        // authentication may replace scope and retire the old estimator.
        // A different locator cannot replace the authenticated direct root
        // or retire its still-valid replay/PA scope during control recovery.
        // Keep every old deadline; the hint supplies no clock or permission.
        const auto& path=rc_client.receiver.path;
        const bool old_direct=!rc_bound()&&previous_peer==anchor.root&&previous_boot==anchor.boot;
        const bool old_relay=rc_bound()&&now<rc_client.lease_until&&path.depth>=1&&path.depth<=2&&
          path.node[path.depth].id==previous_peer&&path.node[path.depth].boot==previous_boot&&
          path.node[0].id==anchor.root&&path.node[0].boot==anchor.boot&&path.source_epoch==anchor.source_epoch;
        if(previous_member&&anchor.valid(now)&&(old_direct||old_relay)){
          if(!following(now)&&!corrected_clock.covers(now,now)){
            forget_leader(now,true);member=true;
          }else{joined_peer=previous_peer;joined_boot=previous_boot;member=previous_member;}
          admission_root=r.root;admission_until=time.until;
          parent_selection.reobserve_gm(r.root,now);
          diagnostic.reason(G1Reason::RootHint,now);continue;
        }
        auto historical=coarse_hold.saved.valid(now)?coarse_hold:G1CoarseHold{};
        if(anchor.valid(now))historical.retain(anchor);
        forget_leader(now,true);coarse_hold=historical;member=true;admission_root=r.root;admission_until=time.until;
        parent_selection.reobserve_gm(r.root,now);
        Report hint{};hint.kind=9;hint.callback_us=now;hint.actual_length=hint.length=16;
        for(unsigned j=0;j<8;j++){hint.bytes[j]=uint8_t(admission_root>>(8*j));hint.bytes[8+j]=uint8_t(admission_until>>(8*j));}
        transport_record(hint);parent_record(G1ParentReason::RootHint,now);continue;
      }
      // A direct-root JOIN changes the transport owner even if root-only
      // scope identifiers match. Retire the RC7 path and clock state first.
      if(rc_bound()){changed_scope(now);member=true;}
#ifdef PTOB_RC9
      if(rc9_client.view(now).bound){rc9_client.fence();cancel_sync(now);}
#endif
      admission_root=admission_until=0;
      parent_selection.bind(r.sender,r.sender_boot,r.source_epoch,now);
      parent_selection.authenticated(r.sender,false,r.sender_boot,r.root,r.root_boot,r.source_epoch,now);
      peer.selection_accepted=true;
      Anchor next{true,r.root,r.root_boot,time.utc_us,row.at,time.until,r.source_epoch,r.publication,time.uncertainty_us};
      if(full_listening())direct_session.begin(next,row.at,admission_random());
      else direct_session.clear();
      // Only this newly authenticated direct-root time bootstrap shortens the
      // successful-peer cooldown. Failures, no-source membership and indirect
      // root hints retain five seconds. Quiet ownership still closes normally;
      // no discovery/JOIN packet extends a previous corrected-model deadline.
      cooldown(r.sender,now,500000);
      phase_learning.accepted(next,now);
      if(!anchor.same(next)){scope_trace(anchor,next,G1Reason::None,now);last_origin_ns=relay_train=0;window_corrected=false;cancel_sync(now);}
      anchor=next;corrected_clock.scope(anchor);coarse_hold.clear();discipline_scope(now);
      parent_record(peer.parent_reason,now);
    }
    // No-source membership creates neither a coarse clock nor precision.
  }
}}
bool fresh_envelope(uint64_t created,uint64_t expires,int64_t offset,uint64_t now,bool immutable_origin=false){
  const auto utc=coarse_ns(now);
  // GR3 signs an immutable source permission lasting up to1650ms. Its
  // creation is not the current RF event's timestamp. Match the frozen
  // upstream semantics while retaining the signed expiry and guard; actual
  // physical observations/corrections still have their separate250ms limit.
  const uint64_t horizon=immutable_origin?1650000000:1500000000;
  return anchor_valid(now)&&utc>=0&&created<=INT64_MAX&&expires<=INT64_MAX&&offset>=0&&
    expires>created&&expires-created<=horizon&&
    offset<=INT64_MAX-int64_t(expires)&&utc>=int64_t(created)+offset&&
    (immutable_origin||utc-(int64_t(created)+offset)<250000000)&&
    int64_t(expires)+offset>utc+int64_t(anchor.guard(now)*1000);
}
void changed_scope(uint64_t now,G1Reason reason,bool preserve_clock,G1ParentReason parent_reason){if(parent_reason==G1ParentReason::ClockStep)retained_estimate.clear();relay_fence();if(anchor.source){inc(diagnostic.scope_changes);scope_trace(anchor,{},reason,now);}
#ifdef PTOB_RC9
 if(!peer.rc9)rc9_client.fence();
#endif
  parent_selection.unavailable(now);
  if(reason==G1Reason::Permission){if(usable_utc()&&anchor.valid(now))coarse_hold.retain(anchor);}
  else coarse_hold.clear();
  diagnostic.reason(reason,now);anchor={};direct_session.clear();rc_client.fence();member=false;admission_root=admission_until=0;last_origin_ns=relay_train=0;window_corrected=false;
  if(reason==G1Reason::Permission||preserve_clock)corrected_clock.hold();else corrected_clock.withdraw();
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
  fine_pending={};fine_waiting=false;
#endif
  corrected_phase_aligned=false;cancel_sync(now);discipline_scope(now);
  parent_record(reason==G1Reason::Permission?G1ParentReason::ReplayExpired:reason==G1Reason::RootHint?G1ParentReason::RootHint:parent_reason,now);}
void forget_leader(uint64_t now,bool keep_admission){
  // Revoke authority first. cancel_sync retains the exact SDK owner and its
  // original drain deadline; clearing a cache never fabricates radio OFF.
  changed_scope(now);corrected_clock.clear();coarse_hold.clear();phase_learning={};
  if(!keep_admission){peer.selection_reported=true;peer.dead=true;cancel_connect(now);terminate_peer(now);}
  Incoming old_reply{};while(incoming.pop(old_reply)){};
  Discovery old_locator{};while(discoveries.pop(old_locator)){};
  parent_selection.leader_lost();parent_failure={};gm_discovery={};
  admission_policy.leader_lost(now);joined_peer=joined_boot=0;
  retry_at=scan_retry_at=scan_retry_until=0;
  for(unsigned i=0;i<8;i++){locator_ids[i]=0;locators[i]={};recent[i]={};}
#ifdef PTOB_RC9
  for(auto& locator:rc9_locators)locator={};
  rc9_retry_at=0;
#endif
  Periodic discarded{};while(periodic.pop(discarded)){}
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
  fine_pending={};fine_waiting=false;
#endif
}
void renew_permission(uint64_t created,uint64_t expires,int64_t offset,uint64_t now,bool immutable_origin=false){
  if(created<last_origin_ns||!fresh_envelope(created,expires,offset,now,immutable_origin))return;
  const uint64_t left=uint64_t(int64_t(expires)+offset-coarse_ns(now))/1000-anchor.guard(now);
  if(left<=(immutable_origin?1650000:1500000)){anchor.until=now+left;last_origin_ns=created;}
}
unsigned periodic_poll(uint64_t now,unsigned budget=8){Periodic row{};unsigned processed=0;
  // A waiting fine association retains its original search interval. Scope
  // invalidation clears the separate pending row before a new leader is used.
  for(;processed<budget;++processed){
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK)
  if(fine_waiting){row=fine_pending;fine_waiting=false;}else
#endif
  if(!periodic.pop(row))break;
  now=micros();
#ifdef PTOB_RC9
  if(rc9_bound()){
   const auto current=rc9_client.view(now);
   if(row.identity==current.server&&row.identity==sync_identity&&now>=row.at&&now-row.at<50000&&client_window_active(row.at,window_phase)){
    rc9::Time packet;
    if(rc9::decode_time(packet,row.bytes,row.length,provision.group)){
     if(rc9_client.receive(packet,now)){
#ifdef PTOB_N30
      signal_received(row,now,sync_capture_epoch);
#endif
      inc(auth);
     }
    }else inc(rejected);
   }
   continue; // Functional snapshots never enter RW fine fitting or RC7 maps.
  }
#endif
#ifdef PTOB_N30
  if(n30_fine_backend_selected()&&row.identity==sync_identity&&now>=row.at&&now-row.at<50000&&client_window_active(row.at,window_phase)){
    const auto matched=n30_fine_backend_receive(row.capture);
    if(matched==1){fine_pending=row;fine_waiting=true;break;}
    if(matched!=2){row.capture.actual=false;row.capture.association_observed=false;}
  }
#endif
#ifdef PTOB_W12_FINE_CLOCK
  if(row.identity==sync_identity&&now>=row.at&&now-row.at<50000&&client_window_active(row.at,window_phase)){
    const auto matched=w12_fine_receive(row.capture);
    if(matched==1){fine_pending=row;fine_waiting=true;break;}
    if(matched!=2){row.capture.actual=false;row.capture.association_observed=false;}
  }
#endif
  struct ReceiveObservation {
    const Periodic& row;uint64_t now;uint32_t authenticated,accepted,refused;
    ~ReceiveObservation(){
      uint32_t outcome=G1ReceiveLoss::Report;
      if(now<row.at||now-row.at>=50000)outcome|=G1ReceiveLoss::Stale;
      if(authenticated!=p1_ok+relay_ok)outcome|=G1ReceiveLoss::Authenticated;else outcome|=G1ReceiveLoss::Refused;
      if(row.capture.actual&&row.capture.association_observed)outcome|=G1ReceiveLoss::Associated;
      if(accepted!=corrected_clock.accepted())outcome|=G1ReceiveLoss::Mapped;
      if(refused!=corrected_clock.refused())outcome|=G1ReceiveLoss::Refused;
      receive_loss.observe(now,outcome,row.capture.epoch);
    }
  } observation{row,now,p1_ok+relay_ok,corrected_clock.accepted(),corrected_clock.refused()};
  if(row.identity!=sync_identity||now<row.at||now-row.at>=50000||!client_window_active(row.at,window_phase)){inc(p1_bad);diagnostic.reason(G1Reason::PeriodicStale,now);continue;}
  if(rc_bound()){
    rc7_packet p{};
    if(row.length!=190||rc7_decode(&p,row.bytes,row.length,&rc_client.receiver.path,&provision.group)){inc(relay_bad);continue;}
    inc(relay_ok);
#ifdef PTOB_N30
    signal_received(row,now,sync_capture_epoch);
#endif
#ifdef V12_R2_RX_SHADOW
    uint64_t available=0;clock_reference_read(&available);
    // Match T096 RC7: delivery is a current-path authenticated physical PA
    // packet, independent of whether its rolling corrections complete a map.
    // This diagnostic cannot grant source permission or usable clock time.
    if(row.capture.epoch==sync_capture_epoch&&Rc7Client::captured(row.capture,available))
      parent_selection.delivered(p.train,row.capture.event,now);
    const auto discontinuities=corrected_clock.discontinuities();
    const auto* original=rc_client.receive(p,row.capture,available);
    G1CorrectedClock::Rc7Update update{};
    if(original)update=corrected_clock.receive_admitted_rc7(rc_client,available,now,anchor,provision.group);
    if(update.kind==G1CorrectedClock::Rc7Kind::PermissionOnly){
      // Fresh matched source entitlement only; retain the delivered model,
      // numeric-window evidence and relay original/age/snapshots.
      anchor.until=update.permission_until_us;rc_client.progress_at=now;
      corrected_clock.scope(anchor);
    }
    if(update.kind==G1CorrectedClock::Rc7Kind::Mapped){
      rc_client.utc_known=false;
      uint64_t completion=0,utc=0;const auto delivered=micros();
      if(clock_reference_read(&completion)&&corrected_clock.finalize(completion)&&corrected_clock.deliver(completion,delivered,utc)&&utc>=delivered-anchor.at){
        anchor.utc=utc-(delivered-anchor.at);anchor.until=corrected_clock.until();rc_client.utc_known=true;
        rc_client.progress_at=now;window_corrected=true;corrected_clock.scope(anchor);
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
        const auto& ref=rc_client.receiver.clock.reference;relay_input(rc_client.receiver.path,ref.origin,ref.proof,original->stamp.tick20,original->stamp.epoch,completion,ref.age_us);
#endif
      }else{
        corrected_clock.withdraw();anchor.until=0;window_corrected=false;
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
        if(relay_map.withdraw_time()){relay_data_dirty=true;relay_cap_dirty=true;}
#endif
      }
    }
    if(corrected_clock.discontinuities()!=discontinuities)changed_scope(now,G1Reason::PeriodicScope,false,G1ParentReason::ClockStep);
#endif
    continue;
  }
  if(exclude_direct_gm){inc(p1_bad);continue;}
  if(row.length==158){
    const uint64_t ids[4]={row.identity,0,0,0};r2_precision_packet p{};
    if(r2_group_precision_decode(&p,row.bytes,158,&provision.group,ids)){inc(p1_bad);diagnostic.reason(G1Reason::PeriodicAuth,now);continue;}inc(p1_ok);
#ifdef PTOB_N30
    signal_received(row,now,sync_capture_epoch);
#endif
    if(!anchor_valid(now))continue;
    const bool same=row.identity==anchor.root&&p.boot==anchor.boot&&p.source_epoch==anchor.source_epoch&&p.train==anchor.publication;
    if(!same){if(fresh_envelope(p.created_ns,p.expires_ns,p.utc_offset_ns,now))forget_leader(now);continue;}
    renew_permission(p.created_ns,p.expires_ns,p.utc_offset_ns,now);discipline_scope(now);
#ifdef V12_R2_RX_SHADOW
    uint64_t available=0;clock_reference_read(&available);
    if(discipline.authenticated_receive(p,row.capture,available,now,coarse_ns(now))){window_corrected=true;}
#endif
  }else if(row.length==190){
    // Stable IDs carried in the authenticated origin/hop, never label/roster
    // positions. Decode authenticates both domains and rejects depth two.
    uint64_t ids[4]{};unsigned count=0;const unsigned offsets[3]={14,90,98};
    for(auto offset:offsets){const auto id=r2_group_get(row.bytes+offset,8);bool seen=false;for(unsigned j=0;j<count;j++)seen|=ids[j]==id;if(id&&!seen)ids[count++]=id;}
    r2_relay_packet p{};
    if(r2_group_relay_decode3(&p,row.bytes,190,&provision.group,ids)||ids[p.route.transmitter]!=row.identity||p.route.depth!=0||p.route.slot!=0||p.route.transmitter!=p.origin.root||p.route.parent!=p.origin.root||p.route.boot!=p.origin.boot){inc(relay_bad);diagnostic.reason(G1Reason::PeriodicAuth,now);continue;}inc(relay_ok);
#ifdef PTOB_N30
    signal_received(row,now,sync_capture_epoch);
#endif
    if(!anchor_valid(now)){diagnostic.reason(G1Reason::Permission,now);continue;}
    const bool same=ids[p.origin.root]==anchor.root&&p.origin.boot==anchor.boot&&p.origin.source_epoch==anchor.source_epoch&&
      row.identity==joined_peer&&p.route.boot==joined_boot&&(!relay_train||relay_train==p.route.train);
    if(!same){if(fresh_envelope(p.origin.created_ns,p.origin.expires_ns,p.origin.utc_offset_ns,now,true))forget_leader(now);continue;}
    if(!fresh_envelope(p.origin.created_ns,p.origin.expires_ns,p.origin.utc_offset_ns,now,true)){diagnostic.reason(G1Reason::Permission,now);continue;}
    relay_train=p.route.train;
    if(row.capture.actual&&row.capture.association_observed)parent_selection.delivered(p.route.train,row.capture.event,now);
    // An authenticated envelope is not a matched corrected observation.
    // Keep the original permission deadline until physical association succeeds.
    discipline_scope(now);
#ifdef V12_R2_RX_SHADOW
    uint64_t available=0;clock_reference_read(&available);
    const auto discontinuities=corrected_clock.discontinuities();
    rc5_origin matched_origin{};
    if(corrected_clock.receive(p,row.capture,available,now,anchor,&matched_origin)){
      uint64_t completion=0,utc=0;
      if(clock_reference_read(&completion)&&corrected_clock.finalize(completion)&&corrected_clock.deliver(completion,micros(),utc)&&utc>=micros()-anchor.at){
        const auto delivered=micros();anchor.until=corrected_clock.until();
        // Only this newly matched physical correction may maintain the coarse
        // clock. The original challenge session and signed map expiry stay
        // finite; repeated polling/PA receipt/held predictions cannot renew it.
        if(full_listening()&&direct_session.matches(anchor))
          direct_session.reanchor(anchor,corrected_clock.original20(),completion,
            delivered,utc,anchor.until,corrected_clock.coarse_enclosure_ns());
        else anchor.utc=utc-(delivered-anchor.at);
        last_origin_ns=p.origin.created_ns;window_corrected=true;corrected_clock.scope(anchor);
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
        rc7_path parent{};parent.source_epoch=anchor.source_epoch;parent.node[0]={anchor.root,anchor.boot,p.route.train,p.route.capture_epoch,0,0};
        relay_input(parent,matched_origin,nullptr,corrected_clock.original20(),row.capture.epoch,completion,0);
#endif
      }
    }
    if(corrected_clock.discontinuities()!=discontinuities){
      // An invalid clock cannot renew the coarse mapping or remain HOLD.
      // Existing supported PA teardown and fresh direct JOIN reacquire the
      // source while normal field service remains requested indefinitely.
      changed_scope(now,G1Reason::PeriodicScope,false,G1ParentReason::ClockStep);
    }
    discipline_scope(now);
    (void)discipline.authenticated_gr3(p,row.capture,available,now,coarse_ns(now));
#endif
  }else inc(p1_bad);
}return processed;}
bool owners_active(){return relay_owned()||scan||peer.connecting||peer.handle!=None||sync_pending||synced;}
void cancel_connect(uint64_t now){
  if(!peer.connecting||connecting_cancelled)return;
  sdk_connect_pending=ble_gap_conn_active();
  if(cleanup_pending_link(now))return;
  const auto generation=peer.incarnation;const auto context=link_context(now);const int rc=ble_gap_conn_cancel();cancel_rc=rc;inc(cancel_calls);note(rc,R2Api::ConnectCancel);
  admission_trace(14,None,rc,&context);
  if(peer.incarnation!=generation||!peer.connecting)return;
  if(!rc||rc==BLE_HS_EALREADY){connecting_cancelled=true;peer.drain_until=peer.attempt_until+2000000;}
  else error(rc,R2Api::ConnectCancel);
}
bool window_poll(uint64_t now){
  const auto start=G1Window::start(now,window_phase);
  if(window_start!=start){
    if(window_start!=UINT64_MAX){if(!window_corrected)inc(window_misses);if(window_owned_us>window_max_owned_us)window_max_owned_us=window_owned_us;}
    if(window_start!=UINT64_MAX&&owners_active()&&!full_listening()){inc(quiet_faults);error(BLE_HS_ETIMEOUT,R2Api::Disconnect);return false;}
    window_start=start;window_faulted=false;window_corrected=false;window_owned_us=0;window_closed_at=0;window_last_poll=now;inc(windows);
  }
  if(now>=window_last_poll&&owners_active())window_owned_us+=now-window_last_poll;
  window_last_poll=now;
  // The observed disconnect arrived2.8ms beyond the quiet deadline. Close only
  // the transient admission owner at400ms; scan/PA retain450ms nominal and
  // all owners still must close by the unchanged500ms quiet boundary.
  // A public master descriptor can exist while the delayed CONNECT callback
  // is still pending. Begin only that proven hidden-ACL teardown at200ms;
  // retained failures delivered DISCONNECT at507ms under400ms cleanup and
  // at522ms under300ms cleanup (221ms between terminate and completion).
  // Normal admitted GATT retains its400ms cutoff and scan/PA retain450ms.
  if(!full_listening()){
    if(now-start>=200000)cleanup_pending_link(now);
    if(now-start>=400000){peer.selection_reported=true;cancel_connect(now);terminate_peer(now);}
  }
  if(client_window_active(now,window_phase))return true;
  stop_scan();cancel_connect(now);terminate_peer(now);cancel_sync(now);
  if(!owners_active()){if(!window_closed_at){
    window_closed_at=now;
    inc(diagnostic.closed);const uint32_t margin=now>=start+G1Window::Quiet?0:uint32_t(start+G1Window::Quiet-now);
    if(margin<diagnostic.close_min_margin_us)diagnostic.close_min_margin_us=margin;
    if(anchor.live(now))cold_windows=0;else if(cold_windows<8)++cold_windows;
    uint64_t corrected_utc=0,clock20=0;
#ifdef V12_R2_RX_SHADOW
    clock_reference_read(&clock20);
#endif
    // T096 r2_ble_client_align: change phase only from an actually delivered
    // corrected map, after owner drain, with at least500ms actual quiet.
    if(!corrected_phase_aligned&&G1Window::align(now,anchor,corrected_clock.covers(now,now))&&
       corrected_clock.deliver(clock20,now,corrected_utc)){
      window_phase=uint32_t((now%G1Window::Cycle+1200000-corrected_utc%G1Window::Cycle)%G1Window::Cycle);
      radio_resume_at=now+500000;corrected_phase_aligned=true;
      admission_trace(25,None,int(window_phase));
    }else if(!corrected_phase_aligned&&(phase_learning.due(anchor,now)||(cold_windows>=8&&!anchor.source&&!admission_root))){
      const bool permission_phase=phase_learning.due(anchor,now);
      auto context=link_context(now);context.generation=0;context.identity=0;context.request_at=0;
      // Delay the next window to search cold phases or leave two consecutive
      // fresh direct bootstraps with short signed lifetimes. This learns only
      // an admission opportunity; it grants no source/map alignment. At least
      //500ms actual quiet remains after closure, with no overlapping window.
      window_phase=(window_phase+125000)%G1Window::Cycle;
      radio_resume_at=start+G1Window::Cycle+125000;
      if(radio_resume_at<now+500000)radio_resume_at=now+500000;
      cold_windows=0;phase_learning.shifted();
      admission_trace(permission_phase?21:10,None,int(window_phase),&context);
    }
  }}
  else if(now>=start+G1Window::Quiet&&!window_faulted){window_faulted=true;admission_trace(8,peer.handle,int(now-start));inc(quiet_faults);error(BLE_HS_ETIMEOUT,R2Api::Disconnect);}
  // Ignore queued discovery at the quiet boundary; it is never a new RF
  // opportunity. The timer/anchor/servo history survive this intentional pause.
  Discovery discard{};while(discoveries.pop(discard)){}
  return false;
}
bool admission_due(uint64_t now,bool renewal_map){
  if(parent_selection.needs_alternative())return true;
  if(rc_bound())return rc_client.admission_due(now,
    !parent_selection.pinned()&&parent_selection.quality().poor,lock_protected(now));
  if(full_listening()&&direct_session.matches(anchor))return direct_session.due(anchor,now);
  return full_listening()?anchor.refresh(now,true):G1Window::initiate(now,anchor,renewal_map,window_phase);
}
void active_poll(uint64_t now){
  if(now<radio_resume_at)return;
  const bool corrected_live=corrected_clock.covers(now,now);
  if(corrected_was_live&&!corrected_live&&corrected_clock.until()&&now>=corrected_clock.until())receive_loss.freeze(now);
  corrected_was_live=corrected_live;
  if(parent_failure.identity){parent_selection.failed(parent_failure.identity,parent_failure.relay,parent_failure.at);parent_failure={};}
  parent_selection.usable(following(now)&&corrected_clock.covers(now,now)&&!fault&&!closing,now);
  parent_selection.tick(now,synced&&!sync_terminating&&client_window_active(now,window_phase),scan&&!peer.connecting);
  // A continuous permission gap requests fresh admission, as T096 does; it
  // does not discard an independently valid replay/acquisition anchor. Old
  // FOLLOW already ended at its signed deadline. Only a newly matched fresh
  // correction can restore it. Losing the anchor here also loses PA reception
  // and turns a missed window into repeated expensive JOIN-only acquisition.
  // Source permission already withdrew FOLLOW at its signed expiry. After
  // the existing1.5s acquisition-loss bound, stop retrying a six-minute old
  // session and rediscover. Short packet gaps keep their current acquisition.
  if(anchor.source&&anchor.until&&now>=anchor.until&&now-anchor.until>=1500000&&
     !corrected_clock.covers(now,now))forget_leader(now);
  else if(anchor.source&&!anchor.valid(now))forget_leader(now);
  if(!rc_bound()&&anchor.source&&anchor.rebootstrap(now))member=false;
  if(usable_utc()&&anchor.valid(now)&&!anchor.live(now))coarse_hold.retain(anchor);
  if(admission_root&&now>=admission_until)admission_root=admission_until=0;
  if(peer.connecting&&now>=peer.attempt_until){failed_admission(now);cancel_connect(now);}
  if((peer.connecting&&connecting_cancelled&&now>=peer.drain_until)||(peer.terminating&&now>=peer.drain_until))error(BLE_HS_ETIMEOUT,R2Api::Disconnect);
  probe_pending_link(now);
  // Enforce this window before any public GATT operation can yield or send.
  if(mode==R2Mode::Client&&!window_poll(now)){periodic_poll(micros());return;}
  // Share one eight-report budget across control boundaries. A
  // yielding SDK call may return with new callbacks queued; consume them
  // before the next control operation can make those reports stale.
  unsigned precision_left=8;
  const auto precision_work=[&](){periodic_rx::poll();precision_left-=periodic_poll(micros(),precision_left);now=micros();};
  precision_work();
  relay_poll(micros(),admission_due(now,corrected_clock.covers(now,now))&&admission_policy.allowed(now)&&now>=retry_at);
  precision_work();gatt_poll(micros());precision_work();receive_join(micros());
  if(peer.relay)send_rc_request(micros());else send_join(micros());
  precision_work();
  // History updates advance its clock; subsequent deferral checks must use
  // the same fresh time rather than treating an older worker time as invalid.
  now=micros();parent_selection.locked(lock_protected(now),now);
  if((synced||sync_pending)&&((anchor_valid(now)&&sync_identity!=anchor.root&&sync_identity!=joined_peer)||(sync_pending&&now>=sync_acquire_until)))cancel_sync(now);
#ifdef PTOB_RC9
  if((synced||sync_pending)&&sync_selected_sid==14&&!rc9_bound())cancel_sync(now);
#endif
  if((sync_cancelled&&sync_pending)||(sync_terminating&&synced)){if(now>=sync_drain_until)error(BLE_HS_ETIMEOUT,R2Api::SyncTerminate);}
  // Optional early JOIN can use an existing map covering this whole window.
  // Otherwise receive first. Original100ms initiation and duty stay intact.
  const bool renewal_map=window_corrected||corrected_clock.covers(now,G1Window::start(now,window_phase)+G1Window::Nominal);
  bool fallback_observation=false;
#ifdef PTOB_RC9
  fallback_observation=rc9_auto_recovery(now);
#endif
  const bool gm_observation=gm_discovery.observe(now,((rc_bound()&&anchor.root&&
    !parent_selection.deferred(anchor.root,false,now))||fallback_observation)&&!exclude_direct_gm&&
    !parent_selection.pinned()&&!parent_diagnostic.pending()&&!closing&&!fault);
  // Recreate the known direct train immediately inside this window. Waiting
  // for another EXT_DISC report costs a discovery interval before the
  // controller can even acquire SyncInfo; late establishment leaves no room
  // for an original and its later correction. The retained canonical address
  // is only a locator. JOIN scope and both GR3 MACs still authorize every
  // sample; no cached packet, permission or controller handle is reused.
  // Leave the original early admission slot available when refresh is due.
  if(anchor_valid(now)&&(rc_bound()||(anchor.root==joined_peer&&anchor.boot==joined_boot&&same_address(sync_address,canonical(anchor.root))))&&
     (!admission_due(now,renewal_map)||lock_protected(now))&&
     peer.handle==None&&!peer.connecting&&!sync_pending&&!synced&&!sync_selected&&!sync_cleanup_queued){
    sync_identity=joined_peer;sync_selected=true;
  }
  const bool protected_parent=selection_protected(micros())&&!gm_observation;
  if(protected_parent&&!relay_slot_scanning()){
    stop_scan();
    // Retire an abandoned logical BUSY retry only after actual scanner OFF.
    // Missing closure keeps its fault/owner deadline; it is never renewed.
    if(!scan&&!ble_gap_disc_active())scan_retry_at=scan_retry_until=0;
  }
  Discovery d{};for(unsigned i=0;i<16&&discoveries.pop(d);i++){
    if(protected_parent&&!relay_slot_scanning())continue;
    if(now<d.at||now-d.at>250000||(mode==R2Mode::Client&&G1Window::start(d.at,window_phase)!=window_start))continue;
    observe_group(d);
#ifdef PTOB_RC9
    if(rc9_discovery(d))continue;
#endif
    G1Reason filter=G1Reason::None;uint64_t id=discovery_identity(d,&filter);bool relay=false,relay_publishing=false;
    if(!id&&d.sid==13&&(d.props&1))for(unsigned at=0;at<d.length;){
      const unsigned n=d.bytes[at];if(!n||at+n>=d.length)break;
      if(n==145&&d.bytes[at+1]==0xff){rc7_control cap{};
        if(!rc7_capability_decode(&cap,d.bytes+at+2,144,&provision.group)){
          relay_slot_observe(cap,d.rssi,d.at);
          if(rc_client.path_usable(cap.path,provision.identity)&&cap.sender!=provision.identity){id=cap.sender;relay=true;relay_publishing=cap.publishing;}
        }
        else ++rc_client.rejected;
      }at+=n+1;
    }
    if(protected_parent)continue; // Slot observations cannot change a locked parent.
    if(id&&parent_selection.accepts(id,relay)){
      // During a maintained relay lock, observe only its authenticated root's
      // direct locator. Other advertisements cannot churn the live parent.
      if(gm_observation&&!fallback_observation&&(relay||id!=anchor.root))continue;
      parent_selection.seen(id,relay,d.rssi,d.at,relay_publishing);unsigned at=8;
      for(unsigned j=0;j<8;j++)if(locator_ids[j]==id){at=j;break;}
      if(at==8){at=0;for(unsigned j=0;j<8;j++)if(!locator_ids[j]||locators[j].at<locators[at].at)at=j;}
      locator_ids[at]=id;locators[at]=d;diagnostic.candidate(id,d.rssi,d.at);inc(discovery_count);
    }
    if(anchor_valid(now)&&peer.handle==None&&!peer.connecting&&!sync_pending&&!synced&&!sync_selected&&d.sid==12&&d.interval==50){
      const bool direct=!exclude_direct_gm&&parent_selection.accepts(joined_peer,false)&&!rc_bound()&&same_address(d.address,canonical(anchor.root));
      const bool relay_match=rc_bound()&&same_address(d.address,sync_address);
      if(direct||relay_match){sync_identity=joined_peer;sync_address=d.address;sync_selected=true;}
    }
  }
  bool gm_available=false;
  if(gm_observation)for(unsigned j=0;j<8;j++)if(locator_ids[j]==anchor.root&&locators[j].sid!=13&&
     now>=locators[j].at&&now-locators[j].at<G1ParentSelection::FreshUs&&!cooled(anchor.root,now))gm_available=true;
  const bool due=admission_due(now,renewal_map)||gm_available;
  const bool slot=full_listening()||G1Window::initiate(now,anchor,renewal_map,window_phase)||
    (rc_bound()&&now>=window_start&&now-window_start<G1Window::Initiate&&due);
  bool independent_rc9=false;
#ifdef PTOB_RC9
  independent_rc9=rc9_selection(now);
#endif
  if(!independent_rc9&&mode==R2Mode::Client&&due&&slot&&peer.handle==None&&!peer.connecting&&now>=retry_at){
    // A maintained lock may renew only its current authenticated publisher.
    // The retained address is a locator for a fresh challenge exchange, never
    // a cached permission. Recent physical PA, same binding and original
    // admission/model/source deadlines are all still required; no scan/probe.
    if(protected_parent){
      const auto& quality=parent_selection.quality();
      if(quality.last_rx&&now>=quality.last_rx&&now-quality.last_rx<250000&&!cooled(joined_peer,now)&&!sync_cleanup_queued&&!sync_terminating){
        Discovery current{};current.address=sync_address;current.sid=rc_bound()?13:12;
        connect(current,joined_peer,now,rc_bound());
      }
    }else{
     const auto id=parent_selection.choose(now,rc_bound()&&now>=rc_client.renew_at);
     for(unsigned j=0;j<8;j++)if(id&&locator_ids[j]==id&&!cooled(id,now)){
      bool relay=locators[j].sid==13;
      if(sync_cleanup_queued||sync_terminating)break;
      if(rc_bound()&&(parent_selection.pinned()||!parent_selection.quality().poor)&&now>=rc_client.progress_at&&now-rc_client.progress_at>=2000000)rc_client.retry_at=now+10000000;
      connect(locators[j],id,now,relay);break;
     }
    }
  }
  bool independent_scan=true;
#ifdef PTOB_RC9
  const auto rc9_view=rc9_client.view(now);
  independent_scan=gm_observation||!independent_rc9||!rc9_view.valid||now>=rc9_client.renew_at;
  if(!independent_scan)stop_scan();
#endif
  if(independent_scan&&!protected_parent&&!peer.connecting&&!scan&&(!synced||due||gm_observation))start_scan(gm_observation);
  if(!relay_slot_scanning()&&(protected_parent||(synced&&!due&&!gm_observation)))stop_scan();
  if(sync_selected&&peer.handle==None&&!peer.connecting&&!sync_pending&&!synced&&!sync_cleanup_queued&&timely()&&client_window_active(micros(),window_phase)){
    ble_gap_periodic_sync_params p{};p.skip=0;p.sync_timeout=100;sync_selected=false;sync_pending=true;sync_cancelled=false;sync_acquire_until=micros()+2000000;
    sync_requested_at=micros();sync_owner_window_start=G1Window::start(sync_requested_at,window_phase);sync_drain_until=0;sync_phy=0;sync_clock_accuracy=255;sync_trace(15,None,0);
    const auto context=sync_context();
    sync_capture_epoch=periodic_rx::scope(None,true);
#ifdef V12_R2_RX_SHADOW
    if(!sync_capture_epoch){sync_pending=false;sync_identity=0;error(BLE_HS_EINVAL,R2Api::SyncCreate);return;}
#endif
    uint8_t selected_sid=12;
#ifdef PTOB_RC9
    if(rc9_bound())selected_sid=14;
#endif
    sync_selected_sid=selected_sid;
    const int rc=ble_gap_periodic_adv_sync_create(&sync_address,selected_sid,&p,gap,reinterpret_cast<void*>(uintptr_t(sync_capture_epoch)));sync_trace(16,None,rc,&context);if(rc){periodic_rx::scope(None);sync_pending=false;sync_identity=0;if(!transient(rc,R2Api::SyncCreate))error(rc,R2Api::SyncCreate);}
  }
  precision_work();
}
void parent_diagnostic_poll(uint64_t now){
  portENTER_CRITICAL(&mux);const auto control=parent_diagnostic;
  if(control.state==1)parent_diagnostic.state=2;
  portEXIT_CRITICAL(&mux);
  if(!control.pending())return;
  if(control.state==1){
#ifdef PTOB_RC9
    rc9_client.fence();
#endif
    changed_scope(now,G1Reason::PeriodicScope,false,G1ParentReason::Manual);corrected_clock.clear();coarse_hold.clear();joined_peer=joined_boot=0;
    peer.selection_reported=true;peer.dead=true;sync_selected=false;
    Incoming in{};while(incoming.pop(in)){};Discovery d{};while(discoveries.pop(d)){};
    Periodic p{};while(periodic.pop(p)){};
    for(unsigned i=0;i<8;i++){locator_ids[i]=0;locators[i]={};recent[i]={};}
  }
  bool preserve_demand=false;
#if defined(PTOB_RC9) && defined(PTOB_W12_LORA_RX)
  uint64_t target9=0;portENTER_CRITICAL(&mux);target9=rc9_target;portEXIT_CRITICAL(&mux);
  preserve_demand=!target9&&rc9_server_selected(now);
#endif
  relay_close(now,preserve_demand);stop_scan();cancel_connect(now);terminate_peer(now);cancel_sync(now);
  // A diagnostic rebind preserves the profile's existing quiet deadline.
  if(!full_listening())window_poll(micros());
  const bool closed=!owners_active()&&!ble_gap_disc_active()&&!ble_gap_conn_active()&&
    !sync_cleanup_queued&&!sync_cleanup_requested&&sync_handle==None;
  if(closed){
    Incoming in{};while(incoming.pop(in)){};Discovery d{};while(discoveries.pop(d)){};
    Periodic p{};while(periodic.pop(p)){};
    periodic_rx::scope(None);
  }
  const auto completed=micros();
  portENTER_CRITICAL(&mux);
  if(parent_diagnostic.observe(completed)||fault){parent_diagnostic.state=4;upstream_mode_change=false;fault=true;}
  else if(closed&&parent_diagnostic.state==2){
    if(upstream_mode_change){exclude_direct_gm=relay_only_requested;upstream_mode_change=false;}
    parent_selection.reset();parent_selection.diagnostic(exclude_direct_gm);parent_selection.target(control.target,control.relay);
    peer={};sync_identity=0;sync_address={};sync_cancelled=sync_terminating=false;
    retry_at=scan_retry_at=scan_retry_until=0;
    parent_diagnostic.state=3;parent_diagnostic.completed_us=completed;
  }
  portEXIT_CRITICAL(&mux);
}
void publish(uint64_t begin){const auto now=micros();uint64_t tick=0;
#ifdef PTOB_RC9
  rc9_publish_status(now);
#endif
  parent_record(G1ParentReason::Scope,now);
  const bool sdk_scan_active=ble_gap_disc_active();
  sdk_connect_pending=ble_gap_conn_active();
#ifdef V12_R2_RX_SHADOW
  clock_reference_read(&tick);
#endif
  const auto q=discipline.view(tick,now);uint64_t delivered_utc=0;
  const bool disciplined=discipline.delivered(q,tick,delivered_utc);uint64_t held_utc=0;
  uint64_t corrected_utc=0;const bool corrected_valid=corrected_clock.deliver(tick,now,corrected_utc)&&!fault&&!closing;
  if(corrected_clock.accepted()!=retained_accepted&&corrected_valid&&corrected_clock.original20()){
    if(retained_estimate.retain(corrected_clock.prediction(),anchor.root,anchor.boot,anchor.source_epoch,corrected_clock.original20(),now)){
      retained_accepted=corrected_clock.accepted();admission_policy.acquired();
      if(!receive_loss.generation)receive_loss.begin();
      else if(!corrected_was_live)receive_loss.reacquired();
      corrected_was_live=corrected_clock.covers(now,now);
    }
  }
  uint64_t retained_utc=0;const bool retained_valid=retained_estimate.deliver(tick,retained_utc)&&!fault&&!closing;
  const bool held=usable_utc()&&coarse_hold.deliver(now,held_utc)&&!fault&&!closing;
  const bool following_now=following(now)&&!closing&&!fault;
  // A remembered model owns its old scope. A new coarse JOIN has no physical
  // model until its own first correction, and must never borrow the old one.
  const bool retained_selected=retained_valid&&!corrected_valid&&!following_now&&!held;
  const auto selection=parent_selection.selection_view(now);
  const auto relative_quality=corrected_valid?corrected_clock.quality(now,tick):G1RelativeQuality{};
  const bool selection_locked=lock_protected_quality(relative_quality,corrected_clock.prediction(),tick,now);
  portENTER_CRITICAL(&mux);
  snap.mode=mode;snap.lease=lease;snap.client=client;snap.requested=mode!=R2Mode::Off&&!closing;snap.fault=fault;snap.active_used=active_used;snap.self=3;snap.scan=scan;snap.connecting=peer.connecting;snap.connections=(peer.handle!=None)+!relay_acl_available();snap.synced=synced;snap.sync_pending=sync_pending;
  snap.off_ack=client.off()&&!lease.exhausted&&lease.stop_generation==lease.stop_completed&&(!lease.generation||lease.completed_us)&&!command_pending&&!stop_pending&&!closing&&mode==R2Mode::Off&&!relay_owned()&&!scan&&!sdk_scan_active&&!sdk_connect_pending&&!peer.connecting&&peer.handle==None&&!sync_pending&&!synced&&!sync_cleanup_queued&&!fault;
#ifdef V12_R2_FIELD_CLIENT
  snap.boot_client=true;
#endif
  snap.boot_client_attempted=boot_client_attempted;snap.boot_client_cancelled=boot_client_cancelled;
  snap.group_member=member;snap.group_id=provision.group.id;snap.key_epoch=provision.group.epoch;snap.client_identity=provision.identity;snap.joined_peer=joined_peer;snap.joined_boot=joined_boot;snap.join_attempts=join_attempts;snap.join_accepts=join_accepts;snap.join_rejected=rejected;snap.join_busy=join_busy;
  snap.follower_snapshot_us=now;snap.follower_received_us=anchor.at;
  snap.ready=member&&!closing;snap.following=following_now;snap.clock_valid=corrected_valid||snap.following||held||retained_selected;snap.follower_utc_us=corrected_valid?corrected_utc:snap.following?(disciplined?delivered_utc:anchor.utc+now-anchor.at):held?held_utc:retained_selected?retained_utc:0;
  snap.root_identity=snap.following?anchor.root:0;snap.root=snap.following?0:255;snap.root_boot=snap.following?anchor.boot:0;snap.source_epoch=snap.following?anchor.source_epoch:0;snap.publication=snap.following?anchor.publication:0;snap.service_until_us=snap.following?anchor.until:0;
  snap.serving=snap.source=false;snap.coordinator=snap.standby=255;snap.members=snap.role=0;
  snap.rx=rx;snap.tx=tx;snap.authenticated=auth;snap.rejected=rejected;snap.discoveries=discovery_count;snap.pa_reports=pa_reports;snap.p1_authenticated=p1_ok;snap.p1_refused=p1_bad;snap.api_errors=api_errors;snap.drops=incoming.drops+discoveries.drops+periodic.drops;snap.peer_losses=peer_losses;snap.setup_retries=setup_retries;snap.last_api=last_api;snap.last_api_rc=last_api_rc;snap.last_api_at_us=last_api_at;snap.first_error_api=first_error_api;snap.first_error_rc=first_error_rc;snap.first_error_at_us=first_error_at;
  snap.g1_gatt_stage=uint8_t(peer.stage);snap.g1_last_gatt_stage=last_gatt_stage;
  snap.g1_pending_disconnects=pending_disconnects;snap.g1_pending_link_teardowns=pending_link_teardowns;snap.g1_cancel_calls=cancel_calls;snap.g1_cancel_rc=cancel_rc;snap.g1_sdk_connect_pending=sdk_connect_pending;snap.g1_last_disconnect_reason=last_disconnect_reason;snap.g1_last_disconnect_at_us=last_disconnect_at;
  snap.g1_adv_callbacks=adv_callbacks;snap.g1_adv_incomplete=adv_incomplete;snap.g1_adv_long=adv_long;snap.g1_adv_nonrandom=adv_nonrandom;snap.g1_discovery_checked=discovery_checked;snap.g1_group_records=group_records;snap.g1_sdk_scan_active=sdk_scan_active;snap.g1_last_adv_at_us=last_adv_at;snap.g1_last_group_id=last_group_id;snap.g1_last_key_epoch=last_key_epoch;snap.g1_last_group_at_us=last_group_at;
  const auto& clock_anchor=anchor.source?anchor:coarse_hold.saved;
  snap.follower_received_us=clock_anchor.at;
  G1CoarseHold coarse_snapshot;coarse_snapshot.retain(clock_anchor);uint64_t coarse_utc=0;coarse_snapshot.deliver(now,coarse_utc);
  snap.g1_coarse_snapshot_utc_us=corrected_valid?corrected_utc:coarse_utc;snap.g1_model_until_us=discipline.model_until_us();
  snap.g1_clock_origin=snap.clock_valid&&clock_anchor.valid(now)?clock_anchor.root:0;
  snap.g1_clock_model=corrected_valid?corrected_clock.prediction():retained_selected?retained_estimate.clock:G1ClockPrediction{};
  if(retained_selected)snap.g1_clock_origin=retained_estimate.root;
  snap.g1_retained_estimate=retained_selected;snap.g1_estimate_observed_us=retained_estimate.observed_us;
  snap.g1_retained_clock=retained_estimate;
  snap.g1_admission_policy=admission_policy;snap.g1_receive_loss=receive_loss;
#if defined(V12_R2_OUTPUT_CORRECTION_NS) && V12_R2_OUTPUT_CORRECTION_NS != 0
  const auto output_reference=periodic_rx::rf_reference();
  snap.g1_clock_model=g1_output_correction(snap.g1_clock_model,V12_R2_OUTPUT_CORRECTION_NS,
    clock_anchor.root,clock_anchor.boot,clock_anchor.source_epoch,listening_percent,
    corrected_valid&&clock_anchor.valid(now)&&snap.requested&&!fault&&!closing,
    output_reference.valid,output_reference.phy,output_reference.extra_us,
    V12_R2_OUTPUT_ROOT,V12_R2_OUTPUT_BOOT,V12_R2_OUTPUT_SOURCE_EPOCH);
#endif
  snap.g1_corrected_accepted=corrected_clock.accepted();snap.g1_corrected_refused=corrected_clock.refused();
  snap.g1_corrected_until_us=corrected_clock.until();snap.g1_corrected_original20=corrected_clock.original20();
  snap.g1_frequency_trained=corrected_clock.frequency_trained(tick);snap.g1_frequency_pairs=corrected_clock.rate_pairs();snap.g1_frequency_scatter_ns=corrected_clock.scatter_ns();
  snap.g1_fit_points=uint16_t(corrected_clock.fit_points());snap.g1_fit_span_ms=uint16_t(corrected_clock.fit_span_ms());snap.g1_fit_reason=corrected_clock.fit_reason();
  snap.g1_quality_missing=corrected_clock.quality_missing();
  snap.g1_clock_predictions=corrected_clock.predictions();snap.g1_clock_causal_squares=corrected_clock.causal_squares();snap.g1_clock_nominal_squares=corrected_clock.nominal_squares();snap.g1_clock_causal_max_ns=corrected_clock.causal_max_ns();
  snap.g1_clock_discontinuities=corrected_clock.discontinuities();snap.g1_clock_last_discontinuity_ns=corrected_clock.last_discontinuity_ns();snap.g1_clock_fit_tick20=corrected_clock.fit_tick20();
  snap.g1_relative_quality=relative_quality;
  snap.g1_relative_quality.evaluated20=tick;
  snap.g1_recovery=recovery;
  snap.g1_anchor_deadline_us=clock_anchor.source?clock_anchor.deadline():0;snap.g1_anchor_guard_us=clock_anchor.source?clock_anchor.guard(now):0;
  snap.g1_refresh_needed=anchor.refresh(now,full_listening());snap.g1_refresh_urgent=anchor.urgent(now,full_listening());snap.g1_refresh_attempts=refresh_attempts;
  snap.g1_window_start_us=window_start==UINT64_MAX?0:window_start;snap.g1_window_closed_us=window_closed_at;snap.g1_window_max_owned_us=window_max_owned_us;
  snap.rc7_exclude_direct_gm=snap.rc7_relay_only=exclude_direct_gm;
  snap.rc7_relay_only_requested=relay_only_requested;snap.rc7_upstream_mode_pending=upstream_mode_change&&parent_diagnostic.pending();
  snap.rc7_bound=rc_bound();snap.rc7_depth=rc_bound()?rc_client.receiver.path.depth:0;
  rc7_path_snapshot::encode(snap.rc7_path_hex,rc_bound()?&rc_client.receiver.path:nullptr);
  snap.rc7_capabilities=rc_client.capabilities;snap.rc7_requests=rc_client.requests;snap.rc7_responses=rc_client.responses;snap.rc7_pending=rc_client.pending;
  snap.rc7_lease_until_us=rc_client.lease_until;snap.rc7_renew_at_us=rc_client.renew_at;
  snap.rc7_delivery=parent_selection.quality().delivery_per_mille;
#ifdef PTOB_N30
  snap.n30_signal=snap.requested&&!fault&&!closing&&synced&&!sync_terminating&&sync_identity==joined_peer?signal_strength.snapshot(sync_identity,sync_capture_epoch,now):n30::Signal{};
#endif
  snap.g1_parent_entries=selection.entries;snap.g1_parent_score=selection.current_score;
  snap.g1_parent_best=selection.best_score;snap.g1_parent_reason=selection_locked&&!gm_discovery.active(now)?6:uint8_t(selection.reason);
  snap.rc_parent=parent_diagnostic;
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
  snap.rc7_server_flags=relay_flags(tick);snap.rc7_server_publication=relay_map.publication;snap.rc7_server_admissions=relay_admissions;
  snap.advertising=relay_advertising||relay_discovery;
#endif
  snap.g1_listening_percent=listening_percent;snap.g1_windows=windows;snap.g1_window_misses=window_misses;snap.g1_quiet_faults=quiet_faults;snap.g1_relay_authenticated=relay_ok;snap.g1_relay_refused=relay_bad;
  snap.link_mask=peer.handle!=None;snap.gatt_mask=peer.ready;snap.peer_mtu_min=observed_mtu;snap.scan_retry_deadline_us=scan_retry_until;snap.close_deadline_us=close_until;snap.precision_interval_units=50;snap.precision_hz=16;snap.schedule_descriptor=UINT64_C(0x503132020000f424);
  snap.rx_capture=periodic_rx::status();snap.host_stack_min_bytes=host_stack_min;
#ifdef V12_R2_RX_SHADOW
  snap.host_stack_configured_bytes=CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE;
#endif
  snap.discipline_accepted=q.accepted;snap.discipline_refused=q.refused;snap.rms_ns=q.rms_ns;snap.precision_ns=q.precision_ns;snap.hold_ms=q.hold_ms;snap.hold_samples=q.hold_samples;snap.trim_mppb=q.trim_mppb;snap.precision_fit=q.fit;snap.model_available20=q.available20;snap.precision_model=q.prediction;
  snap.g1_diagnostics=diagnostic;snap.discipline_first_refusal=q.first_refusal;snap.discipline_last_refusal=q.last_refusal;
  snap.g1_first_fault_site=first_fault_site;
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
  snap.rc7_admission_overlaps=relay_admission_overlaps;snap.rc7_quarantine_timeouts=relay_quarantine_timeouts;
  snap.rc7_quarantine_active=0;for(const auto h:relay_quarantined)if(h!=None)++snap.rc7_quarantine_active;
#endif
  for(unsigned i=0;i<16;i++)snap.discipline_reason_counts[i]=q.reason_counts[i];
  if(now>=begin&&now-begin>snap.worker_max_us)snap.worker_max_us=now-begin;
  portEXIT_CRITICAL(&mux);
}
}
bool r2_init(uint64_t boot,const uint8_t factory[6]){
  const auto device=native_device_binding();uint64_t id=0;for(unsigned i=0;i<6;i++)id=(id<<8)|factory[i];
  if(initialized||!boot||!device||std::memcmp(factory,device->factory,6)||id!=provision.identity||!r2_group_valid(&provision.group)||!r2_group_name(provision.name,std::strlen(provision.name))||!provision.map_bound_us||provision.map_bound_us>50000)return false;
  parent_selection.diagnostic(exclude_direct_gm);boot_id=boot;esp_fill_random(&nonce,sizeof(nonce));if(!nonce||nonce==UINT64_MAX)return false;own_address=canonical(id);
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
  relay_challenge=nonce;
#endif
  initialized=true;snap.self=3;return true;
}
bool r2_host_init(){return initialized&&!ble_gatts_count_cfg(services)&&!ble_gatts_add_svcs(services);}
bool r2_host_sync(){const bool ready=initialized&&!ble_hs_id_set_rnd(own_address.val);const bool reset_sync=ready&&recovery.stage==G1ResetRecovery::AwaitSync;portENTER_CRITICAL(&mux);host_ready=ready;
  if(ready)recovery.synced(micros());
#ifdef V12_R2_FIELD_CLIENT
  const bool admit=ready&&!boot_client_attempted&&!boot_client_cancelled;if(admit)boot_client_attempted=true;
#endif
  portEXIT_CRITICAL(&mux);
  if(reset_sync)admission_trace(27,None,0);
#ifdef V12_R2_FIELD_CLIENT
  if(admit&&!r2_client_start(boot_id)){fault=true;return false;}
#endif
  return ready;
}
void r2_host_cleanup(){
  // Tail dispatch plus a bounded8ms pending-create handoff lets actual SYNC
  // callbacks arrive before CANCEL. The pinned controller can omit CANCEL ACK
  // when first-PDU establishment wins that handoff. Capture already ended;
  // the original2s drain and50-profile500ms closure deadlines are unchanged.
  portENTER_CRITICAL(&mux);snap.owner_busy=true;portEXIT_CRITICAL(&mux);
  sync_cleanup_queued=false;
  const auto now=micros();
  if(sync_cleanup_requested&&(!sync_pending||sync_cancelled||
      now>=sync_drain_until-(2000000-G1Window::SyncCancelGrace))){
    sync_cleanup_requested=false;cancel_sync_worker(now);
  }
  discipline_scope(micros());publish(micros());portENTER_CRITICAL(&mux);snap.owner_busy=false;portEXIT_CRITICAL(&mux);
}
bool reset_aux_quiet(){
  // Shared reset latches on never-admitted diagnostic paths remain sticky.
  // They cannot masquerade as an active legacy owner or get cleared globally.
  const auto rx_s=transport_status();const auto tx_s=diagnostic_tx_status();const auto d=direct_status();
  return rx_s.host_ready&&!rx_s.radio_requested&&!rx_s.command_pending&&
    !rx_s.client.scan&&!rx_s.client.pending&&!rx_s.client.synced&&!rx_s.lease.generation&&
    !rx_s.api_errors&&!rx_s.queue_drops&&
    !tx_s.requested&&!tx_s.periodic&&!tx_s.discovery&&!tx_s.owner_busy&&tx_s.off_acknowledged&&
    !tx_s.lease.generation&&!tx_s.api_errors&&
    (d.stage==DirectStage::Off||d.stage==DirectStage::Fault)&&!d.incarnation&&d.handle==None&&
    !d.encrypted&&!d.confirmed&&!d.api_errors&&!d.crypto_errors&&!d.queue_drops;
}
void recovery_poll(uint64_t now){
  if(recovery.stage!=G1ResetRecovery::AwaitDrain&&recovery.stage!=G1ResetRecovery::Quiet)return;
  if(boot_client_cancelled){recovery.refuse();admission_trace(29,None,1);return;}
  const auto capture=periodic_rx::status();
  const bool closed=host_ready&&mode==R2Mode::Off&&!closing&&!command_pending&&!stop_pending&&client.off()&&
    !owners_active()&&!sync_cleanup_queued&&!sync_cleanup_requested&&sync_handle==None&&
    !ble_gap_disc_active()&&!ble_gap_conn_active()&&!capture.installed&&!capture.armed&&!capture.fault&&reset_aux_quiet();
  if(!closed)return;
  if(recovery.stage==G1ResetRecovery::AwaitDrain){
    // Published API returns EALREADY before issuing any HCI command iff the
    // pending-create cache is empty. Successful startup reset alone does not
    // prove this pinned SDK's periodic cache empty. Any other result refuses.
    const int rc=ble_gap_periodic_adv_sync_create_cancel();
    admission_trace(30,None,rc);
    portENTER_CRITICAL(&mux);
    if(rc!=BLE_HS_EALREADY||boot_client_cancelled||!recovery.drained(client,micros()))recovery.refuse();
    portEXIT_CRITICAL(&mux);
    return;
  }
  if(!recovery.due(now))return;
  if(client.generation==UINT32_MAX||recovery.rearms==UINT32_MAX){recovery.refuse();return;}
  uint64_t id=boot_id^(uint64_t(client.generation+1)<<32);if(!id)id=1;
  portENTER_CRITICAL(&mux);
  const bool admitted=!boot_client_cancelled&&!stop_pending&&!command_pending&&host_ready&&
    recovery.due(now)&&recovery.old_generation==client.generation&&client.admit(now,id,true);
  if(admitted){fault=false;requested_mode=R2Mode::Client;command_pending=true;snap.off_ack=false;snap.client=client;recovery.rearmed();}
  portEXIT_CRITICAL(&mux);
  if(!admitted){recovery.refuse();admission_trace(29,None,2);return;}
  admission_trace(28,None,int(client.generation));
}
void r2_host_poll(){if(!initialized)return;const auto begin=micros();bool start=false,stop=false;R2Mode selected=R2Mode::Off;portENTER_CRITICAL(&mux);snap.owner_busy=true;stop=stop_pending;stop_pending=false;if(command_pending){selected=requested_mode;start=!stop&&(selected==R2Mode::Client?client.start(begin):lease.start(begin));command_pending=false;}portEXIT_CRITICAL(&mux);
  if(start){mode=selected;active_used|=mode==R2Mode::Client;
    if(mode==R2Mode::Client){
      if(clear_parent_history){parent_selection.clear_history();parent_failure={};clear_parent_history=false;}
      // Read-only public getters in the worker, once per activation. No
      // callback/ISR query, PA control or transmit power change is introduced.
      static_assert(ESP_PWR_LVL_N24==0&&ESP_PWR_LVL_P18==14&&ESP_PWR_LVL_P20==15,"Pinned S3 public power mapping changed");
      const auto power=[](esp_ble_enhanced_power_type_t type){const int level=int(esp_ble_tx_power_get_enhanced(type,0));return level>=0&&level<=15?int8_t(level==15?20:-24+3*level):int8_t(127);};
      diagnostic.default_tx_dbm=power(ESP_BLE_ENHANCED_PWR_TYPE_DEFAULT);diagnostic.scan_tx_dbm=power(ESP_BLE_ENHANCED_PWR_TYPE_SCAN);diagnostic.init_tx_dbm=power(ESP_BLE_ENHANCED_PWR_TYPE_INIT);
    }
    if(mode==R2Mode::Client&&client.generation>1){
      // An explicit restart is a new local activation, never a renewal of the
      // old stop deadline. Preserve the previous window's full quiet interval.
      radio_resume_at=window_start==UINT64_MAX?begin:window_start+G1Window::Cycle;
      anchor={};direct_session.clear();rc_client.fence();parent_selection.reset();parent_selection.diagnostic(exclude_direct_gm);parent_selection.target(parent_diagnostic.target,parent_diagnostic.relay);member=false;admission_root=admission_until=0;joined_peer=joined_boot=last_origin_ns=relay_train=0;
      phase_learning={};coarse_hold.clear();corrected_clock.clear();corrected_phase_aligned=false;
      peer={};sync_identity=0;retry_at=scan_retry_at=scan_retry_until=0;
#ifdef PTOB_RC9
      rc9_client.fence();
#endif
      Incoming in{};while(incoming.pop(in)){};Discovery d{};while(discoveries.pop(d)){};
      Periodic p{};while(periodic.pop(p)){};
    }
#ifdef V12_R2_RX_SHADOW
    if(!periodic_rx::start())fault=true;
#endif
  }else if(selected!=R2Mode::Off){portENTER_CRITICAL(&mux);if(selected==R2Mode::Client)client.stopped(begin);else lease.stopped(begin);portEXIT_CRITICAL(&mux);}
  portENTER_CRITICAL(&mux);const bool failed=client.deadline_exceeded||parent_diagnostic.state==4;const bool rebind=parent_diagnostic.pending();const auto deadline=lease.deadline_us;portEXIT_CRITICAL(&mux);if(failed)fault=true;
  if(stop||(mode!=R2Mode::Off&&(fault||(mode!=R2Mode::Client&&begin>=deadline-2000000))))begin_close(begin);
  if(fault&&relay_owned())relay_close(begin);
  periodic_rx::poll();
  if(closing)close_poll(begin);else if(rebind)parent_diagnostic_poll(begin);else if(mode!=R2Mode::Off&&begin>=next_poll){next_poll=begin+2000;active_poll(begin);}
  recovery_poll(micros());
  periodic_rx::poll();discipline_scope(micros());if(periodic_rx::status().fault&&!closing){fault=true;begin_close(micros());}
#ifdef V12_R2_RX_SHADOW
  if(begin>=host_stack_read_us){host_stack_read_us=begin+1000000;const auto available=uint32_t(uxTaskGetStackHighWaterMark(nullptr)*sizeof(StackType_t));if(available<host_stack_min)host_stack_min=available;}
#endif
  publish(begin);portENTER_CRITICAL(&mux);snap.owner_busy=false;portEXIT_CRITICAL(&mux);
}
bool r2_request(R2Mode next,uint32_t ms,uint64_t id){if(!initialized||next!=R2Mode::Passive||ms<4000||ms>10000)return false;const auto rx_s=transport_status();const auto tx_s=diagnostic_tx_status();const auto direct_s=direct_status();if(!rx_s.off_acknowledged||rx_s.radio_requested||tx_s.requested||!tx_s.off_acknowledged||direct_s.stage!=DirectStage::Off)return false;portENTER_CRITICAL(&mux);const bool ok=host_ready&&client.off()&&snap.off_ack&&!snap.owner_busy&&!fault&&!command_pending&&!stop_pending&&lease.admit(micros(),ms,id);if(ok){requested_mode=next;command_pending=true;snap.off_ack=false;snap.lease=lease;}portEXIT_CRITICAL(&mux);return ok;}
bool r2_client_start_profile(uint64_t id,uint8_t percent){if(!initialized||(percent!=50&&percent!=100))return false;const auto rx_s=transport_status();const auto tx_s=diagnostic_tx_status();const auto direct_s=direct_status();if(!rx_s.off_acknowledged||rx_s.radio_requested||tx_s.requested||!tx_s.off_acknowledged||direct_s.stage!=DirectStage::Off)return false;portENTER_CRITICAL(&mux);const bool ok=host_ready&&snap.off_ack&&!snap.owner_busy&&!fault&&!command_pending&&!stop_pending&&client.admit(micros(),id,true);if(ok){clear_parent_history=listening_percent!=percent;listening_percent=percent;snap.g1_listening_percent=percent;requested_mode=R2Mode::Client;command_pending=true;snap.off_ack=false;snap.client=client;}portEXIT_CRITICAL(&mux);return ok;}
bool r2_client_start(uint64_t id){return r2_client_start_profile(id,V12_R2_G1_LISTENING_PERCENT);}
bool r2_parent_select(uint64_t id,bool relay){
  if(!initialized||id==provision.identity)return false;
  portENTER_CRITICAL(&mux);
  const bool ok=host_ready&&mode==R2Mode::Client&&client.running&&!client.stopping&&
    !client.deadline_exceeded&&!fault&&!closing&&!stop_pending&&!command_pending&&
    !(id&&!relay&&exclude_direct_gm)&&
    parent_diagnostic.admit(id,micros(),relay);
#ifdef PTOB_RC9
  if(ok)rc9_target=0;
#endif
  portEXIT_CRITICAL(&mux);return ok;
}
bool r2_rc_parent(uint64_t id){return r2_parent_select(id,true);}
bool r2_gm_parent(uint64_t id){return r2_parent_select(id,false);}
bool r2_rc9_parent(uint64_t id){
#ifdef PTOB_RC9
 if(!initialized||id==provision.identity)return false;
 portENTER_CRITICAL(&mux);
 const bool ok=host_ready&&mode==R2Mode::Client&&client.running&&!client.stopping&&
  !client.deadline_exceeded&&!fault&&!closing&&!stop_pending&&!command_pending&&parent_diagnostic.admit(id,micros(),true);
 if(ok)rc9_target=id;
 portEXIT_CRITICAL(&mux);return ok;
#else
 (void)id;return false;
#endif
}
Rc9Status r2_rc9_status(){
 Rc9Status result;
#ifdef PTOB_RC9
 portENTER_CRITICAL(&mux);result=rc9_published;
 const bool revoked=fault||closing||stop_pending||mode!=R2Mode::Client||parent_diagnostic.pending()||parent_diagnostic.state==4;
 result.fault|=fault;portEXIT_CRITICAL(&mux);
 if(revoked){result.valid=result.following=result.serving=false;result.utc_us=result.until_us=0;}
 const auto now=micros();
 if(now<result.sampled_us||now-result.sampled_us>=250000){
  result.valid=result.following=result.serving=false;result.utc_us=result.until_us=0;
  // A stale display snapshot withdraws UTC, never restarts or shortens the
  // original authenticated client lease that pins RC8 reception.
  if(now>=result.client_until_us){result.clients=0;result.client_until_us=0;}
  return result;
 }
 if(!result.valid||now>=result.until_us||now-result.sampled_us>UINT64_MAX-result.utc_us){
  result.valid=result.following=false;result.utc_us=result.until_us=0;
 }else{result.utc_us+=now-result.sampled_us;result.sampled_us=now;}
#endif
 return result;
}
bool r2_set_relay_only(bool enabled){
  if(!initialized)return false;
  portENTER_CRITICAL(&mux);
  bool ok=host_ready&&mode==R2Mode::Client&&client.running&&!client.stopping&&
    !client.deadline_exceeded&&!fault&&!closing&&!stop_pending&&!command_pending&&
    !parent_diagnostic.pending()&&parent_diagnostic.state!=4;
  if(ok&&(enabled!=exclude_direct_gm||parent_diagnostic.target)){
    ok=parent_diagnostic.admit(0,micros(),true);
    if(ok){relay_only_requested=enabled;upstream_mode_change=true;}
  }
  portEXIT_CRITICAL(&mux);return ok;
}
void r2_stop(){if(!initialized)return;portENTER_CRITICAL(&mux);
  boot_client_cancelled=true;if(recovery.eligible)recovery.refuse();snap.requested=false;
  const bool changing=upstream_mode_change&&parent_diagnostic.pending();
  const auto original=changing?parent_diagnostic.deadline_us:0;
  if(changing){
    // Explicit OFF cancels the requested policy, not the already running
    // drain. Its earlier deadline still bounds the eventual OFF completion.
    upstream_mode_change=false;relay_only_requested=exclude_direct_gm;
    parent_diagnostic.state=0;parent_diagnostic.deadline_us=parent_diagnostic.completed_us=0;
  }
  if(client.pending||client.running||client.stopping){
    // Reuse the actual earlier mode-request admission timestamp, already
    // recorded by RcParentDiagnostic, for the common first drain. This keeps
    // the client ledger's exact2s shape without extending that drain on OFF.
    client.stop(changing?original-2000000:micros());
    stop_pending=true;snap.off_ack=false;
  }else if(lease.busy||lease.pending||command_pending){lease.stop_request();stop_pending=true;snap.off_ack=false;}
  portEXIT_CRITICAL(&mux);
}
void r2_observe(uint64_t now){if(!initialized)return;portENTER_CRITICAL(&mux);if(parent_diagnostic.observe(now)||lease.observe(now,2000000)||client.observe(now)){stop_pending=true;snap.off_ack=false;snap.requested=false;}snap.lease=lease;snap.client=client;portEXIT_CRITICAL(&mux);}
R2Status r2_status(){
  portENTER_CRITICAL(&mux);auto result=snap;result.rc_parent=parent_diagnostic;
  result.rc7_exclude_direct_gm=result.rc7_relay_only=exclude_direct_gm;
  result.rc7_relay_only_requested=relay_only_requested;result.rc7_upstream_mode_pending=upstream_mode_change&&parent_diagnostic.pending();
  portEXIT_CRITICAL(&mux);const auto now=micros();
  if(result.rc_parent.pending()||result.rc_parent.state==4){result.clock_valid=result.following=false;result.rc7_bound=false;result.rc7_depth=0;result.joined_peer=result.joined_boot=0;}
  if(!result.rc7_bound)rc7_path_snapshot::clear(result.rc7_path_hex);
  // Main/console reads must fence a signed source even while the host is
  // blocked inside an SDK HCI command. Never grant new scope or model lifetime.
  const auto age=now>=result.follower_snapshot_us?now-result.follower_snapshot_us:UINT64_MAX;
  const auto growth=age==UINT64_MAX?UINT64_MAX:age/500+unsigned(age%500!=0);
  const bool guard_ok=growth<=UINT64_MAX-result.g1_anchor_guard_us;
  const bool coarse=result.requested&&!result.fault&&result.clock_valid&&!result.g1_retained_estimate&&age!=UINT64_MAX&&
    now<result.g1_anchor_deadline_us&&guard_ok&&result.g1_anchor_guard_us+growth<249900&&
    age<=UINT64_MAX-result.g1_coarse_snapshot_utc_us;
  result.clock_valid=coarse;
  if(!coarse){result.g1_clock_origin=0;result.g1_clock_model={};result.g1_relative_quality={};}
  result.g1_retained_estimate=false;
  result.following=coarse&&result.following&&result.service_until_us&&now<result.service_until_us;
  if(!result.following){result.root_identity=result.root_boot=result.service_until_us=0;result.source_epoch=result.publication=0;result.root=255;}
  bool model=result.following&&result.precision_fit&&result.model_available20&&result.g1_model_until_us&&now<result.g1_model_until_us;
  result.follower_utc_us=coarse?result.g1_coarse_snapshot_utc_us+age:0;
#ifdef V12_R2_RX_SHADOW
  if(coarse&&result.g1_clock_model.tick){uint64_t tick=0;if(clock_reference_read(&tick)&&tick>=result.g1_clock_model.tick){const auto ns=result.g1_clock_model.at(tick);if(ns>0){result.follower_utc_us=uint64_t(ns)/1000;result.g1_relative_quality.evaluated20=tick;}else result.g1_relative_quality={};}else result.g1_relative_quality={};}
  if(!coarse&&result.requested&&!result.fault&&!result.rc_parent.pending()&&result.rc_parent.state!=4){
    uint64_t tick=0,utc=0;
    if(clock_reference_read(&tick)&&result.g1_retained_clock.deliver(tick,utc)){
      result.clock_valid=true;result.follower_utc_us=utc;result.g1_retained_estimate=true;
      result.g1_clock_origin=result.g1_retained_clock.root;result.g1_clock_model=result.g1_retained_clock.clock;
      result.g1_relative_quality={};result.g1_relative_quality.evaluated20=tick;
    }
  }
  if(model){uint64_t tick=0;model=clock_reference_read(&tick)&&tick>=result.model_available20;
    if(model){const auto ns=result.precision_model.at(tick);model=ns>0;if(model)result.follower_utc_us=uint64_t(ns)/1000;}}
#else
  model=false;
#endif
  if(!model){result.model_available20=0;result.precision_model={};result.precision_fit=false;result.precision_ns=result.rms_ns=UINT32_MAX;result.hold_ms=result.hold_samples=0;}
  if(coarse){result.g1_anchor_guard_us+=growth;}
  result.follower_snapshot_us=now;return result;
}
bool r2_parent_event(uint32_t sequence,G1ParentEvent& event){
  portENTER_CRITICAL(&mux);const bool found=parent_journal.get(sequence,event);portEXIT_CRITICAL(&mux);return found;
}
bool r2_parent_next(uint32_t after,G1ParentEvent& event){
  portENTER_CRITICAL(&mux);const bool found=parent_journal.next(after,event);portEXIT_CRITICAL(&mux);return found;
}
R2Activity r2_activity(){
  R2Activity result;
  portENTER_CRITICAL(&mux);
  // These are reported controller callbacks, including rejected reports, not
  // a count of unreported physical receptions or time-authorized updates.
  result.rx=snap.g1_adv_callbacks+snap.pa_reports;
  result.requested=snap.requested;result.fault=snap.fault;
  result.mode_pending=parent_diagnostic.pending();result.relay_only=relay_only_requested;
  portEXIT_CRITICAL(&mux);
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
  const auto tx=periodic_tx::status();result.tx=tx.calls;
  result.tx_armed=tx.installed&&tx.armed&&periodic_tx::estimate_owner_active();
#endif
  return result;
}
void r2_host_reset(int reason){
  portENTER_CRITICAL(&mux);recovery.reset(reason,client.generation,micros(),!boot_client_cancelled);host_ready=false;fault=snap.fault=true;
  // Reset is a fault closure, not an operator OFF/update intent.
  if(client.pending||client.running||client.stopping){client.stop(micros());stop_pending=true;snap.off_ack=false;}
  portEXIT_CRITICAL(&mux);
  admission_trace(26,None,reason);
}
void r2_host_reset(){r2_host_reset(0);}
// Physical GPS/PPS remains a shadow witness in the main task. Never fit input.
void r2_nmea(const uint8_t*,unsigned,uint64_t){}
void r2_pps(uint64_t,uint64_t,uint64_t,bool){}
}
