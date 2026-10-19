// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cstdlib>
int main(int argc,char** argv){
 assert(argc==2);const std::string which=argv[1];
 const auto device=v12::native_device_binding();assert(v12::r2_init(44,device->factory));
 assert(v12::r2_host_init()&&v12::r2_host_sync());step(2);
 assert(v12::client.running&&v12::td_storage&&v12::td_enabled);
 if(which=="cleanup-create-failure"){
  fake_now=1000000;v12::scan=false;scan_active=false;v12::synced=false;v12::sync_pending=true;
  v12::sync_cancelled=false;v12::sync_terminating=false;v12::sync_handle=v12::None;
  v12::sync_capture_epoch=v12::periodic_rx::scope(v12::None,true);
  fixture_sync_arg=reinterpret_cast<void*>(uintptr_t(v12::sync_capture_epoch));
  v12::td_phase=3;v12::td_until=fake_now;v12::td_storage->model.begin(111);
  v12::cancel_sync(fake_now);assert(cleanup_scheduled&&v12::sync_cleanup_requested);
  fake_now+=1000;pump_cleanup();assert(v12::sync_cleanup_requested&&!v12::sync_cleanup_queued&&!v12::sync_cancelled);
  // A real asynchronous create failure arrives inside the original8ms grace.
  ble_gap_event event{};event.type=BLE_GAP_EVENT_PERIODIC_SYNC;event.periodic_sync.status=1;
  v12::gap(&event,fixture_sync_arg);assert(!v12::sync_pending&&!v12::synced);
  for(unsigned i=0;i<10;++i){fake_now+=2000;v12::td_poll(fake_now);pump_cleanup();}
  assert(!v12::sync_cleanup_requested&&!v12::sync_cleanup_queued&&!v12::fault);
  assert(v12::td_phase==0&&scan_active); // Previously stuck phase3,target0 forever.
  assert(!sync_cancel_calls); // No cancel against an already completed owner.
 }else if(which=="cleanup-worker-missing"){
  fake_now=1000000;v12::scan=false;scan_active=false;v12::synced=false;v12::sync_pending=true;
  v12::sync_cancelled=false;v12::sync_terminating=false;v12::sync_handle=v12::None;
  v12::td_phase=3;v12::td_until=fake_now;v12::td_poll(fake_now);
  const auto deadline=v12::sync_drain_until,closed=v12::td_storage->survey.closing_us;
  const auto switches=v12::td_storage->model.view.switches;
  for(unsigned i=0;i<10;++i){fake_now+=100000;v12::td_poll(fake_now);}
  assert(!v12::fault&&v12::sync_drain_until==deadline&&v12::td_storage->survey.closing_us==closed);
  assert(v12::td_storage->model.view.switches==switches); // Finish once, no spin/restart storm.
  fake_now=deadline;v12::td_poll(fake_now);assert(v12::fault&&v12::first_error_api==v12::R2Api::SyncCancel);
 }else if(which=="long-empty-surveys"){
  // No nearby GM is ordinary operation. Advance through14days of phase edges,
  // then beyond the32-bit millisecond boundary without resetting the service.
  for(uint64_t now=1000000;now<UINT64_C(14)*86400000000;now+=1000000){
   fake_now=now;v12::td_poll(fake_now);pump_cleanup();assert(!v12::fault);
  }
  for(uint64_t now=UINT64_C(60)*86400000000;now<UINT64_C(60)*86400000000+60000000;now+=1000000){
   fake_now=now;v12::td_poll(fake_now);pump_cleanup();assert(!v12::fault);
  }
  assert(scan_active&&v12::td_storage->model.view.switches>100000);
 }else return 2;
 // Synthetic PSRAM is explicitly freed for sanitizer ownership checks.
 v12::td_storage->~TdStorage();heap_caps_free(v12::td_storage);v12::td_storage=nullptr;
 std::printf("PASS actual SS3 native %s\n",which.c_str());
}
