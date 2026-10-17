// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Synthetic current-parent model, physical-delivery reports and SDK yields.
// These exercise production scheduling; no fixture grants real RF authority.
namespace v12 {
struct Rc7PermissionInspect {
 static void seed(G1CorrectedClock& c,const G1Anchor& a,uint64_t now){
  c.clear();c.scope_=a;c.until_=a.until;c.clock_={now*20,int64_t(now*1000),0,0,INT64_C(1700000000000000000)};
  c.phase_fit_tick_=now*20;c.frequency_trained_=true;c.rate_guard_=100;c.last_original_=now*20;
  c.quality_second_=now/1000000;auto& b=c.quality_[c.quality_second_%16];b.second=uint16_t(c.quality_second_);
  b.count=16;b.squares=160000;b.peak=1500; // Deliberately fails the unchanged1us lock gate.
 }
 static void expire(G1CorrectedClock& c,uint64_t now){c.until_=now;}
};
}
bool priority_inject=false,priority_delay=false,priority_empty_before_write=false;
void priority_packet(){
 v12::Periodic row{};row.at=fake_now;row.identity=v12::joined_peer;row.length=158;
 r2_precision_packet p{};p.boot=v12::joined_boot;p.created_ns=fake_now*1000;p.expires_ns=p.created_ns+1500000000;
 p.utc_offset_ns=INT64_C(1700000000000000000);p.source_epoch=2;p.train=p.publication=p.capture_epoch=3;p.flags=1;
 const uint64_t ids[4]={row.identity,0,0,0};assert(!r2_group_precision_encode(row.bytes,&p,&v12::provision.group,ids));
 assert(v12::periodic.push(row));
}
void priority_marker_returned(){if(priority_inject){priority_inject=false;priority_packet();}}
void priority_join_written(){priority_empty_before_write=v12::periodic.count==0;if(priority_delay)fake_now+=60000;}
int main(int argc,char** argv){
 assert(argc==2);const std::string which=argv[1];const uint8_t mac[]={2,3,4,5,6,7};
 assert(v12::r2_init(44,mac)&&v12::r2_host_init()&&v12::r2_host_sync());step(8);v12::stop_scan();
 fake_now=100000000;v12::window_start=v12::G1Window::start(fake_now);
 v12::anchor={true,501,601,UINT64_C(1700000000000000)+fake_now,fake_now,fake_now+1500000,2,3,160000};
 v12::joined_peer=501;v12::joined_boot=601;v12::member=true;
 v12::parent_selection.bind(501,601,2,fake_now);v12::parent_selection.tick(fake_now,true,false);v12::parent_selection.delivered(3,10,fake_now);
 v12::sync_identity=501;v12::sync_address=v12::canonical(501);v12::synced=true;v12::sync_handle=9;
 v12::Rc7PermissionInspect::seed(v12::corrected_clock,v12::anchor,fake_now);v12::admission_policy.acquired();
 assert(!v12::lock_protected(fake_now)&&v12::following(fake_now));
 const auto until=v12::anchor.until,deadline=v12::anchor.deadline();
 if(which=="quality-dip"){
  v12::start_scan();assert(!scan_active);
  scan_active=v12::scan=true;v12::active_poll(fake_now);
  assert(!scan_active&&connected==1&&v12::peer.identity==501);
  assert(v12::anchor.until==until&&v12::anchor.deadline()==deadline&&!v12::lock_protected(fake_now));
 }else if(which=="warm-window"||which=="cold-window"){
  if(which=="cold-window")v12::synced=false;
  v12::Discovery d{};d.address=v12::sync_address;v12::connect(d,501,fake_now);
  assert(connected==1&&last_init_interval==32&&last_init_window==(which=="warm-window"?8:32));
  assert(v12::peer.attempt_until==fake_now+2000000&&v12::peer.exchange.until==fake_now+R2_GROUP_JOIN_LIFETIME_US);
 }else if(which=="retry-bound"||which=="retry-unreserved"||which=="retry-relay"){
  v12::anchor.uncertainty=190000;v12::peer={};v12::peer.identity=v12::joined_peer;
  v12::admission_policy.acquired();
  if(which=="retry-unreserved")v12::sync_identity=999;
  if(which=="retry-relay")v12::peer.relay=true;
  const auto original=v12::anchor.deadline();
  const auto random=UINT64_C(0x91919191);
  const auto jitter=which=="retry-bound"?original-fake_now-23000000:20000000;
  v12::failed_admission(fake_now);
  assert(v12::admission_policy.retry_at==fake_now+20000000+random%(jitter+1));
  assert(v12::anchor.deadline()==original&&v12::anchor.until==until&&!v12::fault);
 }else if(which=="control-drain"||which=="batch-budget"){
  v12::peer={};v12::peer.identity=501;v12::peer.handle=22;v12::peer.incarnation=++v12::incarnation;
  v12::peer.stage=2;v12::peer.gatt_until=fake_now+2000000;
  assert(!r2_group_join_begin(&v12::peer.exchange,false,v12::provision.identity,501,44,77,fake_now));
  const auto received=v12::p1_ok;
  if(which=="batch-budget")for(unsigned i=0;i<8;i++)priority_packet();
  priority_inject=true;priority_delay=which=="control-drain";v12::active_poll(fake_now);
  if(which=="control-drain")assert(priority_empty_before_write&&v12::p1_ok==received+1);
  else{
   assert(!priority_empty_before_write&&v12::p1_ok==received+8&&v12::periodic.count==1);
   v12::active_poll(fake_now);assert(v12::p1_ok==received+9&&v12::periodic.count==0);
  }
  assert(!v12::fault);
 }else{
  if(which=="source-expiry")v12::anchor.until=fake_now;
  else if(which=="model-expiry")v12::Rc7PermissionInspect::expire(v12::corrected_clock,fake_now);
  else if(which=="capture-gap")fake_now+=250000;
  else if(which=="wrong-parent")v12::sync_identity=999;
  else if(which=="clock-failure")priority_clock_ok=false;
  else assert(false);
  v12::start_scan();assert(scan_active&&!v12::fault);
 }
 std::puts("PASS production precision scheduling, unchanged quality/expiry and bounded ownership");
}
