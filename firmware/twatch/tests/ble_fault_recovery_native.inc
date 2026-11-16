// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
int main(int argc,char** argv){
 assert(argc==2);const std::string which=argv[1];const uint8_t mac[]={2,3,4,5,6,7};
 assert(v12::r2_init(44,mac)&&v12::r2_host_init());
 if(which=="init-off-on"||which=="sync-off-race"){
  if(which=="init-off-on"){
   v12::r2_watch_host_resume(false);v12::r2_watch_host_resume(true);
   watch_fixture_can_admit=false; // Original stop still owns SDK initialization.
   assert(v12::r2_host_sync()&&!v12::fault&&!v12::client.generation);
   assert(v12::watch_host_resume_pending&&!v12::boot_client_attempted);
  }else{
   watch_fixture_cancel_at_start=true; // OFF wins after Sync's admission snapshot.
   assert(v12::r2_host_sync()&&!v12::fault&&!v12::client.generation);
   v12::r2_watch_host_resume(true);
  }
  watch_fixture_can_admit=true; // Recreated host's fresh Sync after original OFF.
  assert(v12::r2_host_sync()&&!v12::fault&&v12::client.generation==1);
  step(8);
  assert(!v12::watch_host_resume_pending&&v12::client.running);return 0;
 }
 assert(v12::r2_host_sync());step(8);v12::stop_scan();
 if(which=="host-cycle"){
  const auto original_id=v12::client.request_id;const auto original_generation=v12::client.generation;
  v12::r2_stop();step(8);assert(v12::client.off()&&v12::r2_watch_host_suspend());
  assert(v12::host_ready);v12::r2_watch_host_fenced();assert(!v12::host_ready);
  assert(v12::r2_watch_capture_retired()&&v12::r2_watch_host_rearm());
  v12::r2_watch_host_resume(true);assert(v12::r2_host_init()&&v12::r2_host_sync());step(8);
  assert(v12::client.running&&!v12::fault&&v12::client.generation==original_generation+1&&v12::client.request_id!=original_id);
  assert(!v12::member&&!v12::anchor.source&&!v12::following(fake_now));return 0;
 }
 assert(!v12::r2_watch_radio_recover());
 fake_now=100000000;
 v12::sync_identity=v12::joined_peer=501;v12::joined_boot=601;
 v12::sync_address=v12::sync_owner_address=v12::canonical(501);
 v12::synced=true;v12::sync_handle=9;
 uint8_t expected_reason=v12::WatchFaultApi;
 if(which=="epoch"||which=="epoch-failed"||which=="sid"||which=="interval"||which=="address"||which=="adopt"){
  v12::anchor={true,501,601,UINT64_C(1700000000000000)+fake_now,fake_now,fake_now+1500000,2,3,160000};
  v12::sync_pending=true;v12::synced=false;v12::sync_handle=v12::None;
  v12::sync_capture_epoch=v12::periodic_rx::scope(v12::None,true);
  fixture_sync_arg=reinterpret_cast<void*>(uintptr_t(v12::sync_capture_epoch));
  ble_gap_event event{};event.type=BLE_GAP_EVENT_PERIODIC_SYNC;
  event.periodic_sync.sync_handle=9;event.periodic_sync.sid=12;event.periodic_sync.per_adv_ival=50;
  event.periodic_sync.adv_addr=v12::sync_owner_address;
  auto epoch=v12::sync_capture_epoch;
  if(which=="epoch"||which=="epoch-failed"){
   ++epoch;expected_reason=v12::WatchFaultPeriodicEpoch;
   if(which=="epoch-failed")event.periodic_sync.status=1;
  }
  else if(which=="adopt"){++v12::periodic_rx::capture_epoch;expected_reason=v12::WatchFaultCaptureAdopt;}
  else {expected_reason=v12::WatchFaultPeriodicContext;
   if(which=="sid")event.periodic_sync.sid=13;
   if(which=="interval")event.periodic_sync.per_adv_ival=51;
   if(which=="address")event.periodic_sync.adv_addr.val[0]^=1;
  }
  v12::gap(&event,reinterpret_cast<void*>(uintptr_t(epoch)));
  const auto cause=v12::r2_watch_radio_fault_status();
  assert(v12::fault&&cause.reason==expected_reason&&cause.parent==501&&cause.callback_epoch==epoch&&cause.capture_epoch==v12::sync_capture_epoch);
  assert(cause.handle==9&&cause.expected_sid==12&&cause.observed_sid==event.periodic_sync.sid&&cause.interval==event.periodic_sync.per_adv_ival);
 }
 if(which=="unknown"){v12::fault=true;v12::publish(fake_now);expected_reason=v12::WatchFaultUnknown;}
 if(which=="pin"){v12::parent_diagnostic.target=777;v12::parent_diagnostic.relay=true;v12::parent_diagnostic.state=3;}
 if(which=="drain-timeout")sync_terminate_error=BLE_HS_ETIMEOUT;
 if(which=="rebind"||which=="rebind-late"){
  assert(v12::r2_rc_parent(777));v12::parent_diagnostic_poll(fake_now);
  if(which=="rebind-late")sync_terminate_delay=2000000;
  pump_cleanup();
 }
 v12::error(BLE_HS_EINVAL,v12::R2Api::SyncCreate,9);
 const auto episode=v12::r2_watch_radio_fault_status();
 assert(episode.episode==1&&episode.reason==expected_reason&&episode.at_us==(which=="rebind-late"?102000000:100000000));
 if(expected_reason==v12::WatchFaultApi&&which!="rebind-late")assert(episode.rc==BLE_HS_EINVAL&&episode.api==uint8_t(v12::R2Api::SyncCreate)&&episode.site==9);
 if(which=="rebind-late")assert(episode.rc==BLE_HS_ETIMEOUT&&episode.api==uint8_t(v12::R2Api::SyncTerminate)&&episode.site==4);
 step(3);
 // Pending create cancellation retains its existing500ms callback grace.
 if(which=="epoch"||which=="epoch-failed"){fake_now+=500000;step(3);}
 const auto old_generation=v12::client.generation;
 const auto original_stop=v12::client.stop_requested_us,original_deadline=v12::client.stop_deadline_us;
 const auto original_completed=v12::client.completed_us;
 const auto old_api=v12::first_error_api;const auto old_site=v12::first_fault_site;
 assert(v12::fault&&!v12::following(fake_now));
 fake_now+=90000000;
 if(which=="off"||which=="explicit-on"){
  v12::r2_stop();assert(!v12::r2_watch_radio_recover());
  if(which=="off")return 0;
  assert(v12::r2_watch_radio_enable()&&v12::fault&&!v12::following(fake_now));
  assert(!v12::r2_watch_radio_enable());
 }
 assert(v12::r2_watch_radio_recover());assert(!v12::r2_watch_radio_recover());
 if(which=="aux")aux_active=true;
 if(which=="residual-scan")scan_active=true;
 if(which=="new-fault")v12::error(BLE_HS_EINVAL,v12::R2Api::ScanStart);
 if(which=="cancelled")v12::r2_stop();
 v12::r2_host_poll();pump_cleanup();v12::r2_host_poll();
 const auto status=v12::r2_watch_radio_recovery_status();
 const bool refused=which=="drain-timeout"||which=="rebind-late"||which=="aux"||which=="residual-scan"||which=="new-fault"||which=="cancelled"||which=="epoch";
 if(status.recovered!=!refused)std::fprintf(stderr,"recovery case=%s reason=%u client_off=%u closing=%u cleanup=%u/%u synced=%u pending=%u stop=%llu deadline=%llu completed=%llu\n",which.c_str(),unsigned(status.reason),unsigned(v12::client.off()),unsigned(v12::closing),unsigned(v12::sync_cleanup_queued),unsigned(v12::sync_cleanup_requested),unsigned(v12::synced),unsigned(v12::sync_pending),(unsigned long long)v12::client.stop_requested_us,(unsigned long long)v12::client.stop_deadline_us,(unsigned long long)v12::client.completed_us);
 assert(status.attempts==1&&!status.pending&&status.recovered==!refused&&status.refused==refused);
 assert(v12::first_error_api==old_api&&v12::first_fault_site==old_site);
 const auto retained=v12::r2_watch_radio_fault_status();assert(retained.episode==episode.episode&&retained.reason==episode.reason&&retained.at_us==episode.at_us&&retained.site==episode.site&&retained.rc==episode.rc);
 if(refused){
  assert(v12::fault&&v12::client.generation==old_generation);
  assert(v12::client.stop_requested_us==original_stop&&v12::client.stop_deadline_us==original_deadline);
  if(which=="epoch"){
   assert(status.reason==9&&status.unretired_periodic&&status.unretired_handle==9&&status.unretired_callback_epoch==episode.callback_epoch&&status.unretired_at_us==episode.at_us);
   assert(v12::client.off()&&!v12::owners_active()&&v12::sync_handle==v12::None);
   assert(!v12::r2_client_start(9999)); // Local OFF cannot prove the orphan absent.
   v12::recovery.stage=v12::G1ResetRecovery::Quiet;v12::recovery.eligible=true;
   v12::recovery.quiet_until=fake_now;v12::recovery.old_generation=v12::client.generation;
   v12::recovery_poll(fake_now);assert(v12::recovery.stage==v12::G1ResetRecovery::Refused&&v12::fault&&v12::client.generation==old_generation);
  }
  return 0;
 }
 assert(!status.unretired_periodic);
 assert(status.reason==2&&status.stop_requested_us==original_stop&&status.stop_deadline_us==original_deadline&&status.completed_us==original_completed);
 assert(status.completed_us>=status.stop_requested_us&&status.completed_us<status.stop_deadline_us);
 assert(!v12::fault&&v12::client.generation==old_generation+1&&v12::client.running);
 assert(!v12::following(fake_now)&&!v12::anchor.source&&!v12::member&&!v12::synced&&v12::sync_handle==v12::None);
 assert(v12::parent_diagnostic.target==((which=="pin"||which=="rebind")?777:0));
 if(which=="host-reset"){
  // Reset publishes its fault snapshot immediately. The host need not resume
  // before main can read the new UNKNOWN episode instead of the old API cause.
  ++fake_now;v12::r2_host_reset(77);
  const auto reset=v12::r2_watch_radio_fault_status();
  assert(v12::r2_status().fault&&!v12::host_ready);
  assert(reset.episode==2&&reset.at_us==fake_now&&reset.reason==v12::WatchFaultUnknown);
  assert(!reset.api&&!reset.rc&&!reset.site&&reset.handle==UINT16_MAX);
  assert(v12::first_error_api==old_api&&v12::first_fault_site==old_site);
  v12::publish(fake_now);assert(v12::r2_watch_radio_fault_status().episode==reset.episode);
 }
 if(which=="operating"){
  fresh_direct(777,888,2,3);assert(v12::following(fake_now));
  assert(!v12::corrected_clock.covers(fake_now,fake_now));
  ++fake_now;v12::error(BLE_HS_ETIMEOUT,v12::R2Api::ScanStart,4);
  const auto next=v12::r2_watch_radio_fault_status();
  assert(next.episode==2&&next.at_us==fake_now&&next.reason==v12::WatchFaultApi&&next.api==uint8_t(v12::R2Api::ScanStart)&&next.rc==BLE_HS_ETIMEOUT&&next.site==4);
  assert(v12::first_error_api==old_api&&v12::first_fault_site==old_site);
 }
 std::puts("PASS actual watch BLE owner radio-only fresh generation; original drain and failure history retained");
}
