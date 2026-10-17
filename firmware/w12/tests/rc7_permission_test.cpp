#define V12_RC7_CLOCK_TEST_INSPECT 1
#include "g1_corrected_clock.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
namespace v12 {
struct Rc7PermissionInspect {
 static uint32_t issued_age(const G1CorrectedClock& c){return c.remote_epoch_;}
 static uint64_t consumed(const G1CorrectedClock& c){return c.last_original_;}
 static uint64_t mapped_hold(const G1CorrectedClock& c){return c.mapped_hold_until_us_;}
 static uint64_t mapped(const G1CorrectedClock& c){return c.mapped_original20_;}
 static void permit_fields(G1CorrectedClock& dest,const G1CorrectedClock& src){
   dest.until_=src.until_;dest.last_original_=src.last_original_;dest.last_remote_ns_=src.last_remote_ns_;
   dest.remote_epoch_=src.remote_epoch_;dest.local_epoch_=src.local_epoch_;
 }
};
}
namespace rc7_permission_test {
using namespace v12;
// Synthetic fixed geometry: the second original is6ms before the already
// delivered map, but strictly newer than the consumed original. Its immutable
// source envelope is newer and live. No field identity, capture or UTC tuple
// is embedded in this regression.
struct Sample {uint64_t now_us,original20,available20,issued,expires,frame,offset,parent_ns;
 uint32_t age_us,enclosure,epoch;uint16_t event;uint8_t channel;};
constexpr Sample Samples[]={
 {10068500,200000000,201370000,6560000000,8210000000,62500000,1700000000000000000,8000000000,300000,5000,5,100,4},
 {10190000,201250000,203800000,7650000000,9300000000,62500000,1700000000000000000,8062500000,100000,5000,5,101,9},
 {10252500,203750000,205050000,7650000000,9300000000,62500000,1700000000000000000,8187500000,200000,5000,5,103,17}
};
constexpr uint64_t Root=UINT64_C(0x1111111111111111),RootBoot=UINT64_C(0xaaaaaaaaaaaaaaaa);
constexpr uint64_t Parent=UINT64_C(0x2222222222222222),ParentBoot=UINT64_C(0xbbbbbbbbbbbbbbbb),Self=UINT64_C(0x020304050607);
using Clock=G1CorrectedClock;
r2_group_config group(){r2_group_config g{};for(unsigned i=0;i<32;i++)g.psk[i]=uint8_t(i+1);g.id=17;g.epoch=9;return g;}
G1Anchor anchor(){G1Anchor a{};a.source=true;a.root=Root;a.boot=RootBoot;a.at=1000000;
 a.source_epoch=14;a.publication=1;a.uncertainty=30000;a.context_until=361000000;a.drift_ppm=301;return a;}
rc7_path path(){rc7_path p{};p.depth=1;p.source_epoch=14;p.node[0]={Root,RootBoot,1,3,0,0};p.node[1]={Parent,ParentBoot,2,2,1,1};return p;}
Rc7Client bound(const r2_group_config& g){Rc7Client c;auto p=path();const auto& n=p.node[1];
 rc7_control cap{};cap.type=RC7_CAPABILITY;cap.path=p;cap.sender=n.id;cap.sender_boot=n.boot;cap.next_challenge=55;
 uint8_t w[212];rc7_control decoded{};assert(!rc7_control_encode(w,&cap,&g));assert(!rc7_control_decode(&decoded,w,212,&g));
 const auto bt=Samples[0].original20-2000000,now=bt/20;
 assert(c.make_request(decoded,Self,44,66,now-1000,false));
 auto response=c.request;response.type=RC7_RESPONSE;response.sender=Parent;response.target=Self;
 response.sender_boot=ParentBoot;response.target_boot=44;response.next_challenge=56;response.ready=true;
 response.publication_floor=10;response.root_lo_ns=Samples[0].parent_ns-105000000;
 response.root_hi_ns=Samples[0].parent_ns-95000000;response.source_expires_ns=response.root_hi_ns+1650000000;
 assert(!rc7_control_encode(w,&response,&g));assert(!rc7_control_decode(&decoded,w,212,&g));
 assert(c.bind(decoded,now,bt));return c;
}
periodic_rx::Stamp stamp(uint64_t tick,uint64_t available,uint32_t pub,uint16_t event,uint8_t channel,uint32_t epoch=5){periodic_rx::Stamp s{};
 s.actual=s.association_observed=true;s.tick20=tick;s.available20=available;s.epoch=epoch;s.sequence=pub;
 s.event=event;s.channel=channel;s.map_enclosure_ns=5000;return s;}
rc7_packet original_packet(const Sample& p,uint32_t pub,const r2_group_config& g){rc7_packet q{};q.origin={Root,RootBoot,14,p.issued,p.expires,int64_t(p.offset),p.frame,0,0};
 q.depth=1;q.slot=1;q.sender=Parent;q.boot=ParentBoot;q.train=2;q.epoch=2;q.publication=pub;
 assert(!rc7_origin_sign(q.proof,&q.origin,&g));return q;
}
// Drive actual portable admission, history, HMAC decode and the W12 saved
// capture bridge using deterministic synthetic observations. The actual flag
// exercises a trust boundary; no physical radio measurement is claimed.
void pair(Rc7Client& c,const Sample& p,uint32_t pub,const r2_group_config& g,uint32_t epoch=5){
 auto q=original_packet(p,pub,g);uint8_t wire[190];rc7_packet decoded{};
 auto original=stamp(p.original20,p.original20+20,pub,p.event,p.channel,epoch);original.map_enclosure_ns=p.enclosure;
 assert(!rc7_encode(wire,&q,&c.receiver.path,&g));assert(!rc7_decode(&decoded,wire,190,&c.receiver.path,&g));
 assert(!c.receive(decoded,original,original.available20));
 q.publication=pub+1;q.count=1;q.correction[0]={pub,p.event,p.channel,7,p.age_us,p.parent_ns,UINT32_MAX};
 assert(!rc7_encode(wire,&q,&c.receiver.path,&g));assert(!rc7_decode(&decoded,wire,190,&c.receiver.path,&g));
 auto follow=stamp(p.available20-20,p.available20,pub+1,uint16_t(p.event+1),uint8_t((p.channel+1)%37),epoch);
 assert(c.receive(decoded,follow,p.available20));assert(c.completed.stamp.tick20==p.original20);
}
struct Seed {r2_group_config g=group();G1Anchor a=anchor();Rc7Client c=bound(g);Clock clock{};
 Seed(){pair(c,Samples[0],11,g);const auto u=clock.receive_admitted_rc7(c,Samples[0].available20,Samples[0].now_us,a,g);
  assert(u.kind==Clock::Rc7Kind::Mapped);uint64_t utc=0;assert(clock.finalize(Samples[0].available20)&&clock.deliver(Samples[0].available20,Samples[0].now_us,utc));c.utc_known=true;}
};
Clock direct_clock_seed(){Clock clock;const auto& f=Samples[0];r2_relay_packet p{};
 p.origin.boot=RootBoot;p.origin.source_epoch=14;p.origin.created_ns=f.issued;p.origin.expires_ns=f.expires;
 p.origin.frame_ns=f.frame;p.origin.utc_offset_ns=f.offset;
 p.route.boot=RootBoot;p.route.train=p.route.map_generation=1;p.route.capture_epoch=3;p.route.publication=11;
 auto original=stamp(f.original20,f.original20+20,11,f.event,f.channel);
 assert(!clock.receive(p,original,original.available20,f.original20/20,anchor()));
 p.route.publication=12;p.count=1;p.correction[0]={11,f.event,f.channel,f.parent_ns*2/125,1};
 auto follow=stamp(f.available20-20,f.available20,12,uint16_t(f.event+1),uint8_t((f.channel+1)%37));
 assert(clock.receive(p,follow,f.available20,f.now_us,anchor()));
 assert(clock.finalize(f.available20)&&Rc7PermissionInspect::issued_age(clock)==3);
 return clock;
}
int run_model(){
 static_assert(sizeof(Clock)==1864,"permission correction must stay in the fixed clock RAM cap");
 Seed base;const auto original_hold=Rc7PermissionInspect::mapped_hold(base.clock),old_until=base.clock.until();
 const auto immutable_original=base.clock.original20();assert(Samples[1].now_us>old_until);
 pair(base.c,Samples[1],13,base.g);auto before=base.clock;
 assert(base.c.receiver.clock.reference.generation==7&&base.c.receiver.path.node[1].train==2);
 const auto u=base.clock.receive_admitted_rc7(base.c,Samples[1].available20,Samples[1].now_us,base.a,base.g);
 assert(u.kind==Clock::Rc7Kind::PermissionOnly&&u.permission_until_us>Samples[1].now_us);
 assert(u.mapped_hold_until_us==original_hold&&u.mapped_original20==immutable_original);
 assert(u.tuple_original20==Samples[1].original20&&u.tuple_age_us==227500);
 assert(base.clock.original20()==immutable_original&&Rc7PermissionInspect::consumed(base.clock)==Samples[1].original20);
 assert(Rc7PermissionInspect::issued_age(base.clock)==Samples[1].parent_ns-Samples[1].issued);
 // Exact complete object equality after copying only the six permitted scalar
 // fields: no fit, quality, prediction, pending, counter or map mutation.
 Rc7PermissionInspect::permit_fields(before,base.clock);assert(!std::memcmp(&before,&base.clock,sizeof before));
 assert(base.clock.accepted()==1&&base.clock.covers(Samples[1].now_us,u.permission_until_us-1));
 const auto good_clock=base.clock;auto duplicate=base.clock;
 assert(duplicate.receive_admitted_rc7(base.c,Samples[1].available20,Samples[1].now_us,base.a,base.g).kind==Clock::Rc7Kind::Refused);
 assert(duplicate.until()==good_clock.until());
 std::vector<std::string> cases={"mapped","permission-only-exact-model-unchanged","duplicate-reference","nonzero-map-generation-independent-of-train"};
 auto negative=[&](const char* name,auto change){auto test=Seed{};pair(test.c,Samples[1],13,test.g);uint64_t available=Samples[1].available20,now=Samples[1].now_us;
  change(test,available,now);const auto old=test.clock.prediction();const auto deadline=test.clock.until();
  assert(test.clock.receive_admitted_rc7(test.c,available,now,test.a,test.g).kind==Clock::Rc7Kind::Refused);
  const auto after=test.clock.prediction();assert(old.tick==after.tick&&old.epoch==after.epoch&&old.phase==after.phase&&old.rate==after.rate&&old.utc_offset==after.utc_offset);
  assert(test.clock.until()==deadline&&test.clock.original20()==immutable_original);cases.emplace_back(name);
 };
 negative("prior-map-not-delivered",[](auto& s,auto&,auto&){s.c.utc_known=false;});
 negative("lost-actual-capture",[](auto& s,auto&,auto&){s.c.completed.stamp.actual=false;});
 negative("lost-association",[](auto& s,auto&,auto&){s.c.completed.stamp.association_observed=false;});
 negative("bad-origin-proof",[](auto& s,auto&,auto&){s.c.receiver.clock.reference.proof[0]^=1;});
 negative("changed-source",[](auto& s,auto&,auto&){++s.c.receiver.path.source_epoch;});
 negative("invalid-full-path",[](auto& s,auto&,auto&){s.c.receiver.path.node[1].slot=0;});
 negative("changed-parent-boot",[](auto& s,auto&,auto&){++s.c.receiver.path.node[1].boot;});
 negative("changed-root-boot",[](auto& s,auto&,auto&){++s.c.receiver.path.node[0].boot;});
 negative("changed-clock-root",[](auto& s,auto&,auto&){++s.a.root;});
 negative("stale-replay-context",[](auto& s,auto& a,auto&){s.c.receiver.clock.context_until=rc7_tick16(a);});
 negative("expired-admission-lease",[](auto& s,auto&,auto& n){s.c.lease_until=n;});
 negative("expired-anchor-context",[](auto& s,auto&,auto& n){s.a.context_until=n;});
 negative("stale-original",[](auto& s,auto& a,auto&){a=s.c.completed.stamp.tick20+5000001;});
 negative("inherited-age",[](auto& s,auto&,auto&){s.c.receiver.clock.reference.age_us=1500000;});
 negative("wrong-rx-incarnation",[](auto& s,auto&,auto&){++s.c.completed.stamp.epoch;});
 negative("zero-map-generation",[](auto& s,auto&,auto&){s.c.receiver.clock.reference.generation=0;});
 negative("wrong-reference-event",[](auto& s,auto&,auto&){++s.c.receiver.clock.reference.event;});
 negative("wrong-reference-channel",[](auto& s,auto&,auto&){++s.c.receiver.clock.reference.channel;});
 negative("wrong-reference-original",[](auto& s,auto&,auto&){++s.c.receiver.clock.reference.rx;});
 negative("wrong-current-completion",[](auto&,auto& a,auto&){a+=5;});
 negative("older-issued-valid-proof",[](auto& s,auto&,auto&){auto& r=s.c.receiver.clock.reference;r.origin.issued=Samples[0].issued-1;r.origin.expires=r.origin.issued+1650000000;assert(!rc7_origin_sign(r.proof,&r.origin,&s.g));});
 negative("expired-source",[](auto& s,auto&,auto&){auto& r=s.c.receiver.clock.reference;r.origin.expires=r.parent_tx_ns;assert(!rc7_origin_sign(r.proof,&r.origin,&s.g));});
 negative("coarse-hi-expired",[](auto& s,auto& a,auto&){auto& x=s.c.receiver.clock;x.admission.now_tick=rc7_tick16(a);x.admission.root_hi_ns=x.reference.origin.expires;});
 negative("source-guard-exhausted",[](auto& s,auto&,auto&){auto& r=s.c.receiver.clock.reference;r.origin.expires=r.parent_tx_ns+150000000;assert(!rc7_origin_sign(r.proof,&r.origin,&s.g));});
 negative("expired-original-mapped-hold",[&](auto&,auto&,auto& n){n=original_hold;});
 // A normal half-duty sync recreation changes only the RX incarnation. Its
 // continuous20MHz, actual current completed tuple may renew permission.
 {Seed x;pair(x.c,Samples[1],13,x.g,6);const auto u=x.clock.receive_admitted_rc7(x.c,Samples[1].available20,Samples[1].now_us,x.a,x.g);
  assert(u.kind==Clock::Rc7Kind::PermissionOnly&&x.c.local_epoch==6&&x.clock.original20()==immutable_original);cases.emplace_back("50pct-sync-epoch-recreation");}
 {Seed x;pair(x.c,Samples[1],13,x.g);auto& r=x.c.receiver.clock.reference;r.origin.utc_offset=-1;assert(!rc7_origin_sign(r.proof,&r.origin,&x.g));
  assert(x.clock.receive_admitted_rc7(x.c,Samples[1].available20,Samples[1].now_us,x.a,x.g).kind==Clock::Rc7Kind::PermissionOnly);cases.emplace_back("allowed-negative-UTC-offset");}
 // Capability is only a locator. A response can bind a changed full path;
 // request target/boot/self still bind its admitted endpoint.
 {Seed x;pair(x.c,Samples[1],13,x.g);++x.c.request.path.node[1].train;
  assert(x.clock.receive_admitted_rc7(x.c,Samples[1].available20,Samples[1].now_us,x.a,x.g).kind==Clock::Rc7Kind::PermissionOnly);cases.emplace_back("capability-locator-path-is-not-admission-path");}
 {auto g=group();auto c=bound(g);Clock d;auto a=anchor();pair(c,Samples[0],11,g);
  assert(d.receive_admitted_rc7(c,Samples[0].available20,Samples[0].now_us,a,g).kind==Clock::Rc7Kind::Mapped);
  assert(!c.utc_known);pair(c,Samples[1],13,g);
  assert(d.receive_admitted_rc7(c,Samples[1].available20,Samples[1].now_us,a,g).kind==Clock::Rc7Kind::Refused);cases.emplace_back("prior-unfinalized-map-refused");}
 // Real later model update remains causal, creates its own immutable hold,
 // and can feed the relay. PermissionOnly never does either operation.
 pair(base.c,Samples[2],15,base.g);const auto next=base.clock.receive_admitted_rc7(base.c,Samples[2].available20,Samples[2].now_us,base.a,base.g);
 assert(next.kind==Clock::Rc7Kind::Mapped&&next.mapped_original20==Samples[2].original20&&base.clock.accepted()==2);
 assert(next.mapped_hold_until_us>original_hold);cases.emplace_back("later-causal-map");
 auto cleared=base.clock;cleared.withdraw();assert(!Rc7PermissionInspect::issued_age(cleared)&&!Rc7PermissionInspect::consumed(cleared)&&!Rc7PermissionInspect::mapped(cleared)&&!Rc7PermissionInspect::mapped_hold(cleared));cases.emplace_back("owner-withdraw-clears-shared-state");
 auto direct=direct_clock_seed();assert(Rc7PermissionInspect::issued_age(direct)==3&&!Rc7PermissionInspect::mapped(direct));direct.withdraw();assert(!Rc7PermissionInspect::issued_age(direct)&&!Rc7PermissionInspect::consumed(direct));cases.emplace_back("real-GR3-epoch-storage-clears");
 printf("{\"status\":\"PASSED\",\"clock_bytes\":%zu,\"old_permission_until_us\":%llu,\"permission_only_until_us\":%llu,\"immutable_mapped_hold_until_us\":%llu,\"cases\":[",sizeof(Clock),(unsigned long long)old_until,(unsigned long long)u.permission_until_us,(unsigned long long)original_hold);
 for(size_t i=0;i<cases.size();i++)printf("%s\"%s\"",i?",":"",cases[i].c_str());puts("],\"actual_RF\":false}");
 return 0;
}
} // namespace rc7_permission_test

