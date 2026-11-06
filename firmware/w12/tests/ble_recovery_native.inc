v12::Discovery locator(uint64_t id,int8_t rssi){
  v12::Discovery d{};d.props=1;d.sid=12;d.interval=50;d.rssi=rssi;d.at=fake_now;d.length=25;
  d.bytes[0]=24;d.bytes[1]=0xff;auto* b=d.bytes+2;b[0]='G';b[1]='1';b[2]=1;
  r2_group_put(b+3,v12::provision.group.id,8);r2_group_put(b+11,v12::provision.group.epoch,4);r2_group_put(b+15,id,8);d.address=v12::canonical(id);return d;
}
// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual shared host owner and rebind/drain, with synthetic controller APIs.
void startup_slow_case(const std::string& which){
 fake_now=100000000;v12::window_start=v12::G1Window::start(fake_now);
 v12::parent_selection.seen(501,false,-30,fake_now-v12::G1ParentSelection::StartupCollectUs);
 auto d=locator(501,-30);v12::discoveries.push(d);v12::active_poll(fake_now);
 assert(v12::peer.connecting&&v12::peer.identity==501);
 ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;v12::gap(&e,conn_arg);step(3);
 r2_join_record request{};assert(!r2_group_join_decode(&request,output.back().data(),R2_GROUP_JOIN_BYTES,&v12::provision.group));
 r2_join_record challenge{};challenge.kind=R2_JOIN_CHALLENGE;challenge.sender=501;challenge.receiver=v12::provision.identity;
 challenge.sender_boot=601;challenge.receiver_boot=v12::boot_id;challenge.client_nonce=request.client_nonce;challenge.server_nonce=99;
 challenge.sequence=1;challenge.created_us=fake_now+10000;challenge.echo_us=request.created_us;std::strcpy(challenge.name,"GM");inject(challenge);
 r2_join_record proof{};assert(!r2_group_join_decode(&proof,output.back().data(),R2_GROUP_JOIN_BYTES,&v12::provision.group));
 auto accept=challenge;accept.kind=R2_JOIN_ACCEPT;accept.sequence=2;accept.created_us+=10000;accept.echo_us=proof.created_us;
 accept.source_flags=1;accept.source_bound_us=40000;accept.root=501;accept.root_boot=601;
 accept.source_epoch=2;accept.publication=3;accept.utc_us=1700000000000000;accept.remaining_us=1042399;
 const auto sent=v12::peer.exchange.proof_at;
 fake_now=sent+(which=="startup-slow-expired"?250000:160761);
 if(which=="startup-slow-short")accept.remaining_us=201147;
 const auto received=fake_now;inject(accept,which=="startup-slow-mac");
 if(which!="startup-slow"){
  assert(!v12::anchor.source&&!v12::following(fake_now)&&!v12::corrected_clock.until());
  std::puts("PASS delayed startup still refuses expired/short-grant/unauthenticated admission");return;
 }
 assert(v12::anchor.source&&v12::anchor.root==501&&v12::anchor.boot==601);
 assert(v12::anchor.uncertainty==120767&&v12::anchor.at==received);
 assert(v12::anchor.until==received+1042399-201147&&v12::following(fake_now));
 assert(!v12::corrected_clock.accepted()&&!v12::corrected_clock.until()&&!v12::lock_protected(fake_now));
 assert(!v12::admission_policy.unusable&&!v12::fault);
 std::puts("PASS actual authenticated160ms startup JOIN binds coarse clock with full uncertainty/expiry and zero precision authority");
}
void startup_unusable_case(){
 fake_now=100000000;v12::window_start=v12::G1Window::start(fake_now);
 v12::parent_selection.seen(501,false,-30,fake_now-v12::G1ParentSelection::StartupCollectUs);
 auto d=locator(501,-30);v12::discoveries.push(d);v12::active_poll(fake_now);
 assert(v12::peer.connecting&&v12::peer.identity==501);
 ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;v12::gap(&e,conn_arg);step(3);
 r2_join_record request{};assert(!r2_group_join_decode(&request,output.back().data(),R2_GROUP_JOIN_BYTES,&v12::provision.group));
 r2_join_record challenge{};challenge.kind=R2_JOIN_CHALLENGE;challenge.sender=501;challenge.receiver=v12::provision.identity;
 challenge.sender_boot=601;challenge.receiver_boot=v12::boot_id;challenge.client_nonce=request.client_nonce;challenge.server_nonce=99;
 challenge.sequence=1;challenge.created_us=fake_now+10000;challenge.echo_us=request.created_us;std::strcpy(challenge.name,"NoTime");inject(challenge);
 r2_join_record proof{};assert(!r2_group_join_decode(&proof,output.back().data(),R2_GROUP_JOIN_BYTES,&v12::provision.group));
 auto accept=challenge;accept.kind=R2_JOIN_ACCEPT;accept.sequence=2;accept.created_us+=10000;accept.echo_us=proof.created_us;
 inject(accept);step(3);assert(!v12::anchor.source&&!v12::following(fake_now)&&v12::peer.handle==v12::None);
 assert(v12::admission_policy.no_source==1&&v12::cooled(501,fake_now));
 // Retain the ordinary failed-exchange backoff; only the spurious minute
 // reservation is removed. Both locators must be observed afresh afterward.
 fake_now+=500000;d=locator(501,-30);v12::discoveries.push(d);d=locator(777,-80);v12::discoveries.push(d);v12::active_poll(fake_now);
 assert(v12::peer.connecting&&v12::peer.identity==777&&connected==2&&!v12::following(fake_now));
 std::puts("PASS actual authenticated NO_SOURCE first JOIN leaves fresh alternate immediately selectable; no inherited clock or authority");
}
void acquisition_case(const std::string& which){
 constexpr uint64_t start=100000000,T=v12::BleAcquisitionPolicy::StallUs;
 fake_now=start;
 auto nearby=[](){auto d=locator(501,-40);v12::observe_group(d);v12::locator_ids[0]=501;v12::locators[0]=d;};
 nearby();v12::ble_acquisition_poll(fake_now);assert(!v12::parent_diagnostic.pending());
 v12::parent_selection.seen(501,false,-40,fake_now);
 v12::parent_selection.failed(501,false,fake_now);
 v12::parent_diagnostic.target=501;v12::parent_diagnostic.relay=false;
 v12::admission_policy.starts[0]=123;v12::admission_policy.starts[1]=456;
 const auto starts=v12::admission_policy.starts[1];
 if(which=="ble-acquisition-pin"||which=="ble-acquisition-relay-only"){
  if(which=="ble-acquisition-pin")v12::parent_selection.target(777,false);
  else v12::exclude_direct_gm=true;
  fake_now=start+T;nearby();v12::ble_acquisition_poll(fake_now);
  assert(!v12::parent_diagnostic.pending()&&!v12::ble_acquisition_recovery.rebinds());return;
 }
 if(which=="ble-acquisition-progress"){
  for(unsigned i=0;i<1000;++i){fake_now+=62500;nearby();v12::Rc7PermissionInspect::physical_update(v12::corrected_clock);v12::ble_acquisition_poll(fake_now);}
  assert(!v12::ble_acquisition_recovery.rebinds()&&!v12::parent_diagnostic.pending());return;
 }
 fake_now=start+T-1;nearby();v12::ble_acquisition_poll(fake_now);assert(!v12::parent_diagnostic.pending());
 if(which=="ble-acquisition-off"){
  v12::r2_stop();fake_now=start+T;nearby();v12::ble_acquisition_poll(fake_now);
  assert(!v12::parent_diagnostic.pending()&&!v12::ble_acquisition_recovery.rebinds());return;
 }
 if(which=="ble-acquisition-fault"){
  v12::error(7,v12::R2Api::AdvData);fake_now=start+T;nearby();v12::ble_acquisition_poll(fake_now);
  assert(v12::fault&&!v12::parent_diagnostic.pending()&&!v12::ble_acquisition_recovery.rebinds());return;
 }
 if(which=="ble-acquisition-absence"){
  fake_now=start+T+v12::BleAcquisitionPolicy::PeerFreshUs;v12::ble_acquisition_poll(fake_now);
  assert(!v12::parent_diagnostic.pending()&&!v12::ble_acquisition_recovery.rebinds());return;
 }
 if(which=="ble-acquisition-busy"){
  v12::peer.connecting=true;fake_now=start+T;nearby();v12::ble_acquisition_poll(fake_now);
  assert(!v12::parent_diagnostic.pending());v12::peer.connecting=false;
 }
 if(which!="ble-acquisition-startup"){
  // An existing PA may still deliver packets while its clock map has stalled.
  v12::sync_identity=v12::joined_peer=501;v12::joined_boot=601;
  v12::sync_address=v12::sync_owner_address=v12::canonical(501);
  v12::synced=true;v12::sync_handle=9;
  if(which=="ble-acquisition-drain-failure")sync_terminate_error=BLE_HS_ETIMEOUT;
 }
 fake_now=start+T;nearby();v12::ble_acquisition_poll(fake_now);
 assert(v12::parent_diagnostic.pending()&&v12::ble_acquisition_recovery.rebinds()==1);
 const auto deadline=v12::parent_diagnostic.deadline_us;
 assert(deadline==fake_now+2000000&&!v12::r2_status().following);
 v12::r2_host_poll();pump_cleanup();v12::r2_host_poll();
 if(which=="ble-acquisition-drain-failure"){
  fake_now=deadline;v12::r2_host_poll();
  assert(v12::fault&&v12::parent_diagnostic.state!=3&&v12::parent_diagnostic.deadline_us==deadline);return;
 }
 assert(!v12::fault&&v12::parent_diagnostic.state==3&&v12::parent_diagnostic.completed_us<deadline);
 assert(v12::parent_diagnostic.target==501&&!v12::parent_diagnostic.relay);
 assert(!v12::synced&&v12::sync_handle==v12::None&&!v12::anchor.source&&!v12::corrected_clock.until());
 assert(!v12::parent_selection.history().entries(fake_now)&&v12::admission_policy.starts[1]==starts);
 assert(v12::r2_status().w12_ble_recoveries==1&&!v12::r2_status().following);
 // A new locator starts a new authenticated exchange, never an old map.
 fake_now+=50000;v12::parent_selection.seen(501,false,-40,fake_now-v12::G1ParentSelection::StartupCollectUs);
 auto d=locator(501,-40);v12::discoveries.push(d);v12::active_poll(fake_now);
 assert(v12::peer.connecting&&v12::peer.identity==501&&!v12::following(fake_now));
 std::puts("PASS actual BLE-only bounded rebind; old PA closed, history retired, pin/rate gates retained, fresh admission required");
}

