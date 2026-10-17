#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
uint64_t fake_now=1000;
void test_critical_enter(void*);
#define V12_TEST_CRITICAL_ENTER(x) test_critical_enter(x)
#include "../main/native_g1.cpp"
namespace {
bool scan_active=false,small_mtu=false,rx_blocked=false,late_disconnect=false,marker_bad=false;int scan_error=0;
bool cleanup_scheduled=false,sync_cancel_timeout=false;unsigned sync_cancel_calls=0;
bool residual_sync_cache=false,stop_in_cache_check=false,arm_late_stop=false,stop_at_admit=false,aux_active=false;
unsigned connected=0,terminated=0;void* conn_arg=nullptr;bool inline_failure=false,inline_success=false,cancel_missing=false,hidden_link=false,sync_late=false,sync_cancel_disallowed=false,sync_cancel_inline=false;unsigned cancel_calls=0;std::vector<std::vector<uint8_t>> output;
}
namespace v12 {
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
LogStatus fixture_log_status{};
LogStatus log_status(){return fixture_log_status;}
#endif
void transport_g1_cleanup_schedule(){assert(!cleanup_scheduled);cleanup_scheduled=true;}
TransportStatus transport_status(){TransportStatus s;s.off_acknowledged=!rx_blocked;s.host_ready=true;s.radio_requested=aux_active;
  if(arm_late_stop&&recovery.stage==G1ResetRecovery::Quiet&&fake_now>=recovery.quiet_until){arm_late_stop=false;stop_at_admit=true;}return s;}
DiagnosticTxStatus diagnostic_tx_status(){return {};}
DirectStatus direct_status(){return {};}
Report discovery_report;
std::vector<Report> diagnostic_records;
void transport_record(const Report& r){if(r.kind==5)discovery_report=r;if(r.kind==8||r.kind==10||r.kind==20)diagnostic_records.push_back(r);}
}
int ble_hs_id_set_rnd(const uint8_t*){return 0;}
int ble_gatts_count_cfg(const ble_gatt_svc_def*){return 0;}int ble_gatts_add_svcs(const ble_gatt_svc_def*){return 0;}
int ble_gap_ext_disc(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,const ble_gap_ext_disc_params* p,void*,int(*)(ble_gap_event*,void*),void*){assert(p->passive==1&&p->window==32&&p->itvl==32);if(scan_error)return scan_error;scan_active=true;return 0;}
int ble_gap_disc_active(){return scan_active;}int ble_gap_disc_cancel(){scan_active=false;return 0;}
int ble_gap_connect(unsigned,const ble_addr_t*,unsigned t,const ble_gap_conn_params* params,int(*cb)(ble_gap_event*,void*),void* arg){assert(t==BLE_HS_FOREVER);assert(params->scan_itvl==32&&params->scan_window==32&&params->itvl_min==6&&params->itvl_max==6&&params->supervision_timeout==10);++connected;conn_arg=arg;if(inline_failure||inline_success){ble_gap_event e;e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=inline_failure?1:0;cb(&e,arg);}return 0;}
int ble_gap_conn_find(uint16_t,ble_gap_conn_desc* d){d->peer_id_addr=v12::peer.address;return 0;}
int ble_gap_conn_active(){return !hidden_link&&v12::peer.connecting;}
int ble_gap_conn_find_by_addr(const ble_addr_t* a,ble_gap_conn_desc* d){if(!hidden_link)return BLE_HS_ENOTCONN;d->peer_id_addr=*a;return 0;}
int ble_gap_conn_cancel(){++cancel_calls;if(cancel_missing)return 0;ble_gap_event e;e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=1;v12::gap(&e,conn_arg);return 0;}
int ble_gap_terminate(uint16_t,int){++terminated;if(!late_disconnect){ble_gap_event e;e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.peer_id_addr=v12::peer.address;v12::gap(&e,conn_arg);}return 0;}
int ble_gap_set_data_len(uint16_t,uint16_t,uint16_t){assert(false&&"Transient admission must use ordinary LL fragmentation");return BLE_HS_HCI_ERR(0x0c);}
int ble_gattc_exchange_mtu(uint16_t h,int(*cb)(uint16_t,const ble_gatt_error*,uint16_t,void*),void* arg){ble_gatt_error e;cb(h,&e,247,arg);return 0;}
uint16_t ble_att_mtu(uint16_t){return small_mtu?163:247;}
int ble_gattc_read_by_uuid(uint16_t h,uint16_t first,uint16_t last,const ble_uuid_t* uuid,int(*cb)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void* arg){
  assert(first==1&&last==UINT16_MAX&&!ble_uuid_cmp(uuid,&v12::data_uuid.u));os_mbuf b;b.bytes={'R','2','G','1',uint8_t(marker_bad?2:1)};
  ble_gatt_attr a;a.om=&b;ble_gatt_error e;if(!cb(h,&e,&a,arg)){e.status=BLE_HS_EDONE;cb(h,&e,nullptr,arg);}return 0;
}
int ble_gattc_disc_svc_by_uuid(uint16_t h,const ble_uuid_t*,int(*cb)(uint16_t,const ble_gatt_error*,const ble_gatt_svc*,void*),void* arg){ble_gatt_error e;ble_gatt_svc s;if(!cb(h,&e,&s,arg)){e.status=BLE_HS_EDONE;cb(h,&e,nullptr,arg);}return 0;}
int ble_gattc_disc_all_chrs(uint16_t h,uint16_t,uint16_t,int(*cb)(uint16_t,const ble_gatt_error*,const ble_gatt_chr*,void*),void* arg){ble_gatt_error e;ble_gatt_chr c;c.uuid.u128=v12::data_uuid;if(!cb(h,&e,&c,arg)){e.status=BLE_HS_EDONE;cb(h,&e,nullptr,arg);}return 0;}
int ble_gattc_write_no_rsp_flat(uint16_t,uint16_t,const void* p,unsigned n){assert(n==R2_GROUP_JOIN_BYTES);auto* b=static_cast<const uint8_t*>(p);output.emplace_back(b,b+n);return 0;}
int ble_gattc_read(uint16_t,uint16_t,int(*)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void*){return BLE_HS_ENOTCONN;}
int ble_gattc_write_flat(uint16_t,uint16_t,const void*,uint16_t,int(*)(uint16_t,const ble_gatt_error*,ble_gatt_attr*,void*),void*){return BLE_HS_ENOTCONN;}
int ble_gap_periodic_adv_sync_create(const ble_addr_t*,uint8_t sid,const ble_gap_periodic_sync_params*,int(*)(ble_gap_event*,void*),void*){assert(sid==12);return 0;}
void sync_established(){ble_gap_event e;e.type=BLE_GAP_EVENT_PERIODIC_SYNC;e.periodic_sync.per_adv_ival=50;e.periodic_sync.adv_addr=v12::sync_address;v12::gap(&e,nullptr);}
int ble_gap_periodic_adv_sync_create_cancel(){++sync_cancel_calls;if(!v12::sync_pending){if(stop_in_cache_check){stop_in_cache_check=false;v12::r2_stop();}return residual_sync_cache?BLE_HS_HCI_ERR(0x0c):BLE_HS_EALREADY;}if(sync_cancel_timeout){fake_now+=2000000;return BLE_HS_ETIMEOUT_HCI;}if(sync_cancel_disallowed){if(sync_cancel_inline)sync_established();return BLE_HS_HCI_ERR(0x0c);}ble_gap_event e;e.type=BLE_GAP_EVENT_PERIODIC_SYNC;e.periodic_sync.status=1;v12::gap(&e,nullptr);return 0;}
int ble_gap_periodic_adv_sync_terminate(uint16_t){if(sync_late)return 0;ble_gap_event e;e.type=BLE_GAP_EVENT_PERIODIC_SYNC_LOST;v12::gap(&e,nullptr);return 0;}
void pump_cleanup(){if(cleanup_scheduled){cleanup_scheduled=false;v12::r2_host_cleanup();}}
void test_critical_enter(void* m){if(stop_at_admit&&m==&v12::mux){stop_at_admit=false;v12::r2_stop();}}
void step(unsigned n=1){for(unsigned i=0;i<n;i++){fake_now+=2000;v12::r2_observe(fake_now);v12::r2_host_poll();pump_cleanup();}}
void inject(const r2_join_record& r,bool corrupt=false){os_mbuf b;b.bytes.resize(R2_GROUP_JOIN_BYTES);assert(!r2_group_join_encode(b.bytes.data(),&r,&v12::provision.group));if(corrupt)b.bytes[145]^=1;ble_gatt_access_ctxt ctx;ctx.om=&b;assert(!v12::access(22,0,&ctx,nullptr));step();}
void discovery(bool pending_only=false){v12::Discovery d{};d.props=1;d.length=28;d.bytes[0]=2;d.bytes[1]=1;d.bytes[2]=6;d.bytes[3]=24;d.bytes[4]=0xff;auto* p=d.bytes+5;p[0]='G';p[1]='1';p[2]=1;r2_group_put(p+3,v12::provision.group.id,8);r2_group_put(p+11,1,4);r2_group_put(p+15,111,8);d.address=v12::canonical(111);assert(v12::discovery_identity(d)==111);r2_group_put(p+15,UINT64_C(0x0000000000000001),8);d.address=v12::canonical(UINT64_C(0x0000000000000001));assert(v12::discovery_identity(d));d.address.val[0]^=1;assert(!v12::discovery_identity(d));d.address.val[0]^=1;p[2]|=64;assert(!v12::discovery_identity(d));p[2]&=~64;d.at=fake_now;v12::discoveries.push(d);step();assert(connected==1);ble_gap_event e;e.type=BLE_GAP_EVENT_CONNECT;if(!pending_only&&!inline_failure&&!inline_success)v12::gap(&e,conn_arg);step(3);}
void fresh_direct(uint64_t identity,uint64_t boot,uint32_t source,uint32_t train){
  const auto before=output.size();v12::Discovery d{};d.props=1;d.length=25;d.bytes[0]=24;d.bytes[1]=0xff;
  auto* b=d.bytes+2;b[0]='G';b[1]='1';b[2]=1;r2_group_put(b+3,v12::provision.group.id,8);r2_group_put(b+11,1,4);r2_group_put(b+15,identity,8);
  d.address=v12::canonical(identity);d.at=fake_now;v12::discoveries.push(d);step();assert(v12::peer.connecting);
  ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;v12::gap(&e,conn_arg);step(3);
  assert(output.size()==before+1);r2_join_record request{};assert(!r2_group_join_decode(&request,output.back().data(),R2_GROUP_JOIN_BYTES,&v12::provision.group));
  r2_join_record challenge{};challenge.kind=R2_JOIN_CHALLENGE;challenge.sender=identity;challenge.receiver=v12::provision.identity;
  challenge.sender_boot=boot;challenge.receiver_boot=22;challenge.client_nonce=request.client_nonce;challenge.server_nonce=99;
  challenge.sequence=1;challenge.created_us=fake_now+10000;challenge.echo_us=request.created_us;std::strcpy(challenge.name,"CurrentGM");inject(challenge);
  assert(output.size()==before+2);r2_join_record proof{};assert(!r2_group_join_decode(&proof,output.back().data(),R2_GROUP_JOIN_BYTES,&v12::provision.group));
  auto accept=challenge;accept.kind=R2_JOIN_ACCEPT;accept.sequence=2;accept.created_us+=10000;accept.echo_us=proof.created_us;
  accept.source_flags=1;accept.source_bound_us=40000;accept.root=identity;accept.root_boot=boot;accept.source_epoch=source;accept.publication=train;accept.utc_us=1700000000000000+fake_now;accept.remaining_us=250000;
  inject(accept);const auto state=v12::r2_status();assert(state.following&&state.root_identity==identity&&state.root_boot==boot&&state.source_epoch==source&&state.publication==train&&state.clock_valid&&!state.fault);
}
int main(int argc,char** argv){assert(argc==2);std::string which=argv[1];
  const uint8_t c1[]={2,0,0,0,0,5},c2[]={2,0,0,0,0,6},c3[]={2,0,0,0,0,7};
  const bool is_c3=!std::strcmp(V12_UNIT,"C3"),is_c2=!std::strcmp(V12_UNIT,"C2");const auto expected=is_c3?c3:is_c2?c2:c1;assert(!v12::r2_init(22,is_c3||is_c2?c1:c3));assert(v12::r2_init(22,expected));assert(!std::strcmp(v12::native_device_binding()->label,is_c3?"C-3":is_c2?"C-2":"C-1"));assert(v12::r2_host_init());
  if(which=="cancel")v12::r2_stop();if(which=="blocked")rx_blocked=true;assert(v12::r2_host_sync()==(which!="blocked"));
  if(which=="late"){fake_now=v12::client.start_deadline_us;v12::r2_observe(fake_now);}
  if(which=="scan-transient"||which=="scan-timeout")scan_error=BLE_HS_EBUSY;
  step();assert(!v12::r2_status().serving&&!v12::r2_status().source);assert(!v12::r2_request(v12::R2Mode::Active,10000,44));
  if(which=="cancel"){assert(v12::r2_status().off_ack);return 0;}if(which=="blocked"||which=="late"){assert(v12::r2_status().fault);return 0;}
  assert(v12::r2_status().client.running);
  if(which=="listening-profile"||which=="sync-cancel-grace"){
    assert(v12::r2_status().g1_listening_percent==V12_R2_G1_LISTENING_PERCENT);
    if(which=="listening-profile"){
      const auto generation=v12::client.generation;
      assert(!v12::r2_client_start_profile(90,100)&&!v12::r2_client_start_profile(90,75));
      assert(v12::client.generation==generation&&v12::listening_percent==V12_R2_G1_LISTENING_PERCENT);
      v12::r2_stop();step(6);assert(v12::r2_status().off_ack);
      assert(!v12::r2_client_start_profile(91,0));assert(v12::r2_client_start_profile(92,100));
      assert(!v12::r2_client_start_profile(93,50));step();assert(v12::client.generation==generation+1&&v12::listening_percent==100);
    }
    if(which=="sync-cancel-grace"){fake_now=450000;v12::active_poll(fake_now);pump_cleanup();assert(!v12::owners_active());}
    fake_now=1000000;v12::anchor={true,111,55,1700000000000000,0,3000000,2,3,40000};
    v12::joined_peer=111;v12::joined_boot=55;v12::sync_address=v12::canonical(111);v12::active_poll(fake_now);assert(v12::sync_pending);
    if(which=="sync-cancel-grace"){
      fake_now=1450000;v12::active_poll(fake_now);pump_cleanup();const auto deadline=v12::sync_drain_until;
      assert(v12::sync_pending&&v12::sync_cleanup_requested&&!v12::sync_cancelled&&!sync_cancel_calls);
      fake_now+=1000;sync_established();v12::active_poll(fake_now);pump_cleanup();
      assert(!v12::owners_active()&&!sync_cancel_calls&&!v12::fault&&v12::sync_drain_until==deadline);
      fake_now=1500000;v12::active_poll(fake_now);assert(!v12::quiet_faults);
    }else{
      sync_established();const auto epoch=v12::sync_capture_epoch;
      for(const uint64_t at:{1450000,1600000,2000000,2400000}){fake_now=at;v12::active_poll(fake_now);pump_cleanup();assert(v12::synced&&v12::sync_capture_epoch==epoch&&!sync_cancel_calls&&!v12::quiet_faults&&!v12::fault);}
      fake_now=2700000;uint8_t packet[190]{};ble_gap_event e{};e.type=BLE_GAP_EVENT_PERIODIC_REPORT;e.periodic_report.sync_handle=v12::sync_handle;e.periodic_report.data_status=BLE_HCI_PERIODIC_DATA_STATUS_COMPLETE;e.periodic_report.data_length=190;e.periodic_report.data=packet;
      const auto reports=v12::pa_reports;v12::gap(&e,nullptr);assert(v12::pa_reports==reports+1&&v12::periodic.count==1); // Phase700ms beyond half-window still received.
    }
    v12::r2_stop();step(6);assert(v12::r2_status().off_ack&&!v12::anchor.source&&!v12::corrected_clock.prediction().tick);
    if(which=="listening-profile"){assert(v12::r2_client_start_profile(94,50));step();assert(v12::listening_percent==50);fake_now=3000000;v12::active_poll(fake_now);fake_now=3450000;v12::active_poll(fake_now);pump_cleanup();assert(!v12::owners_active()&&!v12::quiet_faults);v12::r2_stop();step(6);assert(v12::r2_status().off_ack);}
    return 0;
  }
  if(which=="rebootstrap-retains-acquisition"){
    fake_now=450000;v12::active_poll(fake_now);pump_cleanup();assert(!v12::owners_active());
    v12::anchor={true,111,55,1700000000000000,0,1000000,2,3,40000};
    v12::joined_peer=111;v12::joined_boot=55;v12::sync_address=v12::canonical(111);v12::member=true;
    fake_now=3000000;v12::active_poll(fake_now);
    assert(v12::anchor.source&&v12::anchor.at==0&&v12::anchor.until==1000000&&v12::anchor.rebootstrap(fake_now));
    assert(!v12::following(fake_now)&&!v12::member&&!v12::sync_pending&&!v12::fault);
    fake_now=3100000;v12::active_poll(fake_now);assert(v12::sync_pending&&v12::sync_identity==111);
    assert(v12::anchor.until==1000000&&!v12::following(fake_now));
    fake_now=3450000;v12::active_poll(fake_now);pump_cleanup();
    v12::r2_stop();step(6);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="known-train-first"||which=="known-train-refresh"||which=="known-train-unbound"){
    fake_now=450000;v12::active_poll(fake_now);pump_cleanup();assert(!v12::owners_active());
    v12::anchor={true,111,55,1700000000000000,0,2000000,2,3,40000};
    v12::joined_peer=111;v12::joined_boot=55;v12::sync_address=v12::canonical(111);
    if(which=="known-train-refresh")v12::anchor.uncertainty=125000;
    if(which=="known-train-unbound")v12::joined_boot=56;
    fake_now=1000000;v12::active_poll(fake_now);
    const bool acquire=which=="known-train-first";
    assert(v12::sync_pending==acquire&&!connected&&!v12::fault);
    if(acquire){
      assert(v12::sync_requested_at==fake_now&&v12::sync_owner_window_start==fake_now&&v12::sync_identity==111);
      sync_established();fake_now=1450000;v12::active_poll(fake_now);pump_cleanup();v12::active_poll(fake_now);
      assert(!v12::owners_active()&&v12::window_closed_at==fake_now&&!v12::quiet_faults);
      fake_now=1999999;v12::active_poll(fake_now);assert(!v12::owners_active());
      fake_now=2000000;v12::active_poll(fake_now);assert(v12::sync_pending); // Acquisition may continue after signed expiry, never FOLLOW.
      assert(!v12::following(fake_now)&&v12::anchor.valid(fake_now));
    }
    v12::r2_stop();step(6);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="persistent-no-GM"){
    for(unsigned i=0;i<900000;i++)step(); // Thirty minutes, no finite client lease or GM-dependent lifetime.
    const auto s=v12::r2_status();assert(s.client.running&&s.client.generation==1&&!s.fault&&!s.following&&!s.clock_valid&&!s.serving&&!s.source&&s.client.stop_generation==0);
    assert(!connected&&!s.g1_quiet_faults);return 0;
  }
  if(which.starts_with("reset-recovery")){
    const unsigned cycles=which=="reset-recovery-repeated"?4:1;
    for(unsigned cycle=0;cycle<cycles;cycle++){
      // Actual original API-timeout path, retained quiet failure, then the
      // SDK's real failed-SYNC callback before reset/startup synchronization.
      v12::window_start=v12::sync_owner_window_start=fake_now;v12::sync_requested_at=fake_now+300000;v12::sync_drain_until=0;
      v12::sync_identity=111;v12::sync_address=v12::canonical(111);v12::sync_pending=true;
      fake_now+=450000;sync_cancel_timeout=true;const auto cancels_before=sync_cancel_calls;v12::cancel_sync(fake_now);pump_cleanup();
      assert(sync_cancel_calls==cancels_before);fake_now+=v12::G1Window::SyncCancelGrace;v12::cancel_sync(fake_now);pump_cleanup();
      assert(v12::fault&&v12::recovery.stage==v12::G1ResetRecovery::AwaitReset);
      const auto first=v12::first_error_at;sync_cancel_timeout=false;
      ble_gap_event failed{};failed.type=BLE_GAP_EVENT_PERIODIC_SYNC;failed.periodic_sync.status=1;v12::gap(&failed,nullptr);
      v12::r2_host_reset(BLE_HS_ETIMEOUT_HCI);
      if(which=="reset-recovery-explicit-off")v12::r2_stop();
      if(which=="reset-recovery-no-sync"){for(unsigned i=0;i<300;i++)step();assert(v12::fault&&!v12::client.running&&v12::recovery.rearms==0);return 0;}
      assert(v12::r2_host_sync());
      if(which=="reset-recovery-residual-cache")residual_sync_cache=true;
      if(which=="reset-recovery-cache-off")stop_in_cache_check=true;
      if(which=="reset-recovery-aux-active")aux_active=true;
      if(which=="reset-recovery-late-drain"){v12::begin_close(fake_now);fake_now=v12::client.stop_deadline_us+1;}
      step(2);const auto generation=v12::client.generation;
      if(which=="reset-recovery-residual-cache"||which=="reset-recovery-explicit-off"||which=="reset-recovery-cache-off"||which=="reset-recovery-late-drain"||which=="reset-recovery-aux-active"){
        step(400);assert(v12::fault&&!v12::client.running&&v12::client.generation==generation&&v12::recovery.rearms==0);return 0;
      }
      assert(v12::recovery.stage==v12::G1ResetRecovery::Quiet&&v12::client.off());
      const auto deadline=v12::recovery.old_stop_deadline,completion=v12::recovery.old_completed;
      assert(completion<deadline&&deadline-v12::recovery.old_stop_requested==2000000);
      fake_now=v12::recovery.quiet_until-1;v12::r2_host_poll();assert(!v12::client.running&&v12::client.generation==generation);
      if(which=="reset-recovery-late-off")arm_late_stop=true;
      fake_now++;v12::r2_host_poll();
      if(which=="reset-recovery-late-off"){step(3);assert(v12::boot_client_cancelled&&!v12::client.running&&v12::client.generation==generation&&!v12::recovery.rearms);return 0;}
      step(2);assert(v12::client.running&&!v12::fault&&v12::client.generation==generation+1&&v12::recovery.rearms==cycle+1);
      assert(v12::recovery.old_stop_deadline==deadline&&v12::recovery.old_completed==completion&&v12::first_error_at==first);
      assert(v12::quiet_faults==(v12::listening_percent==100?0:cycle+1));
      assert(v12::api_errors==(v12::listening_percent==100?1:2)*(cycle+1));
      assert(!v12::boot_client_cancelled&&!v12::anchor.source&&!v12::corrected_clock.prediction().tick);
      assert(v12::client.start_deadline_us-v12::client.admitted_us==2000000&&v12::client.started_us<v12::client.start_deadline_us);
      // Drain normal scan in its original half-duty window before next cycle.
      for(unsigned i=0;i<500;i++)step();
    }
    return 0;
  }
  if(which=="host-stall-source-expiry"){
    fake_now=100000;v12::anchor={true,111,3,1700000000000000,fake_now,250000,1,1,40000};v12::publish(fake_now);
    v12::snap.model_available20=99;v12::snap.g1_model_until_us=200000;v12::snap.precision_fit=true;v12::snap.precision_ns=100;
    fake_now=225000;auto live=v12::r2_status();assert(live.following&&live.root_identity==111&&live.clock_valid&&!live.model_available20&&!live.precision_fit);
    fake_now=260000;auto held=v12::r2_status();assert(!held.following&&!held.root_identity&&!held.root_boot&&!held.source_epoch&&!held.publication&&!held.service_until_us&&!held.model_available20&&held.clock_valid);
    assert(held.follower_utc_us==v12::anchor.utc+fake_now-v12::anchor.at&&held.g1_anchor_deadline_us==v12::snap.g1_anchor_deadline_us&&!v12::coarse_hold.saved.source);
    fake_now=v12::snap.g1_anchor_deadline_us;assert(!v12::r2_status().clock_valid);
    fake_now=99999;assert(!v12::r2_status().clock_valid);
    fake_now=260000;v12::snap.g1_coarse_snapshot_utc_us=UINT64_MAX;assert(!v12::r2_status().clock_valid);
    fake_now=100000;v12::publish(fake_now);v12::r2_stop();assert(!v12::r2_status().following&&!v12::r2_status().clock_valid&&!v12::r2_status().root_identity);return 0;
  }
  if(which=="persistent-root-rotation"){
    const uint64_t ids[]={UINT64_C(0x0000000000000002),UINT64_C(0x0000000000000001),UINT64_C(0x0000000000000003)};
    for(unsigned i=0;i<3;i++){
      if(i){while(fake_now%1000000<999000)step();step();}
      fresh_direct(ids[i],100+i,10+i,20+i);
      const auto old=v12::anchor;while(fake_now<old.until)step();const auto hold=v12::r2_status();
      assert(hold.client.running&&hold.client.generation==1&&hold.clock_valid&&!hold.following&&!hold.root_identity&&!hold.root_boot&&!hold.service_until_us&&!hold.model_available20&&!hold.fault);
      assert(v12::coarse_hold.same(old));
      // Preserve the causal acquisition anchor through T096's1.5s recovery
      // interval; a new candidate is unrestricted once rebootstrap is due.
      while(fake_now<old.until+1500000)step();
    }
    assert(v12::join_accepts==3&&!v12::fault&&!v12::quiet_faults&&v12::sequence==6&&v12::client.generation==1);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack&&!v12::coarse_hold.saved.source);return 0;
  }
  if(which=="sync-tail-delayed"){
    v12::window_start=v12::sync_owner_window_start=0;v12::sync_requested_at=300000;
    v12::sync_pending=true;fake_now=450000;v12::cancel_sync(fake_now);const auto deadline=v12::sync_drain_until;
    fake_now=1451000;pump_cleanup();assert(v12::fault&&v12::quiet_faults==1&&!v12::sync_pending&&v12::sync_drain_until==deadline);
    bool original_window=false;for(const auto& r:v12::diagnostic_records)if(r.bytes[0]==8)original_window|=r2_group_get(r.bytes+8,4)==1451000;
    assert(original_window);return 0;
  }
  if(which=="sync-tail-completion"||which=="sync-command-timeout"){
    v12::window_start=0;v12::sync_identity=111;v12::sync_address=v12::canonical(111);
    v12::sync_requested_at=300000;v12::sync_pending=true;fake_now=450000;
    v12::cancel_sync(fake_now);const auto deadline=v12::sync_drain_until;
    assert(cleanup_scheduled&&v12::sync_pending&&!v12::sync_cancelled&&sync_cancel_calls==0);
    v12::cancel_sync(460000);assert(v12::sync_drain_until==deadline);
    if(which=="sync-tail-completion")sync_established();else {sync_cancel_timeout=true;v12::anchor={true,111,3,1700000000000000,fake_now,3000000,1,1,40000};v12::coarse_hold.retain(v12::anchor);}
    if(sync_cancel_timeout){fake_now+=v12::G1Window::SyncCancelGrace;v12::cancel_sync(fake_now);}
    pump_cleanup();
    if(sync_cancel_timeout){
      assert(v12::fault&&v12::quiet_faults==1&&v12::sync_pending&&v12::sync_drain_until==deadline);
      assert(v12::first_error_api==v12::R2Api::SyncCancel&&v12::first_error_rc==BLE_HS_ETIMEOUT_HCI);
      const auto denied=v12::r2_status();assert(!denied.following&&!denied.clock_valid&&!denied.root_identity&&!denied.service_until_us&&!denied.model_available20);
      sync_established();v12::begin_close(fake_now);v12::close_poll(fake_now);pump_cleanup();v12::close_poll(fake_now);
      assert(v12::mode==v12::R2Mode::Off&&v12::fault&&v12::client.stop_generation==v12::client.stop_completed);
    }else assert(!sync_cancel_calls&&!v12::synced&&!v12::sync_pending&&!v12::fault);
    return 0;
  }
  if(which=="coarse-hold-fences"){
    const v12::G1Anchor a{true,111,222,1700000000000000,100000,250000,3,4,40000};
    v12::anchor=a;fake_now=260000;v12::changed_scope(fake_now,v12::G1Reason::Permission);v12::publish(fake_now);
    auto status=v12::r2_status();assert(status.clock_valid&&!status.following&&!status.root_identity&&!status.root_boot&&!status.service_until_us&&!status.model_available20&&!status.source);
    assert(status.follower_utc_us==a.utc+fake_now-a.at&&v12::coarse_hold.saved.until==a.until&&status.g1_clock_origin==a.root);
    const auto budget=r2_group_anchor_deadline(a.at,a.uncertainty);fake_now=budget;v12::publish(fake_now);assert(!v12::r2_status().clock_valid&&!v12::r2_status().g1_clock_origin);
    fake_now=300000;v12::coarse_hold.retain(a);v12::changed_scope(fake_now);assert(!v12::coarse_hold.saved.source);
    v12::coarse_hold.retain(a);v12::begin_close(fake_now);assert(!v12::coarse_hold.saved.source);v12::close_poll(fake_now);assert(v12::mode==v12::R2Mode::Off);
    v12::G1CoarseHold overflow;auto huge=a;huge.utc=UINT64_MAX;overflow.retain(huge);uint64_t utc=0;assert(!overflow.deliver(100001,utc));return 0;
  }
  if(which=="sync-cancel-inline-race"||which=="sync-cancel-queued-race"||which=="sync-cancel-missing"){
    v12::sync_identity=111;v12::sync_address=v12::canonical(111);v12::sync_requested_at=300000;v12::sync_pending=true;
    sync_cancel_disallowed=true;sync_cancel_inline=which=="sync-cancel-inline-race";
    fake_now=450000;v12::active_poll(fake_now);pump_cleanup();const auto deadline=v12::sync_drain_until;
    assert(deadline==2450000&&!v12::fault&&!sync_cancel_calls);
    fake_now=450000+v12::G1Window::SyncCancelGrace;v12::active_poll(fake_now);pump_cleanup();
    if(which=="sync-cancel-missing"){
      assert(v12::sync_pending&&v12::sync_cancelled);fake_now=480000;v12::active_poll(fake_now);assert(v12::sync_drain_until==deadline);
      fake_now=500000;v12::active_poll(fake_now);assert(v12::fault&&v12::quiet_faults==1&&v12::sync_pending&&v12::sync_drain_until==deadline);return 0;
    }
    if(!sync_cancel_inline){assert(v12::sync_pending&&v12::sync_cancelled);fake_now=459000;sync_established();v12::active_poll(fake_now);pump_cleanup();}
    assert(!v12::sync_pending&&!v12::synced&&!v12::sync_terminating&&!v12::fault&&v12::sync_drain_until==deadline);
    bool cancelled=false,established=false,terminated_sync=false,lost=false;
    for(const auto& r:v12::diagnostic_records){
      if(r.bytes[0]>=17&&r.bytes[0]<=20)assert(r2_group_get(r.bytes+16,8)==111&&r2_group_get(r.bytes+12,4)>=150000);
      if(r.bytes[0]==18){cancelled=true;assert(r.bytes[6]==1&&r2_group_get(r.bytes+1,4)==524);}
      if(r.bytes[0]==17){established=true;assert(r.bytes[30]==1&&r.bytes[31]==7);}
      if(r.bytes[0]==19){terminated_sync=true;assert(r.bytes[6]==2&&r.bytes[30]==1&&r.bytes[31]==7);}
      if(r.bytes[0]==20)lost=true;
    }
    assert(cancelled&&established&&terminated_sync&&lost);fake_now=500000;v12::active_poll(fake_now);assert(!v12::fault);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="permission-phase"){
    v12::G1PhaseLearning learning;v12::G1Anchor a{true,111,222,1700000000000000,100000,1400000,3,4,40000};
    learning.accepted(a,100000);assert(!learning.due(a,100000));a.at=1100000;a.until=1750000;learning.accepted(a,1100000);assert(!learning.due(a,1100000));
    a.at=2100000;a.until=2750000;learning.accepted(a,2100000);assert(learning.due(a,2100000));
    auto other=a;other.boot++;assert(!learning.due(other,2100000));assert(!learning.due(a,2750000));learning.accepted(other,2100000);assert(!learning.due(other,2100000));
    a.at=200000;a.until=850000;v12::anchor=a;v12::phase_learning.accepted(a,200000);a.at=210000;v12::anchor=a;v12::phase_learning.accepted(a,210000);
    fake_now=450000;v12::active_poll(fake_now);assert(v12::window_phase==125000&&!v12::owners_active()&&!v12::fault);
    assert(v12::radio_resume_at>=v12::window_closed_at+500000&&v12::anchor.same(a)&&v12::anchor.until==a.until);
    assert(!v12::phase_learning.due(a,fake_now));fake_now=1000000;v12::active_poll(fake_now);assert(!v12::owners_active());
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="diagnostic-stages"){
    discovery(true);const auto generation=v12::peer.incarnation;const auto identity=v12::peer.identity;
    hidden_link=true;fake_now=100000;v12::probe_pending_link(fake_now);
    assert(v12::peer.connecting&&v12::peer.handle==v12::None&&!terminated&&!v12::member&&!v12::anchor.source);
    assert(v12::peer.descriptor_at==fake_now&&v12::diagnostic.descriptor_seen==1);
    assert(v12::diagnostic.default_tx_dbm==9&&v12::diagnostic.scan_tx_dbm==9&&v12::diagnostic.init_tx_dbm==9&&v12::diagnostic.conn_tx_dbm==9);
    fake_now=200000;v12::active_poll(fake_now);assert(terminated==1&&!v12::peer.identity);
    bool request=false,descriptor=false,ack=false,disconnect=false;
    for(const auto& r:v12::diagnostic_records){assert(r.length==32&&r.bytes[5]==2);
      if(r.bytes[0]==11)request=true;if(r.bytes[0]==13)descriptor=true;if(r.bytes[0]==2)disconnect=true;
      if(r.bytes[0]==7){ack=true;assert(r.cycle==generation&&r2_group_get(r.bytes+16,8)==identity);
        assert(r2_group_get(r.bytes+8,4)==200000&&r.bytes[29]==0&&r.bytes[28]==10);}}
    assert(request&&descriptor&&ack&&disconnect&&v12::diagnostic.disconnect_max_us==0);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="diagnostic-filters"){
    v12::Discovery d{};d.props=1;d.length=25;d.bytes[0]=24;d.bytes[1]=0xff;auto* p=d.bytes+2;
    p[0]='G';p[1]='1';p[2]=1;r2_group_put(p+3,v12::provision.group.id,8);r2_group_put(p+11,2,4);
    r2_group_put(p+15,111,8);d.address=v12::canonical(111);d.at=fake_now;v12::discoveries.push(d);step();
    assert(v12::diagnostic.reasons[unsigned(v12::G1Reason::Epoch)]==1&&!connected);
    r2_group_put(p+11,1,4);v12::cooldown(111,fake_now,5000000);d.at=fake_now;v12::discoveries.push(d);step();
    assert(v12::diagnostic.eligible==1&&v12::diagnostic.peer(111).eligible==1);
    assert(v12::diagnostic.reasons[unsigned(v12::G1Reason::Cooldown)]==1&&!connected);
    assert(v12::diagnostic.first==v12::G1Reason::Epoch&&v12::diagnostic.last==v12::G1Reason::Cooldown);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="cold-phase-search"){
    unsigned changes=0;uint32_t old=0;
    while(changes<8){
      step();assert(!v12::fault);
      if(v12::window_phase!=old){
        ++changes;assert(v12::window_phase==(old+125000)%1000000);
        old=v12::window_phase;assert(!v12::owners_active());
        const auto next=v12::radio_resume_at;
        assert(next>=v12::window_closed_at+500000);
        while(fake_now+2000<next){step();assert(!v12::owners_active()&&!v12::fault);}
      }
    }
    assert(v12::window_phase==0&&v12::windows>=64&&v12::window_max_owned_us<=452000);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="root-hint-filter"){
    v12::admission_root=UINT64_C(0x0000000000000002);v12::admission_until=fake_now+100000;
    v12::Discovery d{};d.props=1;d.length=25;d.bytes[0]=24;d.bytes[1]=0xff;
    auto* p=d.bytes+2;p[0]='G';p[1]='1';p[2]=1;r2_group_put(p+3,v12::provision.group.id,8);
    r2_group_put(p+11,1,4);r2_group_put(p+15,UINT64_C(0x0000000000000001),8);
    d.address=v12::canonical(UINT64_C(0x0000000000000001));d.at=fake_now;
    v12::discoveries.push(d);step();assert(!connected&&!v12::anchor.source);
    v12::admission_root=UINT64_C(0x0000000000000001);d.at=fake_now;
    v12::discoveries.push(d);step();assert(connected==1&&!v12::anchor.source);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="discovery-rssi"){
    ble_gap_event e{};e.type=BLE_GAP_EVENT_EXT_DISC;e.ext_disc.rssi=-71;
    e.ext_disc.tx_power=-8;e.ext_disc.prim_phy=1;e.ext_disc.sec_phy=2;e.ext_disc.legacy_event_type=7;
    e.ext_disc.addr.type=BLE_ADDR_RANDOM;v12::gap(&e,nullptr);v12::Discovery d{};
    assert(v12::discoveries.pop(d)&&d.rssi==-71&&d.tx_power==-8&&d.primary_phy==1&&d.secondary_phy==2&&d.legacy_event_type==7);
    discovery(true);const auto original=v12::discovery_report;assert(original.rssi==127);
    std::memcpy(e.ext_disc.addr.val,original.address_le,6);e.ext_disc.props=1;e.ext_disc.length_data=original.length;e.ext_disc.data=original.bytes;
    v12::gap(&e,nullptr);step();const auto& r=v12::discovery_report;
    assert(r.metadata_mask==1&&r.rssi==-71&&r.advertised_tx_power==-8&&r.primary_phy==1&&r.secondary_phy==2&&r.legacy_event_type==7);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="explicit-restart"){
    v12::R2ClientService legacy;assert(legacy.admit(1,1)&&legacy.start(2));legacy.stop(3);legacy.stopped(4);
    assert(!legacy.admit(5,2));assert(legacy.admit(5,2,true));
    legacy.stop(6);const auto original=legacy.stop_deadline_us;legacy.stop(9);assert(legacy.stop_deadline_us==original);
    legacy.stopped(10);legacy.generation=UINT32_MAX;assert(!legacy.admit(11,3,true));
    assert(!v12::r2_client_start(23));
    fake_now=100000;v12::r2_stop();const auto deadline=v12::client.stop_deadline_us;
    assert(!v12::r2_client_start(23));step(2);assert(v12::r2_status().off_ack);
    assert(v12::client.stop_deadline_us==deadline&&!v12::r2_client_start(22));
    assert(v12::r2_client_start(23));step();assert(v12::client.generation==2&&v12::client.running);
    assert(!scan_active&&v12::boot_client_cancelled&&!v12::anchor.source&&!v12::member);
    fake_now=999999;v12::active_poll(fake_now);assert(!scan_active);
    fake_now=1000000;v12::active_poll(fake_now);assert(scan_active&&!v12::fault);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack&&v12::client.stop_completed==2);
    v12::fault=true;assert(!v12::r2_client_start(24));return 0;
  }
  if(which=="no-source-held"||which=="foreign-root-held"||which=="warm-control-PA"){
    // Use production authenticated exchange validation with an existing
    // acquired parent. A refused control reply must not own that PA/model.
    fake_now=1000000;v12::window_start=fake_now;
    v12::anchor={true,333,444,1700000000000000,fake_now,5000000,7,8,40000};
    v12::joined_peer=333;v12::joined_boot=444;v12::member=true;
    v12::synced=true;v12::sync_identity=333;v12::sync_handle=9;v12::sync_address=v12::canonical(333);v12::sync_requested_at=v12::sync_owner_window_start=fake_now;
    const auto old=v12::anchor;const auto old_sync=v12::sync_capture_epoch;
    v12::admission_policy.acquired();
    if(which=="warm-control-PA"){
      v12::admission_policy.started(fake_now);v12::admission_policy.failed(fake_now,20000000);
      v12::Discovery d{};d.address=v12::canonical(333);v12::connect(d,333,fake_now+1000000);
      assert(!connected&&v12::synced&&v12::sync_capture_epoch==old_sync&&v12::anchor.until==old.until);
    }else{
      v12::peer={};v12::peer.identity=111;v12::peer.handle=22;v12::peer.incarnation=++v12::incarnation;conn_arg=v12::callback_arg();
      assert(!r2_group_join_begin(&v12::peer.exchange,false,v12::provision.identity,111,22,77,fake_now));
      auto& x=v12::peer.exchange;x.peer_boot=55;x.remote_nonce=88;x.proof_at=fake_now-1000;x.challenged=true;
      r2_join_record reply{};reply.kind=R2_JOIN_ACCEPT;reply.sender=111;reply.receiver=v12::provision.identity;
      reply.sender_boot=55;reply.receiver_boot=22;reply.client_nonce=77;reply.server_nonce=88;reply.sequence=2;
      reply.created_us=fake_now;reply.echo_us=x.proof_at;std::strcpy(reply.name,"Fixture");
      if(which=="foreign-root-held"){reply.source_flags=1;reply.source_bound_us=40000;reply.root=999;reply.root_boot=888;reply.source_epoch=9;reply.publication=10;reply.utc_us=1700000000000000;reply.remaining_us=1000000;}
      v12::Incoming row{};row.at=fake_now;row.incarnation=v12::peer.incarnation;
      assert(!r2_group_join_encode(row.bytes,&reply,&v12::provision.group));v12::incoming.push(row);v12::receive_join(fake_now);
      assert(v12::join_accepts==1&&v12::peer.selection_accepted);
      assert(v12::joined_peer==333&&v12::joined_boot==444&&v12::member&&v12::anchor.same(old)&&v12::anchor.until==old.until);
      assert(v12::synced&&!v12::sync_terminating&&v12::sync_capture_epoch==old_sync);
      assert(v12::admission_policy.no_source==(which=="no-source-held"));
      assert(v12::admission_policy.foreign_root==(which=="foreign-root-held"));
      assert(!v12::parent_failure.identity&&v12::admission_policy.retry_at>=fake_now+20000000);
    }
    v12::r2_stop();step(6);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="public-marker"){
    assert(v12::chars[0].flags&BLE_GATT_CHR_F_READ);os_mbuf b;ble_gatt_access_ctxt ctx;ctx.op=BLE_GATT_ACCESS_OP_READ_CHR;ctx.om=&b;
    assert(!v12::access(22,0,&ctx,nullptr));assert((b.bytes==std::vector<uint8_t>{'R','2','G','1',1}));
    assert(!v12::member&&!v12::anchor.source&&!v12::rx&&!v12::join_accepts);
    ctx.op=BLE_GATT_ACCESS_OP_WRITE_CHR;assert(v12::access(22,0,&ctx,nullptr)==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="passive-quiet"){
    assert(scan_active);fake_now=449999;v12::active_poll(fake_now);assert(scan_active);
    fake_now=450000;v12::active_poll(fake_now);assert(!scan_active&&!v12::owners_active());
    const auto closed=v12::window_closed_at;assert(closed==450000);
    fake_now=999999;v12::active_poll(fake_now);assert(!scan_active&&!v12::fault);
    fake_now=1000000;v12::active_poll(fake_now);assert(scan_active&&v12::windows==2&&!v12::fault);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="sync-quiet"||which=="quiet-drain-blocked"){
    v12::anchor={true,1,2,1700000000000000,0,2000000,3,4,40000};
    v12::synced=true;v12::sync_handle=9;sync_late=which=="quiet-drain-blocked";
    fake_now=450000;v12::active_poll(fake_now);pump_cleanup();
    if(sync_late){const auto original=v12::sync_drain_until;assert(v12::synced&&v12::sync_terminating);fake_now=480000;v12::active_poll(fake_now);assert(v12::sync_drain_until==original);fake_now=500000;v12::active_poll(fake_now);assert(v12::fault&&v12::quiet_faults==1);return 0;}
    assert(!v12::synced&&!v12::sync_terminating&&v12::anchor.valid(fake_now)&&v12::following(fake_now));
    fake_now=1000000;v12::active_poll(fake_now);assert(!v12::fault&&v12::anchor.valid(fake_now));
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="hidden-early-reserve"){
    discovery(true);hidden_link=true;fake_now=200000;v12::active_poll(fake_now);
    assert(v12::pending_link_teardowns==1&&v12::peer.handle==v12::None&&!v12::peer.connecting);
    fake_now=500000;v12::active_poll(fake_now);assert(!v12::fault&&!v12::quiet_faults);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="hidden-slow-close-reserve"){
    discovery(true);hidden_link=true;late_disconnect=true;
    fake_now=199999;v12::active_poll(fake_now);assert(!terminated);
    fake_now=200000;v12::active_poll(fake_now);assert(terminated==1&&v12::peer.terminating);
    const auto deadline=v12::peer.drain_until;
    fake_now=421161;ble_gap_event e{};e.type=BLE_GAP_EVENT_DISCONNECT;
    e.disconnect.conn.peer_id_addr=v12::peer.address;v12::gap(&e,conn_arg);
    fake_now=500000;v12::active_poll(fake_now);assert(!v12::fault&&!v12::quiet_faults&&!v12::owners_active());
    assert(deadline==2200000);v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="late-window-connect"){
    discovery(true);fake_now=410000;ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;
    v12::gap(&e,conn_arg);assert(v12::peer.dead);
    auto sent=output.size();v12::active_poll(fake_now);
    assert(output.size()==sent&&v12::peer.handle==v12::None&&!v12::fault);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="expired-source-reacquire"){
    v12::anchor={true,UINT64_C(0x0000000000000003),55,1700000000000000,0,fake_now,2,3,40000};
    v12::joined_peer=v12::anchor.root;v12::joined_boot=55;v12::member=true;
    const auto next_window=fake_now+2000000;while(fake_now<next_window)step();
    // Continuous1.5s expiry requests JOIN and allows a different candidate,
    // while the old independent replay anchor can still acquire PA. Old
    // FOLLOW remains withdrawn at its unchanged original signed expiry.
    discovery();assert(v12::anchor.source&&!v12::following(fake_now));
    assert(v12::peer.identity==UINT64_C(0x0000000000000001)&&!v12::member);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="freshness-policy"){
    v12::G1Anchor a{true,1,2,1700000000000000,0,1500000,3,4,40000};
    assert(a.valid(0)&&a.live(1499999)&&!a.live(1500000));
    assert(!a.rebootstrap(1499999)&&!a.rebootstrap(1500000)&&!a.rebootstrap(2999999)&&a.rebootstrap(3000000));
    a.until=100000000;assert(!a.refresh(29949000)&&a.refresh(29950000));
    assert(!a.urgent(42449000)&&a.urgent(42450000));
    assert(a.valid(104899999)&&!a.valid(104900000));
    assert(!v12::G1Window::initiate(30000000,a,false)&&v12::G1Window::initiate(30000000,a,true));
    assert(v12::G1Window::initiate(43000000,a,false)&&!v12::G1Window::initiate(43100000,a,true));
    auto same=a;same.at=10;same.uncertainty=41000;assert(a.same(same));same.boot++;assert(!a.same(same));
    a.uncertainty=100000;assert(a.guard(0)==100100);a.at=100;assert(!a.valid(99));
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="permission-refresh-policy"){
    v12::G1Anchor a{true,1,2,1700000000000000,0,1500000,3,4,40000};
    assert(!a.refresh(1000000)&&!a.urgent(1000000));
    assert(!v12::G1Window::initiate(1000000,a,false));
    assert(!v12::G1Window::initiate(1100000,a,false));
    assert(a.until==1500000&&!a.live(1500000)&&!a.rebootstrap(1500000));
    assert(a.rebootstrap(3000000)&&v12::G1Window::initiate(3000000,a,false));
    auto next=a;next.at=1000000;next.until=2300000;
    assert(next.same(a)&&next.live(1500000));
    assert(a.until==1500000); // A new bootstrap never mutates old authority.
    next.boot++;assert(!next.same(a));
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="acl-reserve"){
    discovery();assert(v12::peer.handle!=v12::None);late_disconnect=true;
    fake_now=400000;v12::active_poll(fake_now);assert(v12::peer.terminating&&!v12::fault);
    fake_now=453000;ble_gap_event e;e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.peer_id_addr=v12::peer.address;v12::gap(&e,conn_arg);
    fake_now=500000;v12::active_poll(fake_now);assert(!v12::owners_active()&&!v12::fault);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="gr3-direct"||which=="gr3-direct-old-origin"){
    v12::anchor={true,111,55,1700000000000000,fake_now,fake_now+100000,2,3,40000};
    v12::joined_peer=v12::sync_identity=111;v12::joined_boot=55;
    const uint64_t ids[4]={111,0,0,0};r2_relay_packet p{};
    p.origin={0,1,55,2,1000000,1651000000,1000000,INT64_C(1699999999999000000)};
    if(which=="gr3-direct-old-origin"){
      p.origin.utc_offset_ns-=900000000;
      assert(!v12::fresh_envelope(p.origin.created_ns,p.origin.expires_ns,p.origin.utc_offset_ns,fake_now));
      assert(v12::fresh_envelope(p.origin.created_ns,p.origin.expires_ns,p.origin.utc_offset_ns,fake_now,true));
      assert(!v12::fresh_envelope(p.origin.created_ns,p.origin.expires_ns+1,p.origin.utc_offset_ns,fake_now,true));
      assert(!v12::fresh_envelope(p.origin.created_ns,p.origin.expires_ns,p.origin.utc_offset_ns-800000000,fake_now,true));
      assert(!v12::fresh_envelope(p.origin.created_ns,p.origin.expires_ns,p.origin.utc_offset_ns+1000000000,fake_now,true));
    }
    p.route={0,0,0,0,55,3,8,9,3,UINT32_MAX};
    v12::Periodic row{};row.at=fake_now;row.identity=111;row.length=190;uint8_t proof[32]{};
    assert(!r2_group_origin_sign3(proof,&p.origin,&v12::provision.group,ids));
    assert(!r2_group_relay_encode3(row.bytes,&p,proof,&v12::provision.group,ids));
    const auto until=v12::anchor.until;v12::periodic.push(row);v12::periodic_poll(fake_now);
    assert(v12::relay_ok==1&&v12::relay_train==3&&v12::anchor.until==until&&!v12::window_corrected);
    row.bytes[175]^=1;v12::periodic.push(row);v12::periodic_poll(fake_now);assert(v12::relay_bad==1);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="sessionless"||which=="source-scope"||which=="relay"){
    v12::anchor={true,UINT64_C(0x0000000000000001),55,1700000000000000,fake_now,fake_now+100000,2,3,40000};
    v12::member=false;v12::sync_identity=v12::anchor.root;
    assert(v12::following(fake_now));
    v12::Periodic row{};row.at=fake_now;row.identity=v12::sync_identity;
    if(which=="relay"){
      v12::joined_peer=v12::sync_identity=UINT64_C(0x0000000000000002);v12::joined_boot=66;row.identity=v12::sync_identity;row.length=190;
      const uint64_t ids[4]={v12::anchor.root,v12::joined_peer,0,0};r2_relay_packet p{};
      p.origin={0,1,55,2,1000000,1001000000,1000000,INT64_C(1699999999999000000)};
      p.route={1,0,1,1,66,7,8,9,10,UINT32_MAX};uint8_t proof[32]{};
      assert(!r2_group_origin_sign(proof,&p.origin,&v12::provision.group,ids));
      assert(!r2_group_relay_encode(row.bytes,&p,proof,&v12::provision.group,ids));
      v12::periodic.push(row);v12::periodic_poll(fake_now);assert(v12::relay_bad==1&&v12::relay_train==0&&v12::anchor.until==fake_now+100000);
      row.bytes[175]^=1;v12::periodic.push(row);v12::periodic_poll(fake_now);assert(v12::relay_bad==2);
    }else{
      row.length=158;const uint64_t ids[4]={v12::anchor.root,0,0,0};r2_precision_packet p{};
      p.root=0;p.boot=which=="source-scope"?56:55;p.source_epoch=2;p.train=3;p.publication=p.capture_epoch=1;p.flags=1;
      p.created_ns=1000000;p.expires_ns=1001000000;p.utc_offset_ns=INT64_C(1699999999999000000);
      assert(!r2_group_precision_encode(row.bytes,&p,&v12::provision.group,ids));
      v12::periodic.push(row);v12::periodic_poll(fake_now);assert(v12::p1_ok==1);
      if(which=="source-scope")assert(!v12::anchor.source&&!v12::following(fake_now));
      else{assert(v12::anchor.until>fake_now+100000&&v12::following(fake_now));fake_now=v12::anchor.until;assert(!v12::following(fake_now)&&v12::anchor.valid(fake_now));}
    }
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="discovery-diagnostics"){
    ble_gap_event e{};e.type=BLE_GAP_EVENT_EXT_DISC;e.ext_disc.addr.type=BLE_ADDR_RANDOM;e.ext_disc.data_status=1;v12::gap(&e,nullptr);
    e.ext_disc.data_status=0;e.ext_disc.length_data=32;v12::gap(&e,nullptr);
    e.ext_disc.length_data=0;e.ext_disc.addr.type=0;v12::gap(&e,nullptr);
    uint8_t bytes[28]={2,1,6,24,0xff,'G','1',1};r2_group_put(bytes+8,UINT64_C(0x9999999999999999),8);r2_group_put(bytes+16,2,4);
    e.ext_disc.addr.type=BLE_ADDR_RANDOM;e.ext_disc.length_data=28;e.ext_disc.data=bytes;v12::gap(&e,nullptr);step();
    auto s=v12::r2_status();assert(s.g1_adv_callbacks==4&&s.g1_adv_incomplete==1&&s.g1_adv_long==1&&s.g1_adv_nonrandom==1&&s.g1_discovery_checked==1&&s.g1_group_records==1);
    assert(s.g1_last_group_id==UINT64_C(0x9999999999999999)&&s.g1_last_key_epoch==2&&s.g1_sdk_scan_active&&!s.discoveries&&!s.join_attempts&&!s.group_member&&!s.following);
    bytes[3]=25;v12::gap(&e,nullptr);step();assert(v12::r2_status().g1_group_records==1&&!connected);
    v12::r2_stop();step(2);assert(v12::r2_status().off_ack);return 0;
  }
  if(which=="scan-timeout"){fake_now=v12::scan_retry_until;step();assert(v12::r2_status().fault);return 0;}
  if(which=="scan-transient"){const auto ttl=v12::scan_retry_until;fake_now+=100000;scan_error=BLE_HS_ENOMEM;step();assert(v12::scan_retry_until==ttl);fake_now+=100000;scan_error=0;step();assert(v12::scan&&!v12::fault);}
  if(which=="persist"||which=="scan-transient"){for(unsigned i=0;i<150;i++){step(500);}assert(v12::client.running&&!v12::member);}
  else if(which=="pending-disconnect"||which=="pending-disconnect-stale"){
    discovery(true);ble_gap_event e;e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.peer_id_addr=v12::peer.address;e.disconnect.reason=0x3e;
    if(which=="pending-disconnect-stale"){
      v12::gap(&e,reinterpret_cast<void*>(uintptr_t(v12::peer.incarnation+1)));assert(v12::peer.connecting);
      e.disconnect.conn.peer_id_addr.val[0]^=1;v12::gap(&e,conn_arg);assert(v12::peer.connecting);e.disconnect.conn.peer_id_addr.val[0]^=1;
    }
    v12::gap(&e,conn_arg);step();assert(!v12::peer.connecting&&v12::peer.handle==v12::None&&v12::pending_disconnects==1&&v12::last_disconnect_reason==0x3e&&!v12::fault);
  }
  else if(which=="adopted-connect"||which=="adopted-connect-wrong-handle"||which=="adopted-connect-stale"){
    discovery(true);hidden_link=true;late_disconnect=true;
    fake_now=400000;v12::active_poll(fake_now);
    assert(v12::peer.dead&&v12::peer.terminating&&v12::pending_link_teardowns==1);
    const auto deadline=v12::peer.drain_until;const auto stage=v12::peer.stage;
    ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=v12::peer.handle;
    if(which=="adopted-connect-wrong-handle")++e.connect.conn_handle;
    fake_now=420000;v12::gap(&e,which=="adopted-connect-stale"?reinterpret_cast<void*>(uintptr_t(v12::peer.incarnation+1)):conn_arg);
    assert(v12::peer.dead&&v12::peer.terminating&&v12::peer.drain_until==deadline&&v12::peer.stage==stage&&!v12::peer.ready&&output.empty());
    assert(!v12::diagnostic.connect_ok);
    if(which=="adopted-connect")assert(v12::diagnostic.connect_drain==1);
    if(which=="adopted-connect-wrong-handle"){assert(v12::fault);return 0;}
    assert(!v12::fault);e={};e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.peer_id_addr=v12::peer.address;
    e.disconnect.conn.conn_handle=v12::peer.handle;fake_now=450000;v12::gap(&e,conn_arg);v12::active_poll(fake_now);
    assert(!v12::owners_active()&&!v12::fault);late_disconnect=false;
  }
  else if(which=="pending-link-cleanup"||which=="pending-link-drain-blocked"){
    discovery(true);hidden_link=true;late_disconnect=which=="pending-link-drain-blocked";const auto original=v12::peer.attempt_until;fake_now=original+1;v12::cleanup_pending_link(fake_now);assert(v12::pending_link_teardowns==1&&cancel_calls==0);
    if(late_disconnect){assert(v12::peer.drain_until==original+2000000);fake_now=v12::peer.drain_until;step();assert(v12::fault&&!v12::r2_status().off_ack);return 0;}
    assert(!v12::peer.connecting&&v12::peer.handle==v12::None&&!v12::fault);
  }
  else if(which=="connect-timeout"||which=="connect-cancel-blocked"){cancel_missing=which=="connect-cancel-blocked";discovery(true);const auto deadline=v12::peer.attempt_until;fake_now=deadline;v12::cancel_connect(fake_now);assert(cancel_calls==1);if(cancel_missing){step();assert(cancel_calls==1);fake_now=deadline+2000000;step();assert(v12::fault&&!v12::r2_status().off_ack);return 0;}assert(!v12::peer.connecting&&!v12::fault);}
  else {small_mtu=which=="small-mtu";marker_bad=which=="marker-fallback";late_disconnect=which=="stop-blocked";inline_failure=which=="inline-failed-connect";inline_success=which=="inline-connected";discovery();if(marker_bad)step(2);
    if(which=="inline-failed-connect"){assert(!v12::peer.connecting&&output.empty()&&!v12::fault);}
    else if(which=="small-mtu"){step();assert(output.empty()&&terminated);}
    else {assert(output.size()==1);r2_join_record request{};assert(!r2_group_join_decode(&request,output[0].data(),R2_GROUP_JOIN_BYTES,&v12::provision.group));assert(!request.capabilities&&!request.control);
      r2_join_record challenge{};challenge.kind=R2_JOIN_CHALLENGE;challenge.sender=request.receiver;challenge.receiver=request.sender;challenge.sender_boot=55;challenge.receiver_boot=22;challenge.client_nonce=request.client_nonce;challenge.server_nonce=99;challenge.sequence=1;challenge.created_us=200000;challenge.echo_us=request.created_us;std::strcpy(challenge.name,"Peer");
      if(which=="wrong-echo")challenge.echo_us++;
      if(which=="control-upgrade"){challenge.capabilities=1;challenge.control=1;}
      inject(challenge,which=="wrong-key");
      if(which=="wrong-key"||which=="wrong-echo"||which=="control-upgrade"){assert(output.size()==1&&!v12::member&&v12::rejected);}
      else {assert(output.size()==2);r2_join_record proof{};assert(!r2_group_join_decode(&proof,output[1].data(),R2_GROUP_JOIN_BYTES,&v12::provision.group));assert(proof.kind==R2_JOIN_PROOF&&!proof.capabilities&&!proof.control);
        r2_join_record accept=challenge;accept.kind=R2_JOIN_ACCEPT;accept.sequence=2;accept.echo_us=proof.created_us;accept.created_us+=10000;
        if(which=="bootstrap"||which=="root-redirect"||which=="root-redirect-held"){accept.source_flags=1;accept.source_bound_us=40000;accept.root=which!="bootstrap"?UINT64_C(0x0000000000000002):accept.sender;accept.root_boot=55;accept.source_epoch=2;accept.publication=3;accept.utc_us=1700000000000000;accept.remaining_us=1000000;}
        v12::G1Anchor old_clock;
        v12::G1ClockPrediction old_prediction;
        if(which=="root-redirect-held"){
          old_clock={true,UINT64_C(0x0000000000000003),777,1700000000000000,fake_now-10000,fake_now-1,8,9,40000};
          v12::coarse_hold.retain(old_clock);
          uint32_t publication=1;uint64_t previous_remote=0;uint16_t previous_event=0;
          for(unsigned second=1;second<=32;second++)for(unsigned half=0;half<2;half++){
            const uint64_t us=uint64_t(second)*1000000+half*62500;
            const uint64_t remote=us*1000+uint64_t(std::llround(double(us)*.002));
            r2_relay_packet p{};p.origin={0,1,777,8,uint64_t(second)*1000000000,uint64_t(second)*1000000000+1650000000,0,INT64_C(1700000000000000000),0,0};
            p.route={0,0,0,0,777,9,publication,1,9,UINT32_MAX};
            if(half){p.count=1;p.correction[0]={publication-1,previous_event,4,previous_remote*2/125,9};}
            v12::periodic_rx::Stamp c{};c.actual=c.association_observed=true;c.tick20=us*20;c.available20=c.tick20+200;c.epoch=1;c.sequence=publication;
            c.event=uint16_t(publication);c.channel=4;c.map_enclosure_ns=1000;
            const bool accepted=v12::corrected_clock.receive(p,c,c.available20,fake_now-2000,old_clock);
            if(half){assert(accepted&&v12::corrected_clock.finalize(c.available20+200));}
            previous_remote=remote;previous_event=c.event;++publication;
          }
          old_prediction=v12::corrected_clock.prediction();assert(v12::corrected_clock.frequency_trained()&&std::abs(old_prediction.rate-2000)<10);
          v12::anchor=old_clock; // A retained acquisition anchor must also survive an indirect locator.
        }
        if(which=="expired"){fake_now=proof.created_us+250000;}
        if(which=="expired"){auto old=v12::peer.exchange;assert(r2_group_join_receive(&old,&accept,old.until));}
        inject(accept);{assert(v12::member&&v12::join_accepts==1&&v12::r2_status().group_member);assert(v12::r2_status().following==(which=="bootstrap"));inject(accept);assert(v12::join_accepts==1);if(which=="bootstrap"){const auto until=v12::anchor.until;while(fake_now<until)step();const auto s=v12::r2_status();assert(!s.following&&s.clock_valid&&!s.root_identity&&!s.service_until_us&&!s.model_available20);}}
        if(which=="root-redirect"||which=="root-redirect-held"){
          assert(v12::cooled(accept.sender,fake_now+1000000));
          assert(v12::admission_root==accept.root&&v12::admission_until>fake_now&&!v12::anchor.source);
          const auto deadline=v12::admission_until;while(fake_now<deadline)step();
          assert(!v12::admission_root&&!v12::admission_until&&!v12::following(fake_now));
          if(which=="root-redirect-held"){
            const auto held=v12::r2_status();assert(held.clock_valid&&!held.following&&!held.root_identity&&held.g1_clock_origin==old_clock.root);
            assert(v12::coarse_hold.saved.until==old_clock.until&&v12::coarse_hold.saved.at==old_clock.at&&v12::corrected_clock.same(old_clock));
            const auto kept=v12::corrected_clock.prediction();assert(kept.tick==old_prediction.tick&&kept.epoch==old_prediction.epoch&&kept.rate==old_prediction.rate);
            uint64_t utc=0;assert(v12::corrected_clock.deliver(kept.tick+10000000,fake_now,utc)&&utc==uint64_t(old_prediction.at(kept.tick+10000000))/1000);
            const auto retired=v12::coarse_hold;assert(!retired.deliver(r2_group_anchor_deadline(old_clock.at,old_clock.uncertainty),utc));
            fake_now=(fake_now/1000000+1)*1000000+1000;
            fresh_direct(accept.root,888,10,11);assert(v12::corrected_clock.same(v12::anchor)&&!v12::corrected_clock.prediction().tick&&!v12::corrected_clock.frequency_trained());
            assert(!v12::coarse_hold.saved.source&&std::abs(v12::corrected_clock.prediction().rate-old_prediction.rate)<1);
          }
        }
        if(which=="bootstrap")assert(!v12::cooled(accept.sender,fake_now+500000));
      }
    }
  }
  if(which=="stop-blocked"){late_disconnect=true;}
  v12::r2_stop();step();const auto end=v12::client.stop_deadline_us;
  if(which=="stop-blocked"){fake_now=end+1;step();assert(v12::r2_status().fault&&!v12::r2_status().off_ack);}
  else {step();assert(v12::r2_status().off_ack&&v12::client.completed_us<=end);assert(v12::r2_host_sync());step();assert(v12::r2_status().off_ack&&!v12::r2_client_start(22));}
  std::puts("PASS G1 actual adapter client-only join/causal echo/owner closure");
}
