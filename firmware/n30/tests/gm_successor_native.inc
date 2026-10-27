// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
v12::Discovery successor_ad(uint64_t sender,uint64_t boot,uint64_t seq,bool negative=false,bool yield=false){
  gm7_packet p{};p.group=v12::provision.group.id;p.key_epoch=v12::provision.group.epoch;
  p.kind=negative?GM7_WITHDRAW:GM7_ANNOUNCE;p.flags=negative?0:GM7_ELIGIBLE;
  auto& c=p.body.control;c.sender=c.active=sender;c.boot=c.active_boot=boot;c.sequence=seq;
  c.sent_utc_us=negative?0:v12::anchor.utc+fake_now-v12::anchor.at;c.sent_mono_us=fake_now-95000000;
  c.max_age_us=negative?1000000:2750000;c.source_valid_us=negative?0:1500000;
  c.source_gen=4;c.capture_gen=5;c.train_gen=sender==501?3:9;c.timing_epoch=sender==501?2:4;c.birth_us=1700000000000000;c.founder=501;c.term=sender==501?2:3;
  if(sender==501){c.hot=777;c.hot_boot=877;}
  if(yield){p.flags|=GM7_YIELD;c.yield_utc_us=c.sent_utc_us+750000;}
  v12::Discovery d{};d.sid=14;d.address=v12::canonical(sender);d.at=fake_now;d.rssi=-70;
  assert(!gm7_ad_encode(d.bytes,sizeof d.bytes,&p,&v12::provision.group));d.length=GM7_CONTROL_BYTES+4;return d;
}
v12::Discovery locator(uint64_t id,int8_t rssi){
  v12::Discovery d{};d.props=1;d.sid=12;d.interval=50;d.rssi=rssi;d.at=fake_now;d.length=25;
  d.bytes[0]=24;d.bytes[1]=0xff;auto* b=d.bytes+2;b[0]='G';b[1]='1';b[2]=1;
  r2_group_put(b+3,v12::provision.group.id,8);r2_group_put(b+11,v12::provision.group.epoch,4);r2_group_put(b+15,id,8);d.address=v12::canonical(id);return d;
}
int main(int argc,char** argv){
  assert(argc==2);const std::string which=argv[1];const uint8_t mac[]={2,3,4,5,6,7};
  assert(v12::r2_init(44,mac)&&v12::r2_host_init()&&v12::r2_host_sync());step(8);v12::stop_scan();
  fake_now=100000000;v12::window_start=v12::G1Window::start(fake_now);
  v12::anchor={true,501,601,UINT64_C(1700000000000000)+fake_now,fake_now,fake_now+1500000,2,3,1000};
  v12::joined_peer=v12::sync_identity=501;v12::joined_boot=601;v12::member=true;
  v12::parent_selection.bind(501,601,2,fake_now);v12::parent_selection.tick(fake_now,true,false);v12::parent_selection.delivered(3,10,fake_now);
  v12::sync_address=v12::canonical(501);v12::synced=true;v12::sync_handle=9;
  v12::Rc7PermissionInspect::seed(v12::corrected_clock,v12::anchor,fake_now);v12::admission_policy.acquired();
  v12::Rc7PermissionInspect::allow_lock(v12::corrected_clock);assert(v12::selection_protected(fake_now));
  const auto at=fake_now;
#ifdef PTOB_N30
  if(which=="handoff-join"||which=="handoff-clock-step"){
    v12::Rc7PermissionInspect::physical_update(v12::corrected_clock);
    v12::publish(fake_now);assert(v12::retained_estimate.trained);
    const auto held=v12::retained_estimate.clock;
    v12::forget_leader(fake_now);pump_cleanup();step(8);
    assert(!v12::anchor.source&&!v12::corrected_clock.prediction().tick);
    if(which=="handoff-clock-step")v12::changed_scope(fake_now,v12::G1Reason::PeriodicScope,false,v12::G1ParentReason::ClockStep);
    fake_now+=1000000;v12::window_start=v12::G1Window::start(fake_now);
    fresh_direct(777,877,4,9,which=="handoff-join");v12::publish(fake_now);
    const auto status=v12::r2_status();
    assert(v12::anchor.root==777&&v12::anchor.boot==877&&!v12::fault);
    assert(!v12::corrected_clock.covers(fake_now,fake_now));
    if(which=="handoff-join"){
      assert(status.clock_valid&&!status.following&&status.g1_retained_estimate);
      assert(status.g1_clock_origin==501&&status.g1_clock_model.at(fake_now*20)==held.at(fake_now*20));
      assert(!status.root_identity&&!status.source_epoch&&!status.service_until_us);
    }else assert(!status.g1_retained_estimate&&!v12::retained_estimate.root);
    std::puts("PASS actual N30 new JOIN holds old actuator without new-root clock authority; clock-step fence clears carry");return 0;
  }
#endif
  if(which=="scan"){
    v12::gm_control_capture20=fake_now*20;v12::active_poll(fake_now);assert(!scan_active);
    fake_now=at+32000;v12::active_poll(fake_now);assert(scan_active&&v12::scan&&!connected);
    fake_now=at+50000;v12::active_poll(fake_now);assert(!scan_active&&!v12::scan&&!v12::fault);
    assert(v12::anchor.root==501&&v12::following(fake_now));
  }else{
    if(which=="pin")v12::parent_selection.target(501,false);
    if(which=="relay-only")v12::exclude_direct_gm=true;
    auto ann=successor_ad(501,601,1,false,which=="yield");
    if(which=="bad-mac")ann.bytes[ann.length-1]^=1;
    if(which=="bad-address")ann.address=v12::canonical(502);
    assert(v12::discoveries.push(ann));v12::active_poll(fake_now);
    if(which=="bad-mac"||which=="bad-address"){
      auto observer=v12::gm_successor;assert(observer.lost(v12::anchor,fake_now)==0);
      assert(v12::following(fake_now)&&!connected&&!v12::fault);return 0;
    }
    auto observer=v12::gm_successor;assert(observer.lost(v12::anchor,fake_now)==777);
    assert(v12::following(fake_now)&&v12::joined_peer==501&&!connected);
    if(which=="yield"){
      fake_now=at+751099;v12::active_poll(fake_now);assert(v12::anchor.root==501);
      fake_now=at+751100;v12::active_poll(fake_now);
    }else if(which=="loss"){
      fake_now=at+3000000;v12::active_poll(fake_now);
    }else{
      auto event=which=="switchover"?successor_ad(777,877,1):successor_ad(501,601,2,true);
      assert(v12::discoveries.push(event));v12::active_poll(fake_now);
    }
    assert(!v12::anchor.source&&!v12::following(fake_now));
    pump_cleanup();step(8);assert(!v12::synced&&!v12::sync_pending&&!v12::fault);
    auto other=locator(888,-20),hot=locator(777,-90);
    assert(v12::discoveries.push(other));if(which!="fallback")assert(v12::discoveries.push(hot));v12::active_poll(fake_now);
#ifndef PTOB_N30
    fake_now+=40000;v12::active_poll(fake_now); // W12 retains its ordinary locator collection interval.
#endif
    if(which=="pin"||which=="relay-only")assert(!connected);
    else if(which=="fallback"){
      // The nominee's absence has one original3s observation budget.
      assert(!connected); // Fallback case below uses no HOT locator.
      fake_now+=3000000;v12::window_start=v12::G1Window::start(fake_now);
      other=locator(888,-20);assert(v12::discoveries.push(other));v12::active_poll(fake_now);
      assert(connected==1&&v12::peer.identity==888&&!v12::anchor.source&&!v12::fault);
    }else{
      assert(connected==1&&v12::peer.connecting&&v12::peer.identity==777&&!v12::peer.relay);
      assert(!v12::anchor.source&&!v12::corrected_clock.covers(fake_now,fake_now));
    }
  }
  std::printf("PASS production N30 GM successor %s; no clock authority from control/locator\n",which.c_str());
}