void acquisition_pa_case(const std::string& which){
 constexpr uint64_t start=100000000;fake_now=start;
 v12::anchor={true,501,601,UINT64_C(1700000000000000)+fake_now,fake_now,fake_now+1500000,2,3,1000};
 v12::joined_peer=v12::sync_identity=501;v12::joined_boot=601;v12::member=true;
 v12::synced=true;v12::sync_handle=9;v12::sync_capture_epoch=9;
 for(unsigned i=0;i<=15;++i){
  fake_now=start+i*1000000;
  v12::Periodic row{};row.at=fake_now;row.identity=501;row.length=158;row.signal_epoch=which=="pa-old-capture"?8:9;
  r2_precision_packet p{};p.boot=601;p.created_ns=fake_now*1000;p.expires_ns=p.created_ns+1500000000;
  p.utc_offset_ns=INT64_C(1700000000000000000);p.source_epoch=2;p.train=p.publication=p.capture_epoch=3;p.flags=1;
  const uint64_t ids[4]={501,0,0,0};assert(!r2_group_precision_encode(row.bytes,&p,&v12::provision.group,ids));
  if(which=="pa-bad-mac")row.bytes[157]^=1;
  assert(v12::periodic.push(row));v12::periodic_poll(fake_now);v12::ble_acquisition_poll(fake_now);
 }
 assert(!v12::corrected_clock.accepted());
 if(which=="pa-authenticated")assert(v12::parent_diagnostic.pending()&&v12::ble_acquisition_recovery.rebinds()==1);
 else assert(!v12::parent_diagnostic.pending()&&!v12::ble_acquisition_recovery.rebinds());
 std::puts("PASS actual authenticated PA/no-physical-progress supervision; bad MAC and retired capture epochs never count as nearby parent");
}
void permission_gap_case(){
 fake_now=100000000;v12::window_start=v12::G1Window::start(fake_now);
 v12::anchor={true,501,601,UINT64_C(1700000000000000)+fake_now,fake_now,fake_now+1500000,2,3,1000};
 v12::joined_peer=v12::sync_identity=501;v12::joined_boot=601;v12::member=true;
 v12::parent_selection.bind(501,601,2,fake_now);v12::parent_selection.tick(fake_now,true,false);v12::parent_selection.delivered(3,10,fake_now);
 v12::sync_address=v12::canonical(501);v12::synced=true;v12::sync_handle=9;
 v12::Rc7PermissionInspect::seed(v12::corrected_clock,v12::anchor,fake_now);v12::Rc7PermissionInspect::allow_lock(v12::corrected_clock);
 v12::anchor.until=fake_now;v12::Rc7PermissionInspect::expire(v12::corrected_clock,fake_now);v12::parent_selection.delivered(3,11,fake_now);
 assert(!v12::following(fake_now)&&v12::selection_protected(fake_now));
 auto d=locator(777,-20);v12::discoveries.push(d);v12::active_poll(fake_now);
 assert(v12::joined_peer==501&&v12::synced&&v12::sync_handle==9&&!v12::peer.connecting);
    // Reproduce a late permission drought with continuing authenticated PA.
    // The1.5s coarse rebootstrap rule must not destroy that acquisition before
    // its independent15s physical-progress supervisor can recover it.
    const auto until=v12::anchor.until;v12::retry_at=UINT64_MAX;
    fake_now+=2000000;v12::parent_selection.delivered(3,12,fake_now);
    assert(v12::selection_protected(fake_now));v12::active_poll(fake_now);
    assert(v12::anchor.root==501&&v12::joined_peer==501&&v12::synced&&!v12::fault);
    assert(v12::anchor.until==until&&!v12::following(fake_now));
    // The scheduling reservation ends at the physical-stall bound. It
    // supplies no inherited UTC, renewed grant or infinite incumbent hold.
    fake_now=until+15000000;v12::parent_selection.delivered(3,13,fake_now);
    assert(!v12::following(fake_now)&&!v12::selection_protected(fake_now));
    v12::active_poll(fake_now);assert(!v12::anchor.source&&!v12::joined_peer);
    puts("PASS permission drought withdraws output immediately, retains fresh authenticated direct acquisition until bounded15s stall, then retires it");
}
int main(int argc,char** argv){
 assert(argc==2);const std::string which=argv[1];const uint8_t mac[]={2,3,4,5,6,7};
 assert(v12::r2_init(44,mac)&&v12::r2_host_init()&&v12::r2_host_sync());step(8);v12::stop_scan();
 if(which=="permission-gap-parent")permission_gap_case();
 else if(which=="startup-unusable")startup_unusable_case();
 else if(which.starts_with("startup-slow"))startup_slow_case(which);
 else if(which.starts_with("pa-"))acquisition_pa_case(which);
 else acquisition_case(which);
}
