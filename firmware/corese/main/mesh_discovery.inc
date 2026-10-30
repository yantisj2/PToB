// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Included inside the serialized receiver owner, after its radio operations.
void forget_leader(uint64_t now,bool keep_admission);
void corese_select_mesh(const Discovery& d){
 const auto now=micros();
 if(mode!=R2Mode::Client||closing||fault||stop_pending||parent_diagnostic.pending()||
    member||anchor.source||rc_bound()||peer.connecting||peer.handle!=None||peer.pending||peer.terminating||
    sync_pending||synced||sync_selected||sync_cleanup_queued||sync_cleanup_requested||sync_terminating||sync_handle!=None||
    ble_gap_conn_active()||now<d.at||now-d.at>250000)return;
 r2_group_config candidate{};
 if(!corese::mesh_locator(candidate,provision.group,d.bytes,d.length,d.sid,d.props,d.address.type,d.address.val,provision.identity)||
    (candidate.id==corese_group.id&&candidate.epoch==corese_group.epoch))return;
 stop_scan();
 if(fault||scan||ble_gap_disc_active())return;
 uint8_t address[6];corese::mesh_address(address,candidate,provision.identity);
 const auto result=ble_hs_id_set_rnd(address);
 if(result){error(result,R2Api::Address);return;}
 // No ACL/PA owner survives this transition. Old timing, diagnostics, TZ,
 // shadow history and locators are revoked before any new JOIN can start.
 forget_leader(now,false);
 corese_group=candidate;own_address.type=BLE_ADDR_RANDOM;std::memcpy(own_address.val,address,6);
}
