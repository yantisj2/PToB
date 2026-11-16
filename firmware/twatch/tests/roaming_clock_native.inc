// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual receiver owner and publication selection with a synthetic counter.
int main(int argc,char** argv){
 assert(argc==2);const std::string which=argv[1];const uint8_t mac[]={2,3,4,5,6,7};
 assert(v12::r2_init(44,mac)&&v12::r2_host_init()&&v12::r2_host_sync());step(8);v12::stop_scan();
 fake_now=100000000;constexpr int64_t Utc=INT64_C(1700000000000000000);
 const v12::G1ClockPrediction trained{fake_now*20,int64_t(fake_now*1000),0,4321,Utc};
 assert(v12::retained_estimate.retain(trained,111,55,2,trained.tick,fake_now,true));
 if(which=="off"){
  v12::r2_stop();step(6);assert(v12::client.off()&&v12::mode==v12::R2Mode::Off);
  assert(v12::retained_estimate.trained&&v12::retained_estimate.clock.rate==4321);
  assert(v12::r2_client_start(999));step(4);
  assert(v12::client.running&&!v12::fault&&!v12::following(fake_now));
 }
 else if(which=="rebind"){
  v12::synced=true;v12::sync_handle=9;v12::sync_identity=v12::joined_peer=501;v12::joined_boot=601;
  v12::sync_address=v12::sync_owner_address=v12::canonical(501);
  assert(v12::r2_rc_parent(777));v12::parent_diagnostic_poll(fake_now);pump_cleanup();v12::parent_diagnostic_poll(fake_now);
  assert(v12::parent_diagnostic.state==3&&!v12::fault&&!v12::following(fake_now));
  assert(v12::retained_estimate.trained&&v12::retained_estimate.clock.rate==4321);
 }
 fake_now+=61000000;
 // Fresh admission alone must leave the trained oscillator in explicit HOLD,
 // rather than displaying a new coarse anchor with its unrelated phase.
 v12::anchor={true,222,66,UINT64_C(1700000000000000)+fake_now+2000000,fake_now,fake_now+1500000,3,4,1000};
 v12::publish(fake_now);auto status=v12::r2_status();
 assert(status.clock_valid&&status.g1_retained_estimate&&!status.following&&!status.root_identity&&!status.service_until_us);
 assert(status.g1_clock_model.rate==4321&&status.follower_utc_us==uint64_t(trained.at(fake_now*20))/1000);
 if(which=="chain"){
  for(unsigned incarnation=0;incarnation<5;++incarnation){
   const auto old=v12::retained_estimate;v12::corrected_clock.clear();
   ++v12::anchor.boot;++v12::anchor.publication;v12::anchor.at=fake_now;v12::anchor.until=fake_now+1500000;
   uint64_t remote=0;uint16_t prior_event=0;uint8_t prior_channel=0;
   for(unsigned i=0;i<2;++i){
    r2_relay_packet packet{};const auto raw=fake_now*1000+9000000000;
    packet.origin={0,1,v12::anchor.boot,3,raw-1000000000,raw+650000000,0,Utc-9000000000,0,0};
    packet.route={0,0,0,0,v12::anchor.boot,v12::anchor.publication,i+1,10,v12::anchor.publication,UINT32_MAX};
    if(i){packet.count=1;packet.correction[0]={i,prior_event,prior_channel,remote*2/125,v12::anchor.publication};}
    v12::periodic_rx::Stamp stamp{};stamp.actual=stamp.association_observed=true;stamp.tick20=fake_now*20;stamp.available20=stamp.tick20+200;
    stamp.epoch=incarnation+1;stamp.sequence=i+1;stamp.event=uint16_t(i+1);stamp.channel=4;stamp.map_enclosure_ns=1000;
    assert(v12::corrected_clock.receive(packet,stamp,stamp.available20,fake_now,v12::anchor,nullptr,&v12::retained_estimate)==bool(i));
    remote=raw;prior_event=stamp.event;prior_channel=stamp.channel;fake_now+=62500;
   }
   assert(!v12::corrected_clock.frequency_trained()&&v12::corrected_clock.prediction().rate==4321);
   assert(v12::corrected_clock.finalize(fake_now*20));v12::publish(fake_now);
   assert(v12::retained_estimate.trained&&v12::retained_estimate.original20==old.original20&&v12::retained_estimate.clock.rate==4321);
   assert(v12::retained_estimate.clock.tick==old.clock.tick&&v12::retained_estimate.observed_us==old.observed_us);
   v12::changed_scope(fake_now,v12::G1Reason::PeriodicScope,false,v12::G1ParentReason::Manual);
   assert(!v12::corrected_clock.prediction().tick&&!v12::corrected_clock.frequency_trained());
   assert(v12::retained_estimate.trained&&v12::retained_estimate.clock.rate==4321);
   v12::anchor={true,222,66,UINT64_C(1700000000000000)+fake_now,fake_now,fake_now+1500000,3,4,1000};
  }
 }
 v12::changed_scope(fake_now,v12::G1Reason::PeriodicScope,false,v12::G1ParentReason::Manual);
 fixture_clock_fail=fixture_clock_reads+1;v12::publish(fake_now);assert(!v12::snap.clock_valid); // Counter failure cannot deliver a retained model.
 std::puts("PASS actual watch owner: retained rate/clock through61s admission, BLE stop/rearm or parent rebind, untrained successor chains and unavailable counter; no inherited authority");
}
