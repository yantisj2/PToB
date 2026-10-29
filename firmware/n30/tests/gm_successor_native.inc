// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
v12::Discovery successor_ad(uint64_t sender,uint64_t boot,uint64_t seq,bool negative=false,bool yield=false){
  gm7_packet p{};p.group=v12::provision.group.id;p.key_epoch=v12::provision.group.epoch;
  p.kind=negative?GM7_WITHDRAW:GM7_ANNOUNCE;p.flags=negative?0:GM7_ELIGIBLE;
  auto& c=p.body.control;c.sender=c.active=sender;c.boot=c.active_boot=boot;c.sequence=seq;
  c.sent_utc_us=negative?0:v12::anchor.utc+fake_now-v12::anchor.at;c.sent_mono_us=fake_now-95000000;
  c.max_age_us=negative?1000000:2750000;c.source_valid_us=negative?0:1500000;
  c.source_gen=4;c.capture_gen=5;c.train_gen=sender==501?3:9;c.timing_epoch=sender==501?2:4;c.birth_us=1700000000000000;c.founder=501;c.term=sender==501?2:3;
  if(sender==501){c.hot=777;c.hot_boot=877;}
#ifdef R2_TZ_WIRE
  c.tz=r2_tz_manual();
#endif
  if(yield){p.flags|=GM7_YIELD;c.yield_utc_us=c.sent_utc_us+750000;}
  v12::Discovery d{};d.sid=14;d.address=v12::canonical(sender);d.at=fake_now;d.rssi=-70;
  assert(!gm7_ad_encode(d.bytes,sizeof d.bytes,&p,&v12::provision.group));d.length=gm7_wire_size(p.kind)+4;return d;
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
#ifdef R2_TZ_WIRE
  if(which=="tz-updates"){
    auto announce=[&](r2_tz zone,uint64_t sequence){
      auto d=successor_ad(501,601,sequence);gm7_packet p{};assert(!gm7_ad_decode(&p,d.bytes,d.length,&v12::provision.group));
      p.body.control.tz=zone;assert(!gm7_ad_encode(d.bytes,sizeof d.bytes,&p,&v12::provision.group));return d;
    };
    auto first=announce({2,-300},1);assert(v12::discoveries.push(first));v12::active_poll(fake_now);assert(v12::coarse_tz.value.offset_min==-300);
    fake_now+=100000;v12::anchor.until=0;
    auto winter=announce({2,-360},2);assert(v12::discoveries.push(winter));v12::active_poll(fake_now);assert(v12::coarse_tz.value.offset_min==-360&&!v12::anchor.until);
    const auto until=v12::coarse_tz.until_us;
    first.at=fake_now;assert(v12::discoveries.push(first));v12::active_poll(fake_now);assert(v12::coarse_tz.value.offset_min==-360&&v12::coarse_tz.until_us==until);
    assert(!v12::coarse_tz_order.bootstrap(v12::coarse_tz,{2,-300},fake_now,fake_now));
    auto unknown=announce(r2_tz_unknown(),3);assert(v12::discoveries.push(unknown));v12::active_poll(fake_now);assert(!v12::coarse_tz.value.id);
    r2_tz_renew(&v12::coarse_tz,fake_now+50000,R2_TZ_GM_PERIOD_US);assert(!v12::coarse_tz.value.id);
    // Actual authenticated capability decoder and current-path/advertiser gates.
    v12::coarse_tz={};v12::coarse_tz_order={};rc7_path path{};path.source_epoch=2;path.depth=1;
    path.node[0]={501,601,3,2,0,0};path.node[1]={777,877,5,8,1,1};assert(rc7_path_valid(&path));
    v12::rc_client.receiver.path=path;v12::rc_client.receiver.clock.bound=true;v12::joined_peer=v12::sync_identity=777;v12::joined_boot=877;
    auto cap=[&](r2_tz zone,uint64_t token){
      rc7_control c{};c.type=RC7_CAPABILITY;c.sender=777;c.sender_boot=877;c.next_challenge=token;c.path=path;c.tz=zone;
      v12::Discovery d{};d.props=1;d.sid=13;d.address=v12::canonical(777);d.at=fake_now;d.length=RC_DYNAMIC_CAPABILITY_BYTES+2;
      d.bytes[0]=RC_DYNAMIC_CAPABILITY_BYTES+1;d.bytes[1]=255;assert(!rc7_capability_encode(d.bytes+2,&c,&v12::provision.group));return d;
    };
    auto c=cap({2,-300},10);v12::coarse_relay_observed(c,fake_now);assert(v12::coarse_tz.value.offset_min==-300);
    c=cap({2,-360},11);v12::coarse_relay_observed(c,fake_now);assert(v12::coarse_tz.value.offset_min==-360);
    const auto deadline=v12::coarse_tz.until_us;c=cap({2,-300},11);v12::coarse_relay_observed(c,fake_now);assert(v12::coarse_tz.value.offset_min==-360&&v12::coarse_tz.until_us==deadline);
    c=cap({2,-300},10);v12::coarse_relay_observed(c,fake_now);assert(v12::coarse_tz.value.offset_min==-360);
    c=cap(r2_tz_unknown(),12);v12::coarse_relay_observed(c,fake_now);assert(!v12::coarse_tz.value.id);
    c=cap({2,-300},13);c.address=v12::canonical(778);v12::coarse_relay_observed(c,fake_now);assert(!v12::coarse_tz.value.id);
    c=cap({2,-300},13);c.bytes[c.length-1]^=1;v12::coarse_relay_observed(c,fake_now);assert(!v12::coarse_tz.value.id);
    v12::changed_scope(fake_now);assert(!v12::coarse_tz.value.id&&!v12::coarse_tz_order.ordered);
    puts("PASS actual ordered direct/relay TZ updates, stale/same-token refusal, JOIN non-downgrade, explicit unknown and parent fencing without UTC permission");return 0;
  }
  if(which=="tz"){
    const auto time_until=v12::anchor.until;
    auto ann=successor_ad(501,601,1);assert(v12::discoveries.push(ann));v12::active_poll(fake_now);
    assert(v12::coarse_tz.value.id==2&&v12::coarse_tz.value.offset_min==-300);
    assert(v12::coarse_tz.until_us==fake_now+750000&&v12::anchor.until==time_until);
    // Authenticated current-parent presence renews TZ before clock/fit permission.
    fake_now+=100000;v12::anchor.until=0;
    const uint64_t ids[4]={501,0,0,0};r2_relay_packet p{};
    p.origin={0,1,601,2,1000000,1651000000,1000000,INT64_C(1699999999999000000)};
    p.route={0,0,0,0,601,3,8,9,3,UINT32_MAX};
    v12::Periodic row{};row.at=fake_now;row.identity=501;row.length=190;uint8_t proof[32]{};
    assert(!r2_group_origin_sign3(proof,&p.origin,&v12::provision.group,ids));
    assert(!r2_group_relay_encode3(row.bytes,&p,proof,&v12::provision.group,ids));
    assert(v12::periodic.push(row));v12::periodic_poll(fake_now);
    const auto deadline=fake_now+750000;assert(v12::coarse_tz.until_us==deadline&&!v12::anchor.until);
    fake_now+=100000;row.at=fake_now;row.bytes[175]^=1;
    assert(v12::periodic.push(row));v12::periodic_poll(fake_now);assert(v12::coarse_tz.until_us==deadline);
    v12::publish(fake_now);assert(v12::r2_status().tz.id==2&&!v12::r2_status().following);
    fake_now=deadline;assert(!v12::r2_status().tz.id); // A stalled worker cannot retain live metadata.
    assert(!v12::anchor.until&&!v12::fault);puts("PASS actual parent ANN/presence coarse TZ renewal without precision permission, bad MAC refusal and hard expiry");return 0;
  }
#endif

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