#ifndef V12_RC7_PERMISSION_NATIVE
int main(){return rc7_permission_test::run_model();}
#else
// The focused runner includes the existing native_g1_test callback/controller
// fakes before this file. Only the device-binding adapter is synthetic; native
// owner/admission/periodic_poll bodies and all clock/portable helpers are real.
namespace rc7_permission_native {
using namespace rc7_permission_test;
void quiesce_for_jump(){
 v12::stop_scan();v12::cancel_sync(fake_now);pump_cleanup();
 fake_now+=G1Window::SyncCancelGrace;v12::r2_host_cleanup();v12::sync_selected=false;
 assert(!v12::peer.connecting&&v12::peer.handle==v12::None&&!v12::sync_pending&&!v12::synced);
}
void cleared(){assert(!Rc7PermissionInspect::consumed(v12::corrected_clock)&&
 !Rc7PermissionInspect::issued_age(v12::corrected_clock)&&!Rc7PermissionInspect::mapped(v12::corrected_clock)&&
 !Rc7PermissionInspect::mapped_hold(v12::corrected_clock));}
void forget_locators(uint64_t target,bool relay){
 v12::parent_selection.reset();v12::parent_selection.target(target,relay);
 for(auto& q:v12::recent)q={};v12::retry_at=0;
}
void admission_slot(){
 // Seeded physical models must pass the same warm start budget as runtime;
 // do not reset it to make an immediate synthetic handoff possible.
 v12::publish(fake_now);const auto& policy=v12::admission_policy;
 uint64_t ready=policy.retry_at;
 if(policy.warm){
  if(policy.starts[1])ready=std::max(ready,policy.starts[1]+20000000);
  if(policy.starts[0])ready=std::max(ready,policy.starts[0]+60000000);
 }
 if(ready>fake_now)fake_now=(ready/1000000+1)*1000000+1000;
}
void stop(){
 fixture_clock_fail=fixture_clock_backwards=0;
 v12::r2_stop();step(8);assert(v12::r2_status().off_ack&&
   v12::client.completed_us<v12::client.stop_deadline_us&&!v12::fault);
}
int run(const std::string& which){
 const auto device=v12::native_device_binding();assert(v12::r2_init(44,device->factory));
 assert(v12::provision.identity==Self&&v12::r2_host_init()&&v12::r2_host_sync());step();
 if(V12_R2_G1_LISTENING_PERCENT==50){fake_now=450000;v12::active_poll(fake_now);pump_cleanup();assert(!v12::owners_active());}
 fixture_cap={};fixture_cap.type=RC7_CAPABILITY;fixture_cap.path=path();
 fixture_cap.sender=Parent;fixture_cap.sender_boot=ParentBoot;fixture_cap.next_challenge=55;
 fake_now=1001000;
 if(which=="direct-to-rc7"){
   forget_locators(Root,false);fresh_direct(Root,RootBoot,14,1);assert(!v12::rc_bound());step(6);quiesce_for_jump();
   fake_now=Samples[0].now_us;v12::corrected_clock=direct_clock_seed();v12::anchor=rc7_permission_test::anchor();
   assert(Rc7PermissionInspect::issued_age(v12::corrected_clock)==3);
   v12::anchor.until=0;forget_locators(Parent,true);const auto generation=v12::client.generation;
   admission_slot();rc_discovery();assert(v12::rc_bound());cleared();assert(v12::client.generation==generation);
 }else{
   forget_locators(Parent,true);rc_discovery();assert(v12::rc_bound());quiesce_for_jump();
   Seed seed;fake_now=Samples[0].now_us;v12::corrected_clock=seed.clock;v12::anchor=seed.a;
   v12::rc_client.utc_known=true;v12::joined_peer=Parent;v12::joined_boot=ParentBoot;
   const auto generation=v12::client.generation;
   if(which=="retained-new-coarse-root"){
     v12::anchor.until=v12::corrected_clock.until();v12::publish(fake_now);
     const auto retained=v12::retained_estimate;assert(retained.root==Root);
     constexpr uint64_t NewRoot=0x55555555,NewBoot=0x66666666;
     forget_locators(NewRoot,false);v12::anchor.until=0;admission_slot();fresh_direct(NewRoot,NewBoot,18,9);
     auto current=v12::r2_status();assert(current.following&&current.root_identity==NewRoot&&current.root_boot==NewBoot);
     assert(current.g1_clock_origin==NewRoot&&!current.g1_clock_model.tick&&!current.g1_retained_estimate&&!v12::r2_clock_witness(current));
     assert(current.g1_retained_clock.root==retained.root&&current.g1_retained_clock.boot==retained.boot&&current.g1_retained_clock.clock.tick==retained.clock.tick);
     fake_now=v12::anchor.until+1;v12::active_poll(fake_now);v12::publish(fake_now);current=v12::r2_status();
     assert(current.clock_valid&&!current.following&&current.g1_clock_origin==NewRoot&&!current.g1_clock_model.tick&&!current.g1_retained_estimate);
     fake_now=v12::anchor.deadline()+1;v12::active_poll(fake_now);pump_cleanup();v12::publish(fake_now);current=v12::r2_status();
     assert(current.clock_valid&&!current.following&&current.g1_retained_estimate&&current.g1_clock_origin==Root&&!current.root_identity&&!v12::r2_clock_witness(current));
     assert(current.g1_clock_model.tick==retained.clock.tick&&current.g1_clock_model.epoch==retained.clock.epoch);
   }else if(which=="rc7-foreign-root-hint"){
     v12::anchor.until=v12::corrected_clock.until();v12::member=true;
     v12::synced=true;v12::sync_handle=9;v12::sync_identity=Parent;
     v12::sync_requested_at=v12::sync_owner_window_start=fake_now;
     fixture_sync_arg=reinterpret_cast<void*>(uintptr_t(v12::sync_capture_epoch));
     const auto old_anchor=v12::anchor;const auto old_client=v12::rc_client;const auto epoch=v12::sync_capture_epoch;
     v12::peer={};v12::peer.identity=0x77777777;v12::peer.handle=22;v12::peer.incarnation=++v12::incarnation;conn_arg=v12::callback_arg();
     assert(!r2_group_join_begin(&v12::peer.exchange,false,Self,v12::peer.identity,44,77,fake_now));
     auto& x=v12::peer.exchange;x.peer_boot=0x88888888;x.remote_nonce=88;x.proof_at=fake_now-1000;x.challenged=true;
     r2_join_record hint{};hint.kind=R2_JOIN_ACCEPT;hint.sender=v12::peer.identity;hint.receiver=Self;hint.sender_boot=x.peer_boot;hint.receiver_boot=44;
     hint.client_nonce=77;hint.server_nonce=88;hint.sequence=2;hint.created_us=fake_now;hint.echo_us=x.proof_at;std::strcpy(hint.name,"Locator");
     hint.source_flags=1;hint.source_bound_us=40000;hint.root=0x99999999;hint.root_boot=0xaaaaaaaa;hint.source_epoch=20;hint.publication=10;hint.utc_us=1700000000000000;hint.remaining_us=1000000;
     v12::Incoming row{};row.at=fake_now;row.incarnation=v12::peer.incarnation;assert(!r2_group_join_encode(row.bytes,&hint,&v12::provision.group));
     assert(v12::incoming.push(row));v12::receive_join(fake_now);
     assert(v12::peer.selection_accepted&&v12::join_accepts>=1&&v12::admission_policy.foreign_root==1);
     assert(v12::member&&v12::joined_peer==Parent&&v12::joined_boot==ParentBoot&&v12::rc_bound());
     assert(!std::memcmp(&old_client,&v12::rc_client,sizeof old_client)&&v12::anchor.same(old_anchor)&&v12::anchor.at==old_anchor.at&&v12::anchor.until==old_anchor.until);
     assert(v12::synced&&v12::sync_identity==Parent&&v12::sync_capture_epoch==epoch&&!v12::sync_terminating&&!v12::sync_cleanup_queued);
   }else if(which=="retained-expiry"||which=="retained-clock-step"){
     v12::anchor.until=v12::corrected_clock.until();v12::publish(fake_now);
     const auto original=v12::retained_estimate.clock;const auto retained=v12::r2_status();
     assert(retained.clock_valid&&!retained.g1_retained_estimate&&v12::admission_policy.warm);
     if(which=="retained-clock-step"){
       v12::changed_scope(fake_now,v12::G1Reason::PeriodicScope,false,v12::G1ParentReason::ClockStep);
       v12::publish(fake_now);assert(!v12::r2_status().clock_valid&&!v12::retained_estimate.root);
     }else{
       fake_now=v12::anchor.deadline()+1000;v12::active_poll(fake_now);v12::publish(fake_now);
       const auto held=v12::r2_status();assert(held.clock_valid&&!held.following&&held.g1_retained_estimate);
       assert(!held.root_identity&&!held.root_boot&&!held.source_epoch&&!held.publication&&!held.service_until_us);
       assert(!held.g1_relative_quality.samples&&!held.precision_fit&&held.precision_ns==UINT32_MAX&&!v12::r2_clock_witness(held));
       assert(v12::receive_loss.frozen&&v12::receive_loss.frozen_at==fake_now);
       assert(held.follower_utc_us==uint64_t(original.at(fake_now*20))/1000);
       assert(v12::retained_estimate.clock.tick==original.tick&&v12::retained_estimate.clock.rate==original.rate);
     }
   }else if(which=="autonomous-direct"){
     forget_locators(Root,false);v12::rc_client.renew_at=0;v12::anchor.until=0;
     admission_slot();fresh_direct(Root,RootBoot,14,1);assert(!v12::rc_bound()&&v12::member&&v12::joined_peer==Root);cleared();
   }else if(which=="typed-direct"){
     assert(v12::r2_gm_parent(Root));step(6);assert(!v12::rc_bound());cleared();
     admission_slot();fresh_direct(Root,RootBoot,14,1);assert(!v12::rc_bound()&&v12::member);
   }else if(which=="changed-full-path"||which=="response-path-changed"){
     if(which=="response-path-changed")fixture_response_path_change=true;else ++fixture_cap.path.node[1].train;
     fixture_cap.next_challenge+=100;v12::rc_client.renew_at=0;forget_locators(Parent,true);
     admission_slot();rc_discovery();assert(v12::rc_bound()&&v12::rc_client.receiver.path.node[1].train==3);cleared();
     if(which=="response-path-changed")assert(fixture_request.path.node[1].train==2&&fixture_response.path.node[1].train==3);
   }else{
     assert(which=="permission-native"||which=="loss-recovery-preserved"||which=="mapped-read-failure"||which=="mapped-finalize-failure"||which=="mapped-deliver-failure");
     v12::corrected_clock.clear();v12::rc_client=bound(v12::provision.group);v12::anchor=rc7_permission_test::anchor();
     pair(v12::rc_client,Samples[0],11,v12::provision.group);
     assert(v12::corrected_clock.receive_admitted_rc7(v12::rc_client,Samples[0].available20,Samples[0].now_us,v12::anchor,v12::provision.group).kind==Clock::Rc7Kind::Mapped);
     uint64_t utc=0;assert(v12::corrected_clock.finalize(Samples[0].available20)&&v12::corrected_clock.deliver(Samples[0].available20,Samples[0].now_us,utc));v12::rc_client.utc_known=true;
     if(which=="loss-recovery-preserved"){
       v12::anchor.until=v12::corrected_clock.until();v12::publish(fake_now);
       v12::receive_loss.observe(fake_now,G1ReceiveLoss::Report|G1ReceiveLoss::Mapped,5);
       fake_now=v12::corrected_clock.until()+1;v12::active_poll(fake_now);
       assert(v12::receive_loss.frozen&&v12::receive_loss.expiries==1);
       quiesce_for_jump(); // Drain the synthetic PA owner before replacing its fixture epoch.
     }
     const auto first_loss=v12::receive_loss;
     const auto& sample=which=="permission-native"?Samples[1]:Samples[2];
     auto q=original_packet(sample,13,v12::provision.group);
     if(which=="mapped-deliver-failure"){q.origin.utc_offset=-1;assert(!rc7_origin_sign(q.proof,&q.origin,&v12::provision.group));}
     const auto captured=stamp(sample.original20,sample.original20+20,13,sample.event,sample.channel);
     assert(!v12::rc_client.receive(q,captured,captured.available20));
     q.publication=14;q.count=1;q.correction[0]={13,sample.event,sample.channel,7,sample.age_us,sample.parent_ns,UINT32_MAX};
     fake_now=sample.now_us;v12::Periodic row{};row.identity=v12::sync_identity=Parent;row.at=fake_now;row.length=190;
     row.capture=stamp(fake_now*20-20,fake_now*20,14,uint16_t(sample.event+1),3);v12::sync_capture_epoch=5;
     assert(!rc7_encode(row.bytes,&q,&v12::rc_client.receiver.path,&v12::provision.group));
     auto before=v12::corrected_clock;const auto old_utc=v12::anchor.utc;v12::window_corrected=false;
     fixture_clock_reads=0;fixture_clock_fail=which=="mapped-read-failure"?2:0;fixture_clock_backwards=which=="mapped-finalize-failure"?2:0;
     assert(v12::periodic.push(row));v12::periodic_poll(fake_now);
     if(which=="permission-native"){
       assert(v12::anchor.until>fake_now&&v12::anchor.until==v12::corrected_clock.until());
       assert(v12::anchor.utc==old_utc&&!v12::window_corrected&&v12::rc_client.progress_at==fake_now);
       before.scope(v12::anchor);Rc7PermissionInspect::permit_fields(before,v12::corrected_clock);
       assert(!std::memcmp(&before,&v12::corrected_clock,sizeof before));
     }else if(which=="loss-recovery-preserved"){
       assert(v12::rc_client.utc_known&&v12::window_corrected&&v12::corrected_clock.covers(fake_now,fake_now));
       v12::publish(fake_now);
       assert(v12::receive_loss.frozen&&v12::receive_loss.frozen_at==first_loss.frozen_at&&v12::receive_loss.reacquisitions==1);
       assert(v12::receive_loss.sequence==first_loss.sequence&&!std::memcmp(v12::receive_loss.entries,first_loss.entries,sizeof first_loss.entries));
       fake_now=v12::corrected_clock.until()+1;v12::active_poll(fake_now);
       assert(v12::receive_loss.expiries==2&&v12::receive_loss.frozen_at==first_loss.frozen_at);
       assert(!std::memcmp(v12::receive_loss.entries,first_loss.entries,sizeof first_loss.entries));
     }else assert(!v12::rc_client.utc_known&&!v12::anchor.until&&!v12::corrected_clock.prediction().tick&&!v12::window_corrected);
   }
   assert(v12::client.generation==generation);
 }
 stop();printf("{\"status\":\"PASSED\",\"case\":\"%s\",\"listening_percent\":%u,\"original_stop_deadline\":true,\"actual_RF\":false}\n",which.c_str(),V12_R2_G1_LISTENING_PERCENT);
 return 0;
}
} // namespace rc7_permission_native
int main(int argc,char** argv){assert(argc==2);return rc7_permission_native::run(argv[1]);}
#endif
