// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Sole relay controller owner: RC9 replaces RC7, never overlaps its adverts.
#if defined(PTOB_RC9) && defined(PTOB_W12_LORA_RX)
rc9::Time rc9_sent;
uint32_t rc9_publication=0;
uint64_t rc9_packet_at=0;
uint64_t rc9_server_demand_until(){return relay_profile==9?relay_demand_until():0;}
uint8_t rc9_server_clients(uint64_t now){
 uint8_t n=0;if(relay_profile==9)for(const auto& c:relay_children)if(c.id&&rc5_demand_live(&c.demand,now))++n;return n;
}
bool rc9_server_selected(uint64_t now){
 w12_lora_view l;w12_lora_snapshot(l);now=micros();uint64_t utc=0;
 const bool permit=full_listening()&&!closing&&!fault&&!stop_pending&&l.mode!=uint8_t(w12::LoRaMode::KeepOff)&&!l.fault;
 return w12::rc9_source(lock_protected(now),w12::lora_time(l,now,utc),l.active&&l.requested,
   rc9_server_demand_until(),now,permit).select_rc8;
}
bool rc9_cap_update(uint64_t now){
 if(relay_session.handle!=None||peer.connecting||peer.handle!=None)return false;
 relay_pause_cap();rc9::Control c;c.sender=provision.identity;c.sender_boot=boot_id;c.next_challenge=relay_challenge;
 uint8_t wire[167]={2,1,6,17,7};std::memcpy(wire+5,rc9_service.value,16);wire[21]=145;wire[22]=255;
 if(!rc9::encode_control(relay_cap,c,provision.group)||!rc9::encode_capability(wire+23,c,provision.group)){fault=true;return false;}
 relay_cap_path={};auto* mb=ble_hs_mbuf_from_flat(wire,sizeof wire);if(!mb){fault=true;return false;}
 if(!relay_api(ble_gap_ext_adv_set_data(1,mb),R2Api::AdvData))return false;
 relay_cap_at=now;relay_cap_dirty=false;return true;
}
void rc9_requests(uint64_t now){
 RelayWrite row{};for(unsigned n=0;n<2&&relay_writes.pop(row);++n){
  if(row.handle!=relay_session.handle||row.generation!=relay_session.generation||relay_session.replied||
     relay_session.terminating||now<row.at||now>=relay_session.until){inc(relay_refused);continue;}
  rc9::Control q;
  if(!rc9::decode_control(q,row.wire,212,provision.group)||q.type!=rc9::Type::Request||q.target!=provision.identity||
     q.target_boot!=boot_id||q.challenge!=relay_session.challenge){inc(relay_refused);relay_terminate(now);continue;}
  unsigned at=8;for(unsigned i=0;i<8;++i)if(relay_children[i].id==q.sender){at=i;break;}
  if(at==8)for(unsigned i=0;i<8;++i)if(!relay_children[i].id||!rc5_demand_live(&relay_children[i].demand,now)){at=i;break;}
  if(at==8||rc9_publication==UINT32_MAX){inc(relay_refused);relay_terminate(now);continue;}
  auto child=relay_children[at];if(child.id!=q.sender)child={};child.id=q.sender;
  if(!rc5_demand_begin(&child.demand,boot_id,q.sender_boot,q.challenge,now)||
     !rc5_demand_accept(&child.demand,boot_id,q.sender_boot,q.challenge,q.sequence,q.lease_us,now)){
   inc(relay_refused);relay_terminate(now);continue;
  }
  rc9::Control c;c.type=rc9::Type::Response;c.sender=provision.identity;c.sender_boot=boot_id;
  c.target=q.sender;c.target_boot=q.sender_boot;c.challenge=q.challenge;c.nonce=q.nonce;c.sequence=q.sequence;
  c.lease_us=q.lease_us;c.next_challenge=relay_challenge;c.server_us=micros();c.publication_floor=rc9_publication+1;c.ready=rc9_sent.valid&&relay_pa;
  uint8_t reply[212];if(!rc9::encode_control(reply,c,provision.group)){inc(relay_refused);relay_terminate(now);continue;}
  relay_children[at]=child;std::memcpy(relay_session.reply,reply,212);relay_session.replied=true;
  relay_data_dirty=true;inc(relay_admissions);
 }
}
bool rc9_make_time(uint64_t now,uint8_t wire[190]){
 if(rc9_publication==UINT32_MAX){fault=true;return false;}
 w12_lora_view l;w12_lora_snapshot(l);now=micros();uint64_t utc=0;rc9::Time p;
 p.sender=provision.identity;p.sender_boot=boot_id;p.publication=rc9_publication+1;p.sampled_us=now;
 p.valid=w12::lora_time(l,now,utc);
 if(p.valid){
  p.edge_us=l.edge_us;p.until_us=l.until_us;p.event_ns=l.event_ns;p.root=l.root;p.root_boot=l.root_boot;
  p.emitter=l.emitter;p.emitter_boot=l.emitter_boot;p.stream=l.stream;p.created_ns=l.created_ns;p.expires_ns=l.expires_ns;
  p.source_epoch=l.source_epoch;p.generation=l.generation;p.corrected_sequence=l.corrected_sequence;
  p.link_rms_ns=l.link_rms_ns;p.link_samples=l.link_samples;p.assurance=l.assurance;p.authority=l.authority;p.kind=l.kind;
  std::memcpy(p.context,l.context,16);
 }
 if(!rc9::encode_time(wire,p,provision.group)){fault=true;return false;}
 rc9_sent=p;++rc9_publication;rc9_packet_at=now;return true;
}
void rc9_server_poll(uint64_t now,bool upstream_due){
 if(!relay_configured&&!relay_configure())return;
 relay_quarantine_poll(now);if(fault)return;
 relay_admission_locked=rc9_server_selected(now);if(!relay_admission_locked)return;
 if(relay_session.terminating&&now>=relay_session.drain){fault=true;return;}
 if(relay_session.handle!=None&&now>=relay_session.until)relay_terminate(now);
 rc9_requests(now);if(fault)return;
 // Source withdrawal takes effect without waiting for the routine250ms update.
 w12_lora_view l;w12_lora_snapshot(l);now=micros();uint64_t utc=0;const bool live=w12::lora_time(l,now,utc);
 if(!relay_pa||relay_data_dirty||now-rc9_packet_at>=250000||live!=rc9_sent.valid){
  uint8_t wire[190];if(!rc9_make_time(micros(),wire))return;
  auto* mb=ble_hs_mbuf_from_flat(wire,sizeof wire);if(!mb){fault=true;return;}
  if(!relay_api(ble_gap_periodic_adv_set_data(0,mb),R2Api::AdvData))return;
  relay_data_dirty=false;
 }
 if(!relay_pa){
  if(!relay_api(ble_gap_periodic_adv_start(0),R2Api::AdvStart))return;
  relay_pa=true;
  static const uint8_t marker[3]={2,1,6};auto* mb=ble_hs_mbuf_from_flat(marker,sizeof marker);if(!mb){fault=true;return;}
  if(!relay_api(ble_gap_ext_adv_set_data(0,mb),R2Api::AdvData)||!relay_api(ble_gap_ext_adv_start(0,0,0),R2Api::AdvStart))return;
  relay_discovery=true;relay_cap_dirty=true;
 }
 if(!upstream_due&&relay_session.handle==None&&!peer.connecting&&peer.handle==None){
  if(relay_cap_dirty&&!rc9_cap_update(micros()))return;
  if(!relay_advertising){const int rc=ble_gap_ext_adv_start(1,0,0);if(!rc)relay_advertising=true;else relay_api(rc,R2Api::AdvStart);}
 }
}
#else
#ifdef PTOB_RC9
uint64_t rc9_server_demand_until(){return 0;}
uint8_t rc9_server_clients(uint64_t){return 0;}
bool rc9_server_selected(uint64_t){return false;}
#endif
#endif
