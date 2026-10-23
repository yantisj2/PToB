// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
void client_case(bool normal_pin=false){
 const auto* mac=v12::native_device_binding()->factory;assert(v12::r2_init(44,mac)&&v12::r2_host_init()&&v12::r2_host_sync());step(8);
 if(normal_pin){assert(v12::r2_gm_parent(11));step(8);}
 v12::Discovery d{};d.sid=15;d.props=1;d.rssi=-30;d.at=fake_now;d.address=v12::canonical(77);d.length=167;
 d.bytes[0]=2;d.bytes[1]=1;d.bytes[2]=6;d.bytes[3]=17;d.bytes[4]=7;std::memcpy(d.bytes+5,v12::rc9_service.value,16);d.bytes[21]=145;d.bytes[22]=255;
 v12::rc9::Control cap;cap.sender=77;cap.sender_boot=88;cap.next_challenge=99;
 assert(v12::rc9::encode_capability(d.bytes+23,cap,v12::provision.group));
 v12::discoveries.push(d);step(21);
 if(normal_pin){assert(!v12::peer.connecting&&!v12::rc9_bound());assert(v12::r2_rc9_parent(77));step(8);d.at=fake_now;v12::discoveries.push(d);step(21);}
 assert(v12::peer.connecting&&v12::peer.rc9);
 ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;v12::gap(&e,conn_arg);step(18);
 assert(!v12::fault&&v12::rc9_bound()&&v12::sync_pending&&v12::sync_selected_sid==14);
 sync_established();assert(v12::synced&&!v12::fault&&v12::periodic_rx::armed);
 v12::rc9::Time p;p.sender=77;p.sender_boot=88;p.publication=10;p.sampled_us=fake_now;p.edge_us=fake_now-100;
 p.until_us=p.edge_us+12000000;p.event_ns=1700000000000000000ULL;p.root=66;p.emitter=55;p.kind=2;
 p.source_epoch=p.generation=p.corrected_sequence=1;p.context[0]=1;p.created_ns=p.event_ns-100;p.expires_ns=p.event_ns+1000000;p.valid=true;
 uint8_t wire[190];assert(v12::rc9::encode_time(wire,p,v12::provision.group));
 e={};e.type=BLE_GAP_EVENT_PERIODIC_REPORT;e.periodic_report.data_length=190;e.periodic_report.data=wire;
 v12::gap(&e,fixture_sync_arg);step(2);const auto r=v12::r2_rc9_status();const auto normal=v12::r2_status();
 assert(r.valid&&r.following&&r.server==77&&r.root==66&&r.accepted==1&&!r.serving&&!r.clients);
 assert(!normal.following&&!normal.clock_valid&&!normal.g1_clock_model.tick&&!normal.discipline_accepted&&!pa_active);
 v12::r2_stop();assert(!v12::r2_rc9_status().valid);step(8);assert(!v12::rc9_bound()&&!v12::r2_rc9_status().valid&&v12::r2_status().off_ack);
 puts("PASS actual RC9 discovery/GATT/SID14 capture/report/UTC isolation/OFF");
}
