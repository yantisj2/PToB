// Included after the existing generated synthetic controller helpers. All
// assertions below exercise current native_g1.cpp; no hardware or RF claims.
namespace v12 {
// Existing friend seam seeds a wholly synthetic prior delivered model. It
// exercises the real lock gates; it does not claim RF training or accuracy.
struct Rc7PermissionInspect {
 static void lock(G1CorrectedClock& c,const G1Anchor& a,uint64_t now){
  c.clear();c.scope_=a;c.until_=a.until;c.clock_.tick=now*20;c.clock_.epoch=int64_t(now*1000);
  c.clock_.utc_offset=INT64_C(1700000000000000000);c.phase_fit_tick_=now*20;c.frequency_trained_=true;c.rate_guard_=100;
  c.mapped_original20_=c.last_original_=now*20;c.mapped_hold_until_us_=a.until;
  c.quality_second_=now/1000000;auto& b=c.quality_[c.quality_second_%16];b.second=uint16_t(c.quality_second_);b.count=16;b.squares=160000;b.peak=150;
 }
 static void expire(G1CorrectedClock& c,uint64_t until){c.until_=until;}
};
}
namespace {
unsigned owner_checks=0;
#define OWNER_CHECK(x) do{assert(x);++owner_checks;}while(false)
const v12::G1ParentHistory::Row* owner_row(uint64_t id,bool relay){return v12::parent_selection.history().find(id,relay,fake_now);}
void owner_init(){
 const auto* mac=v12::native_device_binding()->factory;
 OWNER_CHECK(v12::r2_init(44,mac));OWNER_CHECK(v12::r2_host_init());OWNER_CHECK(v12::r2_host_sync());step();
 OWNER_CHECK(!rc7_capability_decode(&fixture_cap,golden0_pre_request_capability_projected_hex,144,&v12::provision.group));
 if(V12_R2_G1_LISTENING_PERCENT==50){fake_now=450000;v12::active_poll(fake_now);pump_cleanup();OWNER_CHECK(!v12::owners_active());}
 fake_now=1001000;
}
void owner_advert(bool relay,uint64_t id){
 v12::Discovery d{};d.props=1;d.rssi=-30;d.at=fake_now;d.address=v12::canonical(id);
 if(relay){d.sid=13;d.length=167;d.bytes[0]=2;d.bytes[1]=1;d.bytes[2]=6;d.bytes[3]=17;d.bytes[4]=7;
  std::memcpy(d.bytes+5,v12::rc_service.value,16);d.bytes[21]=145;d.bytes[22]=255;
  OWNER_CHECK(!rc7_capability_encode(d.bytes+23,&fixture_cap,&v12::provision.group));
 }else{d.length=25;d.bytes[0]=24;d.bytes[1]=0xff;auto* b=d.bytes+2;b[0]='G';b[1]='1';b[2]=1;
  r2_group_put(b+3,v12::provision.group.id,8);r2_group_put(b+11,v12::provision.group.epoch,4);r2_group_put(b+15,id,8);}
 ble_gap_event e{};e.type=BLE_GAP_EVENT_EXT_DISC;e.ext_disc.addr=d.address;e.ext_disc.rssi=d.rssi;e.ext_disc.sid=d.sid;e.ext_disc.props=1;
 e.ext_disc.data_status=BLE_HCI_ADV_DATA_STATUS_COMPLETE;e.ext_disc.length_data=d.length;e.ext_disc.data=d.bytes;
 v12::gap(&e,nullptr);
}
void owner_begin(bool relay=true){
 const auto attempts=connected;owner_advert(relay,fixture_cap.sender);step(21);OWNER_CHECK(connected==attempts+1);OWNER_CHECK(v12::peer.connecting&&v12::peer.relay==relay);
}
void owner_ready(){owner_begin();ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;v12::gap(&e,conn_arg);step(20);
 OWNER_CHECK(v12::rc_bound()&&v12::joined_peer==fixture_cap.sender);OWNER_CHECK(v12::parent_selection.parent()==fixture_cap.sender);
 OWNER_CHECK(!v12::fault&&v12::api_errors==0);}
void owner_stop(){v12::r2_stop();step(8);OWNER_CHECK(v12::r2_status().off_ack&&!v12::fault);}
void owner_failure(bool reset_locator){
 const auto id=fixture_cap.sender;owner_begin(reset_locator);ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.status=1;
 const auto arg=conn_arg;v12::gap(&e,arg);OWNER_CHECK(v12::parent_failure.identity==id&&v12::parent_failure.relay==reset_locator);
 OWNER_CHECK(!owner_row(id,reset_locator)); // Callback queues only; it never mutates history.
 v12::gap(&e,arg);OWNER_CHECK(v12::parent_failure.identity==id); // Duplicate cannot produce a second penalty.
 if(reset_locator)v12::parent_selection.reset();step();
 const auto* r=owner_row(id,reset_locator);OWNER_CHECK(r&&r->failures==1&&!r->delivery_samples&&!r->usable_samples);
 OWNER_CHECK(!owner_row(id,!reset_locator));OWNER_CHECK(!v12::parent_failure.identity);
 step();OWNER_CHECK(owner_row(id,reset_locator)->failures==1);OWNER_CHECK(!v12::fault&&v12::api_errors==0);
}
void owner_reacquire_window(){
 if(V12_R2_G1_LISTENING_PERCENT==50){fake_now=1450000+v12::window_phase;v12::active_poll(fake_now);pump_cleanup();step(6);OWNER_CHECK(!v12::owners_active());}
 fake_now=7001000+v12::window_phase;v12::active_poll(fake_now);pump_cleanup();
}
void owner_renew(){
 // A new actual signed request/response through the native worker handler,
 // preserving the existing authenticated complete path.
 const auto now=fake_now;OWNER_CHECK(v12::rc_client.make_request(fixture_cap,v12::provision.identity,v12::boot_id,++v12::nonce,now,false));
 const auto& q=v12::rc_client.request;rc7_control r{};r.type=RC7_RESPONSE;r.sender=q.target;r.target=q.sender;r.sender_boot=q.target_boot;r.target_boot=q.sender_boot;
 r.challenge=q.challenge;r.nonce=q.nonce;r.sequence=q.sequence;r.lease_us=q.lease_us;r.next_challenge=q.challenge+1;r.path=fixture_cap.path;r.ready=true;r.publication_floor=10;
 r.root_lo_ns=now*1000-1000000;r.root_hi_ns=now*1000+1000000;r.source_expires_ns=now*1000+1500000000;
 v12::Incoming row{};row.length=212;row.at=now;OWNER_CHECK(!rc7_control_encode(row.bytes,&r,&v12::provision.group));
 v12::peer.stage=6;v12::receive_rc_control(row,now);OWNER_CHECK(v12::rc_bound());
}
void owner_lock(bool relay=true){
 if(relay)owner_ready();else fresh_direct(501,601,2,3);
 step(3);
 if(!relay){v12::Discovery current{};current.sid=12;current.interval=50;current.address=v12::canonical(v12::joined_peer);current.at=fake_now;
  OWNER_CHECK(v12::discoveries.push(current));step(2);}
 OWNER_CHECK(v12::sync_pending);sync_established();OWNER_CHECK(v12::synced);
 v12::anchor.utc=UINT64_C(1700000000000000);v12::anchor.at=fake_now;v12::anchor.until=fake_now+1500000;
 v12::anchor.context_until=fake_now+30000000;v12::anchor.uncertainty=100;v12::anchor.drift_ppm=301;
 if(relay)v12::rc_client.utc_known=true;
 v12::Rc7PermissionInspect::lock(v12::corrected_clock,v12::anchor,fake_now);
 v12::sync_identity=v12::joined_peer;
 auto& q=const_cast<r2_link_quality_view&>(v12::parent_selection.quality());q.last_rx=fake_now;
 // Explicit synthetic precondition: a prior current-parent cooldown expired.
 for(auto& r:v12::recent)if(r.identity==v12::joined_peer)r.until=fake_now;
 v12::retry_at=fake_now;
 OWNER_CHECK(v12::following(fake_now)&&v12::corrected_clock.covers(fake_now,fake_now));OWNER_CHECK(v12::lock_protected(fake_now));
 v12::stop_scan(); // Set up the ordinary scanner OFF; the queued-scan case below also tests stopping an existing owner.
}
}
int main(int argc,char** argv){
 assert(argc==2);const std::string which=argv[1];owner_init();
 if(which.rfind("startup-",0)==0){
  const auto initial=connected;const uint64_t gm=fixture_cap.path.node[0].id;
  OWNER_CHECK(gm!=fixture_cap.sender);
  for(unsigned i=0;i<150;i++){
   fake_now=1001000+uint64_t(i)*20000;owner_advert(true,fixture_cap.sender);step();
   OWNER_CHECK(connected==initial&&!v12::peer.connecting&&!v12::parent_selection.parent());
   OWNER_CHECK(!v12::fault&&v12::quiet_faults==0);
  }
  fake_now=4001000;owner_advert(true,fixture_cap.sender);
  if(which=="startup-gm")owner_advert(false,gm);
  step(2);OWNER_CHECK(connected==initial+1&&v12::peer.connecting);
  OWNER_CHECK(v12::peer.relay==(which=="startup-relay"));
  OWNER_CHECK(v12::peer.identity==(which=="startup-gm"?gm:fixture_cap.sender));
  OWNER_CHECK(!v12::anchor.source&&!v12::following(fake_now));owner_stop();
  std::printf("PASS %s %u native startup assertions %d%%\n",argv[1],owner_checks,V12_R2_G1_LISTENING_PERCENT);return 0;
 }
 // Remaining cases begin after a synthetic prior admission; their original
 // short discovery fixtures test established operation, not cold acquisition.
 v12::parent_selection.bind(99,199,1,fake_now);v12::parent_selection.reset();
 if(which=="connect-failure"){owner_failure(false);owner_stop();}
 else if(which=="failure-role-reset"){owner_failure(true);owner_stop();}
 else if(which=="ready-no-time"){
  owner_ready();const auto* r=owner_row(fixture_cap.sender,true);OWNER_CHECK(r&&r->context&&r->delivery_samples==0&&r->usable_samples==0&&r->failures==0);
  OWNER_CHECK(v12::parent_selection.quality().received==0&&!v12::rc_client.utc_known&&!v12::following(fake_now));
  step(3);OWNER_CHECK(v12::peer.handle==v12::None&&!v12::parent_failure.identity);OWNER_CHECK(owner_row(fixture_cap.sender,true)->failures==0);owner_stop();
 }
 else if(which=="quiet-neutral"){
  OWNER_CHECK(V12_R2_G1_LISTENING_PERCENT==50);owner_begin();fake_now=1401000;v12::active_poll(fake_now);pump_cleanup();step(4);
  fake_now=1451000;v12::active_poll(fake_now);pump_cleanup();step(4);OWNER_CHECK(!v12::owners_active());
  OWNER_CHECK(!v12::parent_failure.identity&&v12::parent_selection.history().entries(fake_now)==0);OWNER_CHECK(v12::quiet_faults==0&&!v12::fault);owner_stop();
 }
 else if(which=="stop-neutral"){
  owner_begin();owner_stop();OWNER_CHECK(!v12::parent_failure.identity&&v12::parent_selection.history().entries(fake_now)==0);
 }
 else if(which=="rebind-neutral"){
  owner_begin();OWNER_CHECK(v12::r2_set_relay_only(true));OWNER_CHECK(v12::r2_activity().mode_pending);step(8);
  OWNER_CHECK(v12::exclude_direct_gm&&!v12::parent_diagnostic.pending()&&!v12::fault);OWNER_CHECK(!v12::parent_failure.identity&&v12::parent_selection.history().entries(fake_now)==0);owner_stop();
 }
 else if(which=="scope-clear"){
  owner_ready();const auto id=fixture_cap.sender;const auto context=owner_row(id,true)->context;v12::changed_scope(fake_now);
  OWNER_CHECK(v12::parent_selection.parent()==0&&!v12::rc_bound()&&!v12::following(fake_now));OWNER_CHECK(owner_row(id,true)&&owner_row(id,true)->context==context);
  v12::parent_selection.seen(123,false,-20,fake_now);OWNER_CHECK(v12::parent_selection.choose(fake_now+40000)==123);owner_stop();
 }
 else if(which=="same-renewal"){
  owner_ready();const auto id=fixture_cap.sender;const auto context=owner_row(id,true)->context;
  const auto before=v12::parent_selection.quality();const auto accepted=v12::corrected_clock.accepted();
  fake_now+=10000;owner_renew();OWNER_CHECK(owner_row(id,true)->context==context);OWNER_CHECK(owner_row(id,true)->delivery_samples==0&&owner_row(id,true)->usable_samples==0);
  OWNER_CHECK(v12::parent_selection.quality().received==before.received&&v12::corrected_clock.accepted()==accepted);
  v12::parent_selection.seen(id+1,false,-20,fake_now);OWNER_CHECK(v12::parent_selection.choose(fake_now+40000,true)==id); // Fresh incumbent still in collection.
  OWNER_CHECK(v12::parent_selection.selection_view(fake_now).reason==v12::G1ParentSelection::Decision::Dwell);
  fake_now+=200000;v12::parent_selection.seen(id+1,false,-20,fake_now);OWNER_CHECK(v12::parent_selection.choose(fake_now,true)==0); // No stale incumbent admission.
  OWNER_CHECK(v12::parent_selection.selection_view(fake_now).reason==v12::G1ParentSelection::Decision::Dwell);owner_stop();
 }
 else if(which=="profile-clear"){
  owner_ready();OWNER_CHECK(v12::parent_selection.history().entries(fake_now)==1);owner_stop();
  const uint8_t other=V12_R2_G1_LISTENING_PERCENT==50?100:50;OWNER_CHECK(v12::r2_client_start_profile(909,other));step(2);
  OWNER_CHECK(v12::listening_percent==other&&v12::parent_selection.history().entries(fake_now)==0);owner_stop();
 }
 else if(which=="new-parent-request"||which=="current-parent-rescan"){
  owner_ready();step(3);OWNER_CHECK(v12::sync_pending);sync_established();OWNER_CHECK(v12::synced);
  auto& quality=const_cast<r2_link_quality_view&>(v12::parent_selection.quality());quality.poor=true;owner_reacquire_window();
  const auto old_id=v12::joined_peer;const bool other=which=="new-parent-request";
  if(other){fixture_cap.sender+=100;fixture_cap.path.node[fixture_cap.path.depth].id=fixture_cap.sender;}
  v12::Discovery d{};d.props=1;d.sid=13;d.rssi=-30;d.at=fake_now;d.address=v12::canonical(fixture_cap.sender);
  v12::connect(d,fixture_cap.sender,fake_now,true);OWNER_CHECK(v12::peer.connecting&&v12::peer.rescan==!other);
  ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;v12::gap(&e,conn_arg);step(18);OWNER_CHECK(fixture_request.operation==(other?0:1));
  OWNER_CHECK(!v12::fault&&v12::api_errors==0);if(other)OWNER_CHECK(v12::joined_peer!=old_id);owner_stop();
 }
 else if(which=="lock-no-alternatives"){
  owner_lock();auto& q=const_cast<r2_link_quality_view&>(v12::parent_selection.quality());q.poor=true;q.last_rx=fake_now-250000;
  v12::rc_client.renew_at=fake_now;
  v12::Discovery alt{};alt.rssi=-20;alt.props=1;alt.address=v12::canonical(333);alt.at=fake_now;alt.length=25;alt.bytes[0]=24;alt.bytes[1]=0xff;
  auto* b=alt.bytes+2;b[0]='G';b[1]='1';b[2]=1;r2_group_put(b+3,v12::provision.group.id,8);r2_group_put(b+11,v12::provision.group.epoch,4);r2_group_put(b+15,333,8);
  OWNER_CHECK(v12::discoveries.push(alt));v12::parent_selection.seen(333,false,-20,fake_now); // Both queued and previously cached alternatives.
  const auto scans=fixture_parent_scan_calls,attempts=connected,discovered=v12::discovery_count;
  v12::start_scan();OWNER_CHECK(fixture_parent_scan_calls==scans&&!scan_active);
  scan_active=v12::scan=true;v12::active_poll(fake_now);OWNER_CHECK(!scan_active&&!v12::scan);
  OWNER_CHECK(connected==attempts&&v12::discovery_count==discovered);OWNER_CHECK(!v12::discoveries.pop(alt));
  v12::publish(fake_now);OWNER_CHECK(v12::r2_status().g1_parent_reason==6);owner_stop();
 }
 else if(which=="lock-rc-renewal"||which=="lock-direct-renewal"){
  const bool relay=which=="lock-rc-renewal";owner_lock(relay);const auto current=v12::joined_peer;const auto address=v12::sync_address;
  auto& q=const_cast<r2_link_quality_view&>(v12::parent_selection.quality());q.last_rx=fake_now;q.poor=true;
  if(relay)v12::rc_client.renew_at=fake_now;else v12::anchor.uncertainty=100000;
  const auto scans=fixture_parent_scan_calls,attempts=connected;v12::active_poll(fake_now);
  OWNER_CHECK(connected==attempts+1&&v12::peer.identity==current&&v12::peer.relay==relay&&!v12::peer.rescan);
  OWNER_CHECK(v12::same_address(v12::peer.address,address)&&fixture_parent_scan_calls==scans&&!scan_active);
  ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;v12::gap(&e,conn_arg);step(18);
  if(relay)OWNER_CHECK(fixture_request.operation==0&&fixture_request.target==current);else{
   OWNER_CHECK(!output.empty());r2_join_record request{};OWNER_CHECK(!r2_group_join_decode(&request,output.back().data(),R2_GROUP_JOIN_BYTES,&v12::provision.group));
   OWNER_CHECK(request.kind==R2_JOIN_REQUEST&&request.receiver==current&&request.client_nonce==v12::peer.exchange.nonce);
  }owner_stop();
 }
 else if(which=="lock-expiry"){
  owner_lock();const auto clock=v12::corrected_clock.prediction();const auto quality=v12::corrected_clock.quality(fake_now,fake_now*20);
  const auto context=v12::anchor.context_until;v12::anchor.context_until=fake_now+999999;
  OWNER_CHECK(!v12::lock_protected_quality(quality,clock,fake_now*20,fake_now));v12::anchor.context_until=fake_now+1000000;
  OWNER_CHECK(v12::lock_protected_quality(quality,clock,fake_now*20,fake_now));v12::anchor.context_until=context;
  const auto permission=v12::anchor.until;v12::anchor.until=fake_now;OWNER_CHECK(!v12::lock_protected(fake_now));v12::anchor.until=permission;
  v12::Rc7PermissionInspect::expire(v12::corrected_clock,fake_now+1);OWNER_CHECK(v12::lock_protected(fake_now));++fake_now;
  OWNER_CHECK(!v12::lock_protected(fake_now));const auto scans=fixture_parent_scan_calls;v12::start_scan();OWNER_CHECK(fixture_parent_scan_calls==scans+1);
  v12::changed_scope(fake_now);OWNER_CHECK(!v12::lock_protected(fake_now)&&v12::parent_selection.parent()==0);owner_stop();
 }
 else if(which=="lock-clock-failure"){
  owner_lock();const auto scans=fixture_parent_scan_calls;fixture_parent_clock_ok=false;
  OWNER_CHECK(!v12::lock_protected(fake_now));v12::start_scan();OWNER_CHECK(fixture_parent_scan_calls==scans+1);
  fixture_parent_clock_ok=true;OWNER_CHECK(v12::lock_protected(fake_now));owner_stop();
 }
 else if(which=="lock-go-withdraw"){
  owner_lock();OWNER_CHECK(v12::r2_set_relay_only(true));OWNER_CHECK(v12::parent_diagnostic.pending()&&!v12::lock_protected(fake_now));step(8);
  OWNER_CHECK(!v12::parent_diagnostic.pending()&&v12::exclude_direct_gm&&!v12::lock_protected(fake_now));
  OWNER_CHECK(!v12::following(fake_now)&&v12::parent_selection.parent()==0);owner_stop();
 }
 else if(which=="lock-quiet"){
  OWNER_CHECK(V12_R2_G1_LISTENING_PERCENT==50);owner_lock();const auto samples=v12::parent_selection.quality().samples;
  const auto scans=fixture_parent_scan_calls;fake_now=1450000;v12::active_poll(fake_now);pump_cleanup();step(4);
  OWNER_CHECK(!v12::owners_active()&&fixture_parent_scan_calls==scans);OWNER_CHECK(v12::parent_selection.quality().samples==samples&&!v12::fault&&!v12::quiet_faults);owner_stop();
 }
 else if(which=="lock-known-recreate"){
  OWNER_CHECK(V12_R2_G1_LISTENING_PERCENT==50);owner_lock();const auto current=v12::joined_peer;
  fake_now=1450000;v12::active_poll(fake_now);pump_cleanup();step(4);OWNER_CHECK(!v12::owners_active());
  fake_now=v12::G1Window::start(fake_now+1000000,v12::window_phase)+1000;
  v12::rc_client.renew_at=fake_now;v12::retry_at=fake_now;for(auto& r:v12::recent)if(r.identity==current)r.until=fake_now;
  const auto scans=fixture_parent_scan_calls,attempts=connected;
  OWNER_CHECK(v12::lock_protected(fake_now));v12::active_poll(fake_now);
  OWNER_CHECK(v12::sync_pending&&v12::sync_identity==current&&fixture_parent_scan_calls==scans&&connected==attempts);
  sync_established();rc_packet(11);step(2);
  OWNER_CHECK(v12::peer.connecting&&v12::peer.identity==current&&!v12::peer.rescan&&fixture_parent_scan_calls==scans);owner_stop();
 }
 else if(which=="lock-scan-retry"){
  owner_lock();v12::scan_retry_at=fake_now+100000;v12::scan_retry_until=fake_now+2000000;
  v12::active_poll(fake_now);OWNER_CHECK(!v12::scan_retry_at&&!v12::scan_retry_until&&!scan_active);
  if(V12_R2_G1_LISTENING_PERCENT==50){fake_now=1450000;v12::active_poll(fake_now);pump_cleanup();step(4);OWNER_CHECK(!v12::owners_active());}
  // The intentionally cancelled old retry cannot remain a deadline ten
  // seconds later. The synthetic model expires; this asserts no new authority.
  fake_now+=10000000;OWNER_CHECK(!v12::lock_protected(fake_now));const auto scans=fixture_parent_scan_calls;
  v12::start_scan();OWNER_CHECK(fixture_parent_scan_calls==scans+1&&!v12::fault&&scan_active&&!v12::scan_retry_until);owner_stop();
 }else assert(false&&"unknown focused owner case");
 std::printf("PASS %s profile%d %u actual-native synthetic owner assertions\n",which.c_str(),V12_R2_G1_LISTENING_PERCENT,owner_checks);
}
