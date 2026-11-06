// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual shared host owner and rebind/drain, with synthetic controller APIs.
void startup_slow_case(const std::string& which){
 fake_now=100000000;v12::window_start=v12::G1Window::start(fake_now);
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
 constexpr uint64_t start=100000000,T=n30::BleAcquisitionRecovery::StallUs;
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
  fake_now=start+T+n30::BleAcquisitionRecovery::PeerFreshUs;v12::ble_acquisition_poll(fake_now);
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
 assert(v12::r2_status().n30_ble_recoveries==1&&!v12::r2_status().following);
 // A new locator starts a new authenticated exchange, never an old map.
 auto d=locator(501,-40);v12::discoveries.push(d);fake_now+=50000;v12::active_poll(fake_now);
 assert(v12::peer.connecting&&v12::peer.identity==501&&!v12::following(fake_now));
 std::puts("PASS actual BLE-only bounded rebind; old PA closed, history retired, pin/rate gates retained, fresh admission required");
}
