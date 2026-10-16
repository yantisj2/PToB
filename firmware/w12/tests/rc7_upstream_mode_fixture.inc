// Included after the existing synthetic actual-owner fakes, before main.
void mode_finish(){for(unsigned i=0;i<8&&v12::parent_diagnostic.pending();++i)step();
 assert(!v12::parent_diagnostic.pending()&&!v12::fault);
 assert(v12::parent_diagnostic.completed_us<v12::parent_diagnostic.deadline_us);}
void mode_set(bool enabled){assert(v12::r2_set_relay_only(enabled));if(v12::parent_diagnostic.pending())mode_finish();
 assert(v12::r2_status().rc7_relay_only==enabled);}
void mode_old_source(){
 v12::anchor={true,55,66,1700000000000000,fake_now,fake_now+1000000,7,9,100};
 v12::joined_peer=55;v12::joined_boot=66;v12::member=true;v12::coarse_hold.retain(v12::anchor);
 v12::publish(fake_now);assert(v12::r2_status().following);
}
void mode_old_sync(){
 mode_old_source();v12::sync_identity=55;v12::sync_handle=9;v12::synced=true;
 v12::sync_address=v12::canonical(55);v12::sync_capture_epoch=v12::periodic_rx::scope(9,true);
 fixture_sync_arg=reinterpret_cast<void*>(uintptr_t(v12::sync_capture_epoch));
}
int main(int argc,char** argv){assert(argc==2);const std::string which=argv[1];
 assert(!v12::r2_set_relay_only(true));
 const uint8_t mac[]={0x44,0xb1,0x76,0xae,0x17,0x78};
 assert(v12::r2_init(44,mac)&&v12::r2_host_init()&&v12::r2_host_sync());step();
 assert(v12::client.running&&!v12::fault);const auto client_generation=v12::client.generation;
 if(which=="boot-default"){
#ifdef V12_R2_RC7_EXCLUDE_DIRECT_GM
  assert(v12::r2_status().rc7_relay_only&&v12::r2_activity().relay_only);
#else
  assert(!v12::r2_status().rc7_relay_only&&!v12::r2_activity().relay_only);
#endif
 }else{
  mode_set(false); // Also proves a compatibility build default is overridable.
  if(which=="normal-clears-gm-pin"||which=="normal-clears-rc-pin"){
   assert(which=="normal-clears-gm-pin"?v12::r2_gm_parent(55):v12::r2_rc_parent(55));mode_finish();
   assert(v12::parent_selection.pinned());mode_old_source();
   assert(v12::r2_set_relay_only(false));assert(!v12::r2_status().following);
   assert(v12::parent_diagnostic.target==0&&v12::r2_status().rc7_upstream_mode_pending);
   mode_finish();assert(!v12::parent_selection.pinned()&&!v12::anchor.source&&!v12::coarse_hold.saved.source);
  }else if(which=="published-relay-fenced"){
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
   // Use the same explicit synthetic source/model seam as the existing server
   // owner fixture to create an actually owned PA+TX observer before toggling.
   fake_now=1000010;rc7_path root{};root.source_epoch=9;root.node[0]={11,22,1,1,0,0};
   rc5_origin origin={11,22,9,1000000000,2650000000,1700000000000000000,1000000000,0,0};
   v12::G1ClockPrediction map{fake_now*20,1000010000,0,0,origin.utc_offset};
   assert(v12::relay_map.input(root,origin,nullptr,map,20000000,1,fake_now*20,0,
     v12::provision.identity,44,v12::provision.group));
   v12::relay_data_dirty=v12::relay_cap_dirty=true;v12::relay_poll(fake_now,false);
   fake_now+=16000;v12::relay_poll(fake_now,false);
   assert(pa_active&&ext_active[0]&&v12::periodic_tx::tx_installed);
   assert(v12::r2_set_relay_only(true));
   assert(pa_active&&v12::r2_status().rc7_upstream_mode_pending); // Caller never touches controller.
   mode_finish();assert(!pa_active&&!ext_active[0]&&!v12::periodic_tx::tx_installed&&!v12::relay_map.valid);
   step(6);assert(!pa_active&&!v12::relay_map.ready(fake_now*20)); // Fresh source required to publish again.
#else
   assert(false&&"BLE50 cannot compile a relay publisher");
#endif
  }else if(which=="toggle-and-select"){
   mode_old_source();const auto connect_calls=connected,cancelled=sync_cancel_calls;const auto old_scan=scan_active;
   assert(v12::r2_set_relay_only(true));const auto pending=v12::r2_status();
   assert(!pending.rc7_relay_only&&pending.rc7_relay_only_requested&&pending.rc7_upstream_mode_pending);
   assert(!pending.following&&!pending.clock_valid&&!pending.rc7_bound);
   assert(v12::r2_activity().relay_only&&v12::r2_activity().mode_pending);
   assert(connected==connect_calls&&sync_cancel_calls==cancelled&&scan_active==old_scan);
   const auto deadline=v12::parent_diagnostic.deadline_us;
   assert(!v12::r2_set_relay_only(false)&&v12::parent_diagnostic.deadline_us==deadline);
   assert(!v12::r2_rc_parent(77));mode_finish();
   assert(!v12::anchor.source&&!v12::rc_bound()&&!v12::coarse_hold.saved.source);
   assert(!v12::r2_gm_parent(55));
   v12::parent_selection.seen(55,false,-5,fake_now);v12::parent_selection.seen(77,true,-50,fake_now);
   assert(v12::parent_selection.choose(fake_now+40000)==77);
   mode_set(false);assert(!v12::parent_selection.pinned()&&!v12::parent_selection.diagnostic());
   assert(v12::parent_selection.choose(fake_now+40000)==0); // Old locators retired.
   v12::parent_selection.seen(55,false,-5,fake_now);v12::parent_selection.seen(77,true,-50,fake_now);
   assert(v12::parent_selection.choose(fake_now+40000)==55);
   assert(!v12::r2_status().following); // Policy/discovery alone gives no time.
  }else if(which=="slow-sync-drain"||which=="drain-timeout"){
   mode_old_sync();sync_late=true;assert(v12::r2_set_relay_only(true));
   const auto deadline=v12::parent_diagnostic.deadline_us;step();
   assert(v12::parent_diagnostic.pending()&&v12::synced&&!v12::r2_status().rc7_relay_only);
   assert(!v12::anchor.source&&!v12::r2_status().following);
   assert(!v12::r2_set_relay_only(false)&&v12::parent_diagnostic.deadline_us==deadline);
   if(which=="drain-timeout"){
    fake_now=deadline;v12::r2_observe(fake_now);v12::r2_host_poll();
    assert(v12::fault&&v12::parent_diagnostic.state==4&&!v12::r2_status().rc7_relay_only);
    assert(!v12::r2_status().rc7_upstream_mode_pending&&!v12::r2_set_relay_only(false));
    puts("PASS original2s mode drain timeout retains fault and does not apply requested mode");return 0;
   }
   // Complete well inside the50-profile's original quiet deadline as well.
   fake_now+=20000;ble_gap_event e{};e.type=BLE_GAP_EVENT_PERIODIC_SYNC_LOST;
   v12::gap(&e,fixture_sync_arg);mode_finish();assert(v12::r2_status().rc7_relay_only&&!v12::synced);
  }else if(which=="stop-queued"||which=="stop-draining"){
   if(which=="stop-draining"){mode_old_sync();sync_late=true;}
   assert(v12::r2_set_relay_only(true));const auto deadline=v12::parent_diagnostic.deadline_us;
   if(which=="stop-draining")step();fake_now+=1000;v12::r2_stop();
   assert(!v12::parent_diagnostic.pending()&&!v12::r2_status().rc7_upstream_mode_pending);
   assert(!v12::r2_activity().relay_only&&v12::client.stop_deadline_us==deadline);
   assert(v12::client.stop_deadline_us-v12::client.stop_requested_us==2000000);
   if(which=="stop-draining"){ble_gap_event e{};e.type=BLE_GAP_EVENT_PERIODIC_SYNC_LOST;v12::gap(&e,fixture_sync_arg);}
   step(6);assert(v12::r2_status().off_ack&&!v12::fault&&!v12::r2_set_relay_only(true));
   puts("PASS explicit OFF cancels mode request and preserves earliest actual drain admission/deadline");return 0;
  }else if(which=="activity"){
   auto before=v12::r2_activity();ble_gap_event e{};e.type=BLE_GAP_EVENT_EXT_DISC;
   e.ext_disc.data_status=1;v12::gap(&e,nullptr); // Count even rejected discovery.
   v12::publish(fake_now);auto after=v12::r2_activity();assert(after.rx==before.rx+1&&after.requested);
   v12::synced=true;v12::sync_handle=9;e={};e.type=BLE_GAP_EVENT_PERIODIC_REPORT;
   e.periodic_report.sync_handle=9;e.periodic_report.data_status=1;
   v12::gap(&e,reinterpret_cast<void*>(uintptr_t(v12::sync_capture_epoch)));
   v12::publish(fake_now);assert(v12::r2_activity().rx==after.rx+1);v12::synced=false;v12::sync_handle=UINT16_MAX;
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
   fixture_tx_calls=7;v12::periodic_tx::tx_installed=true;
   const auto tx=v12::r2_activity();assert(tx.tx==7&&tx.tx_armed);
   v12::periodic_tx::tx_installed=false;assert(!v12::r2_activity().tx_armed);
#else
   assert(!after.tx&&!after.tx_armed&&!v12::relay_owned());
#endif
   assert(!v12::r2_status().following);
  }else assert(false&&"Unknown focused upstream case");
 }
 assert(v12::client.generation==client_generation&&v12::client.running&&!v12::fault);
 v12::r2_stop();step(6);assert(v12::r2_status().off_ack);
 puts("PASS focused native runtime upstream selection, activity and original owner fencing");
}
