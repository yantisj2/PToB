// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
void bind_relay_return(){
  rc7_control cap{};cap.type=RC7_CAPABILITY;cap.sender=333;cap.sender_boot=444;cap.next_challenge=55;
  cap.path.depth=1;cap.path.source_epoch=7;cap.path.node[0]={555,666,1,3,0,0};cap.path.node[1]={333,444,2,2,1,1};
  assert(v12::rc_client.make_request(cap,v12::provision.identity,v12::boot_id,99,fake_now,false));
  auto reply=v12::rc_client.request;reply.type=RC7_RESPONSE;reply.sender=333;reply.target=v12::provision.identity;
  reply.sender_boot=444;reply.target_boot=v12::boot_id;reply.ready=true;reply.next_challenge=56;reply.publication_floor=10;
  reply.root_lo_ns=fake_now*1000-1000000;reply.root_hi_ns=fake_now*1000+1000000;reply.source_expires_ns=fake_now*1000+1500000000;
  v12::peer={};v12::peer.identity=333;v12::peer.handle=22;v12::peer.relay=true;v12::peer.stage=6;
  v12::peer.address=v12::canonical(333);v12::peer.incarnation=++v12::incarnation;conn_arg=v12::callback_arg();
  v12::Incoming row{};row.at=fake_now;row.incarnation=v12::peer.incarnation;row.length=212;
  assert(!rc7_control_encode(row.bytes,&reply,&v12::provision.group));v12::receive_rc_control(row,fake_now);
  assert(!v12::rc9_bound()&&v12::rc_bound()&&v12::peer.handle==22&&v12::peer.selection_accepted);
  assert(v12::anchor.root==555&&v12::anchor.boot==666&&!v12::corrected_clock.prediction().tick&&!v12::fault);
}
void client_case(bool normal_pin=false,const std::string& recovery=""){
 const auto* mac=v12::native_device_binding()->factory;assert(v12::r2_init(44,mac)&&v12::r2_host_init()&&v12::r2_host_sync());step(8);
 if(normal_pin){assert(v12::r2_gm_parent(11));step(8);}
 if(recovery=="cache-fence"){
  v12::parent_selection.authenticated(333,false,444,333,444,7,fake_now);
  v12::parent_selection.failed(333,false,fake_now);
  assert(v12::parent_selection.history().entries(fake_now)==1);
 }

 v12::Discovery d{};d.sid=15;d.props=1;d.rssi=-30;d.at=fake_now;d.address=v12::canonical(77);d.length=167;
 d.bytes[0]=2;d.bytes[1]=1;d.bytes[2]=6;d.bytes[3]=17;d.bytes[4]=7;std::memcpy(d.bytes+5,v12::rc9_service.value,16);d.bytes[21]=145;d.bytes[22]=255;
 v12::rc9::Control cap;cap.sender=77;cap.sender_boot=88;cap.next_challenge=99;
 assert(v12::rc9::encode_capability(d.bytes+23,cap,v12::provision.group));
 v12::discoveries.push(d);step(21);
 if(normal_pin){assert(!v12::peer.connecting&&!v12::rc9_bound());assert(v12::r2_rc9_parent(77));step(8);d.at=fake_now;v12::discoveries.push(d);step(21);}
 assert(v12::peer.connecting&&v12::peer.rc9);
 ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;v12::gap(&e,conn_arg);step(18);
 assert(!v12::fault&&v12::rc9_bound()&&v12::sync_pending&&v12::sync_selected_sid==14);
 if(recovery=="cache-fence")assert(!v12::parent_selection.history().entries(fake_now));

 if(recovery=="pending-relay-return"){
  const auto old_address=v12::sync_address;const auto old_epoch=v12::sync_capture_epoch;
  bind_relay_return();const auto drain=v12::sync_drain_until;
  assert(v12::sync_pending&&v12::sync_cleanup_requested&&drain);
  // Establishment won the cancellation race for the old RC9 address. It is
  // still the old exact SDK owner and may only drain, never regain capture.
  ble_gap_event late{};late.type=BLE_GAP_EVENT_PERIODIC_SYNC;late.periodic_sync.adv_addr=old_address;
  late.periodic_sync.sid=14;late.periodic_sync.per_adv_ival=50;
  v12::gap(&late,reinterpret_cast<void*>(uintptr_t(old_epoch)));
  assert(!v12::fault&&v12::synced&&!v12::periodic_rx::armed&&v12::sync_drain_until==drain);
  const auto reports=v12::pa_reports;uint8_t old_wire[190]{};
  ble_gap_event report{};report.type=BLE_GAP_EVENT_PERIODIC_REPORT;
  report.periodic_report.data_length=190;report.periodic_report.data=old_wire;
  v12::gap(&report,reinterpret_cast<void*>(uintptr_t(old_epoch)));
  assert(v12::pa_reports==reports); // The fence precedes the asynchronous drain.
  pump_cleanup();assert(!v12::synced&&!v12::sync_pending&&!v12::fault&&v12::sync_drain_until==drain);
  v12::r2_stop();step(8);assert(v12::r2_status().off_ack);return;
 }
 sync_established();assert(v12::synced&&!v12::fault&&v12::periodic_rx::armed);
 v12::rc9::Time p;p.sender=77;p.sender_boot=88;p.publication=10;p.sampled_us=fake_now;p.edge_us=fake_now-100;
 p.until_us=p.edge_us+12000000;p.event_ns=1700000000000000000ULL;p.root=66;p.emitter=55;p.kind=2;
 p.source_epoch=p.generation=p.corrected_sequence=1;p.context[0]=1;p.created_ns=p.event_ns-100;p.expires_ns=p.event_ns+1000000;p.valid=true;
 uint8_t wire[190];assert(v12::rc9::encode_time(wire,p,v12::provision.group));
 e={};e.type=BLE_GAP_EVENT_PERIODIC_REPORT;e.periodic_report.data_length=190;e.periodic_report.data=wire;
 v12::gap(&e,fixture_sync_arg);step(2);const auto r=v12::r2_rc9_status();const auto normal=v12::r2_status();
 assert(r.valid&&r.following&&r.server==77&&r.root==66&&r.accepted==1&&!r.serving&&!r.clients);
 assert(!normal.following&&!normal.clock_valid&&!normal.g1_clock_model.tick&&!normal.discipline_accepted&&!pa_active);
 if(recovery=="relay-return"){
  // A normal RC7 response must survive the complete old-RC9 cache fence.
  // Its authenticated current ACL/request is retained while the old PA drains.
  bind_relay_return();
 }else if(!recovery.empty()&&recovery!="cache-fence"){
  const auto original_lease=v12::rc9_client.lease_until;
  const auto refresh=[&](){
   p.publication++;p.corrected_sequence++;p.sampled_us=fake_now;p.edge_us=fake_now-100;
   p.until_us=p.edge_us+12000000;p.event_ns=1700000000000000000ULL+fake_now*1000;
   p.created_ns=p.event_ns-100;p.expires_ns=p.event_ns+1000000;
   assert(v12::rc9_client.receive(p,fake_now));
  };
  if(recovery=="scan"){
   v12::active_poll(fake_now);pump_cleanup();assert(!scan_active);
   fake_now+=v12::G1GmDiscovery::IntervalUs+1;refresh();
   v12::active_poll(fake_now);pump_cleanup();
   assert(scan_active&&v12::gm_discovery.active(fake_now));
   assert(v12::rc9_client.view(fake_now).valid&&v12::rc9_client.lease_until==original_lease);
  }else{
   if(recovery=="expired")fake_now+=12000001;
   const bool had_time=v12::rc9_client.view(fake_now).valid;
   assert(had_time==(recovery!="expired"));
   // Real fresh discovery is still required; an RC9 lease grants no normal
   // timing authority. Do not bypass startup collection or admission budgets.
   v12::Discovery gm{};gm.props=1;gm.length=25;gm.rssi=-50;gm.address=v12::canonical(333);
   gm.bytes[0]=24;gm.bytes[1]=255;auto* b=gm.bytes+2;b[0]='G';b[1]='1';b[2]=1;
   r2_group_put(b+3,v12::provision.group.id,8);r2_group_put(b+11,v12::provision.group.epoch,4);r2_group_put(b+15,333,8);
   const auto end=fake_now+4000000;
   while(fake_now<end&&!v12::peer.connecting){
    gm.at=fake_now;v12::discoveries.push(gm);v12::active_poll(fake_now);pump_cleanup();fake_now+=20000;
   }
   if(recovery=="pin")assert(!v12::peer.connecting&&v12::rc9_client.view(fake_now).bound);
   else{
    assert(v12::peer.connecting&&!v12::peer.rc9&&v12::peer.identity==333);
    assert(v12::rc9_client.lease_until==original_lease&&v12::rc9_client.view(fake_now).bound);
    assert(!v12::anchor.source&&!v12::corrected_clock.prediction().tick);
    if(recovery=="no-source"||recovery=="hint"){
     const auto old=v12::rc9_client.view(fake_now);const auto old_until=old.until_us;
     ble_gap_event connect{};connect.type=BLE_GAP_EVENT_CONNECT;v12::gap(&connect,conn_arg);step(3);
     r2_join_record request{};assert(!r2_group_join_decode(&request,output.back().data(),R2_GROUP_JOIN_BYTES,&v12::provision.group));
     r2_join_record reply{};reply.kind=R2_JOIN_CHALLENGE;reply.sender=333;reply.receiver=v12::provision.identity;
     reply.sender_boot=444;reply.receiver_boot=v12::boot_id;reply.client_nonce=request.client_nonce;
     reply.server_nonce=99;reply.sequence=1;reply.created_us=fake_now+10000;reply.echo_us=request.created_us;
     std::strcpy(reply.name,"SourceProbe");inject(reply);
     r2_join_record proof{};assert(!r2_group_join_decode(&proof,output.back().data(),R2_GROUP_JOIN_BYTES,&v12::provision.group));
     reply.kind=R2_JOIN_ACCEPT;reply.sequence=2;reply.created_us+=10000;reply.echo_us=proof.created_us;
     if(recovery=="hint"){
      reply.source_flags=1;reply.source_bound_us=40000;reply.root=555;reply.root_boot=666;
      reply.source_epoch=7;reply.publication=8;reply.utc_us=1700000000000000+fake_now;reply.remaining_us=1000000;
     }
     inject(reply);const auto kept=v12::rc9_client.view(fake_now);
     assert(kept.bound&&kept.valid&&kept.server==old.server&&kept.until_us==old_until);
     assert(v12::rc9_client.lease_until==original_lease&&v12::joined_peer==old.server);
     assert(!v12::anchor.source&&!v12::following(fake_now));
    }else{
     fresh_direct(333,444,7,8);
     assert(!v12::rc9_client.view(fake_now).bound&&!v12::r2_rc9_status().valid);
     assert(v12::anchor.root==333&&v12::anchor.boot==444&&v12::anchor.source_epoch==7);
    }
    assert(!v12::corrected_clock.prediction().tick); // JOIN is coarse permission only.
   }
  }
 }
 v12::r2_stop();assert(!v12::r2_rc9_status().valid);step(8);assert(!v12::rc9_bound()&&!v12::r2_rc9_status().valid&&v12::r2_status().off_ack);
 puts("PASS actual RC9 discovery/GATT/SID14 capture/report/UTC isolation/OFF");
}
