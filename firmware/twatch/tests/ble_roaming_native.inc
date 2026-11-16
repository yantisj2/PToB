// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
int main(int argc,char** argv){
 assert(argc==2);const std::string which=argv[1];
 const uint8_t mac[]={2,3,4,5,6,7};
 assert(v12::r2_init(44,mac)&&v12::r2_host_init()&&v12::r2_host_sync());step(8);v12::stop_scan();
 constexpr uint64_t start=100000000,T=n30::BleAcquisitionRecovery::StallUs;
 fake_now=start;
 v12::Rc7PermissionInspect::physical_update(v12::corrected_clock);
 v12::ble_acquisition_poll(fake_now);
 // No fresh locators or RSSI. Existing PA handle remains owned while silent.
 v12::sync_identity=v12::joined_peer=501;v12::joined_boot=601;
 v12::sync_address=v12::sync_owner_address=v12::canonical(501);
 v12::synced=true;v12::sync_handle=9;
 if(which=="drain-failure")sync_terminate_error=BLE_HS_ETIMEOUT;
 fake_now=start+T-1;v12::ble_acquisition_poll(fake_now);
 assert(!v12::parent_diagnostic.pending());
 if(which=="off")v12::r2_stop();
 if(which=="fault")v12::fault=true;
 if(which=="busy")v12::peer.connecting=true;
 fake_now=start+T;v12::ble_acquisition_poll(fake_now);
 if(which=="off"||which=="fault"||which=="busy"){
  assert(!v12::parent_diagnostic.pending()&&!v12::ble_acquisition_recovery.rebinds());return 0;
 }
 assert(v12::parent_diagnostic.pending()&&v12::ble_acquisition_recovery.rebinds()==1);
 const auto deadline=v12::parent_diagnostic.deadline_us;
 assert(deadline==fake_now+2000000);
 v12::r2_host_poll();pump_cleanup();v12::r2_host_poll();
 if(which=="drain-failure"){
  fake_now=deadline;v12::r2_host_poll();
  assert(v12::fault&&v12::parent_diagnostic.state!=3&&v12::parent_diagnostic.deadline_us==deadline);return 0;
 }
 assert(!v12::fault&&v12::parent_diagnostic.state==3);
 assert(!v12::synced&&v12::sync_handle==v12::None&&!v12::anchor.source&&!v12::following(fake_now));
 assert(!v12::parent_diagnostic.target); // AUTO survives; no pin to departed GM.
 // A different local GM is now admitted for a fresh JOIN, never inherited time.
 v12::Discovery d{};d.props=1;d.sid=12;d.interval=50;d.rssi=-45;
 d.at=fake_now;d.length=25;d.address=v12::canonical(777);
 d.bytes[0]=24;d.bytes[1]=0xff;auto* b=d.bytes+2;b[0]='G';b[1]='1';b[2]=1;
 r2_group_put(b+3,v12::provision.group.id,8);r2_group_put(b+11,v12::provision.group.epoch,4);r2_group_put(b+15,777,8);
 v12::discoveries.push(d);v12::active_poll(fake_now);
 assert(v12::peer.connecting&&v12::peer.identity==777&&!v12::following(fake_now));
 std::puts("PASS silent old PA drained within original deadline; AUTO discovers different GM and requires fresh JOIN");
}
