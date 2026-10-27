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
 }else if(which=="diagnostics-wire"){
  auto& store=*v12::td_storage;auto& model=store.model;
  auto& mesh=model.view.mesh;const auto& group=v12::provision.group;
  r2_role_record r{};r.sender=r.root=111;r.sender_boot=r.root_boot=55;r.hot=r.coordinator=222;r.hot_boot=66;
  r.source_epoch=3;r.valid_us=2000000;r.flags=3;r.members=3;r.sequence=1;r.created_us=5000000;
  r2_gnss_record q{};q.root=111;q.root_boot=55;q.mesh_epoch=3;q.sequence=1;q.sampled_us=4900000;q.valid_us=30000000;
  q.known=63;q.values=7;q.sats=12;q.source_kind=1;q.rmc_age_ms=100;q.pps_age_ms=5;q.gga_age_ms=120;
  q.priority1=q.priority2=128;q.clock_class=6;q.accuracy=254;q.variance=UINT16_MAX;q.hdop_centi=75;
  auto gs=[&](uint64_t at){v12::Discovery d{};d.sid=12;d.address=v12::canonical(r.root);d.at=at;d.length=R2_ROLE_AD_BYTES;assert(!r2_role_encode(d.bytes,&r,&group));return d;};
  auto gd=[&](uint64_t at){v12::Discovery d{};d.sid=12;d.address=v12::canonical(q.root);d.at=at;d.length=R2_GNSS_AD_BYTES;assert(!r2_gnss_encode(d.bytes,&q,&group));return d;};
  auto observe=[&](uint64_t at){model.seen(111,111,false,0,true,true,-30,1,at);model.peer(111).boot=55;model.peer(111).source_epoch=3;};
  observe(1000000);model.peer(111).boot=0;model.peer(111).source_epoch=0;auto d=gd(1000000);v12::td_discovery(d);v12::td_mesh_poll(1000000);
  assert(!mesh.reference.reported); // GD waits for observed authenticated scope.
  d=gs(1100000);v12::td_discovery(d);v12::td_mesh_poll(2000000);
  assert(mesh.roles.accepted==1&&mesh.roles.epoch_since_us==1100000&&mesh.reference.accepted==1);
  assert(mesh.reference.hdop_centi==75&&mesh.reference.sats==12&&mesh.reference.values==7&&mesh.reference.wire_version==2);
  assert(v12::td_mesh::current(mesh.reference,mesh.selected,2000000));
  const auto first=mesh.reference.received_us,until=mesh.reference.until_us;
  d=gd(3000000);v12::td_discovery(d);v12::td_mesh_poll(3000000);
  assert(mesh.reference.received_us==first&&mesh.reference.until_us==until&&mesh.reference.accepted==1);
  assert(!v12::td_mesh::live(mesh.roles,3100000)); // Exact GS TTL, no display hold.
  ++q.sequence;q.sampled_us+=1000000;d=gd(4000000);d.bytes[74]^=1;v12::td_discovery(d);v12::td_mesh_poll(4000000);
  assert(mesh.reference.rejected_auth==1&&mesh.reference.until_us==until);
  d=gd(5000000);d.address.val[0]^=1;v12::td_discovery(d);v12::td_mesh_poll(5000000);
  assert(mesh.reference.accepted==1); // Canonical advertiser mandatory.
  d=gd(6000000);v12::td_discovery(d);v12::td_mesh_poll(6000000);assert(mesh.reference.accepted==2);
  q.sequence=1;d=gd(7000000);v12::td_discovery(d);v12::td_mesh_poll(7000000);assert(mesh.reference.replayed==1);
  q.sequence=3;++q.mesh_epoch;d=gd(8000000);v12::td_discovery(d);v12::td_mesh_poll(8000000);assert(mesh.reference.rejected_scope==1);
  --q.mesh_epoch;++q.sequence;d=gd(10000000);v12::td_discovery(d);v12::td_mesh_poll(9000000);assert(mesh.reference.rejected_age==1); // Future receipt rejected.
  // Legacy GD1 is authenticated using its own domain; it cannot invent HDOP.
  ++q.sequence;q.known&=~R2_GNSS_K_HDOP;q.hdop_centi=UINT16_MAX;
  d=gd(10000000);d.length=R2_GNSS_LEGACY_AD_BYTES;d.bytes[0]=d.length-1;d.bytes[6]=1;
  assert(!r2_group_mac(d.bytes+R2_GNSS_LEGACY_AD_BODY,&group,"R2GNDG01",d.bytes,R2_GNSS_LEGACY_AD_BODY));
  observe(10000000);v12::td_discovery(d);v12::td_mesh_poll(10000000);
  assert(mesh.reference.accepted==3&&mesh.reference.wire_version==1&&mesh.reference.hdop_centi==UINT16_MAX&&!(mesh.reference.fields&v12::td_mesh::Hdop));
  // Same mesh label survives a new primary; changing the label resets seen age.
  v12::td_mesh::RoleMonitor roles;
  auto feed=[&](uint64_t at){auto bytes=gs(at);return roles.receive(bytes.bytes,bytes.length,group,r.root,{r.root,r.root_boot,r.source_epoch},at,at);};
  assert(feed(1000000));++r.sequence;r.created_us+=1000000;assert(feed(2000000));assert(roles.snapshot(2000000).epoch_since_us==1000000);
  r.sender=r.root=333;r.sender_boot=r.root_boot=77;r.sequence=1;assert(feed(3000000));assert(roles.snapshot(3000000).epoch_since_us==1000000);
  ++r.sequence;++r.source_epoch;assert(feed(4000000));assert(roles.snapshot(4000000).epoch_since_us==4000000);
  r.sender=r.root=111;r.sender_boot=r.root_boot=55;r.sequence=1;r.source_epoch=3;
  assert(!feed(5000000)&&roles.snapshot(5000000).replayed==1); // Fence survives root replacements.
  // Missing scans withdraw the observed selection; cached diagnostics remain history.
  v12::td_mesh_poll(40000000);assert(!mesh.selected.root&&!v12::td_mesh::current(mesh.reference,mesh.selected,40000000));
  assert(mesh.reference.until_us==40000000&&mesh.reference.received_us==10000000);
 }else return 2;
 // Synthetic PSRAM is explicitly freed for sanitizer ownership checks.
 v12::td_storage->~TdStorage();heap_caps_free(v12::td_storage);v12::td_storage=nullptr;
 std::printf("PASS actual SS3 native %s\n",which.c_str());
}
