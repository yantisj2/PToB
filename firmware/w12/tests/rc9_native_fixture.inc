// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
w12_lora_view rc9_radio{};bool snapshot_race=false;
void w12_lora_snapshot(w12_lora_view& out){
 if(snapshot_race){++fake_now;rc9_radio.sampled_us=fake_now;}out=rc9_radio;
}
void radio_live(){
 auto& l=rc9_radio;l={};l.requested=l.active=true;l.state=2;l.sampled_us=fake_now;
 l.metadata_known=l.time_valid=l.model_valid=true;
 l.utc_us=1700000000000000ULL+fake_now;l.edge_us=fake_now-100;l.until_us=l.edge_us+120000000;
 l.event_ns=(l.utc_us-100)*1000;
 l.emitter=55;l.root=66;l.emitter_boot=101;l.root_boot=102;l.stream=103;
 l.source_epoch=1;l.generation=1;l.corrected_sequence=1;l.context[0]=1;l.kind=2;
 l.link_rms_ns=123;l.link_samples=8;
}
void* rc9_arg(){return reinterpret_cast<void*>(uintptr_t(9));}
v12::rc9::Control read9(){os_mbuf b;ble_gatt_access_ctxt c{};c.op=BLE_GATT_ACCESS_OP_READ_CHR;c.om=&b;
 assert(!v12::relay_access(33,0,&c,rc9_arg()));v12::rc9::Control cap;
 assert(v12::rc9::decode_control(cap,b.bytes.data(),b.bytes.size(),v12::provision.group));return cap;}
void write9(const v12::rc9::Control& q){os_mbuf b;b.bytes.resize(212);assert(v12::rc9::encode_control(b.bytes.data(),q,v12::provision.group));
 ble_gatt_access_ctxt c{};c.om=&b;assert(!v12::relay_access(33,0,&c,rc9_arg()));v12::relay_poll(fake_now,false);}
int main(int argc,char** argv){assert(argc==2);const std::string which=argv[1];
 if(which=="client-terminate-ack"){client_case(false,"terminate-ack");return 0;}
 if(which=="client-cache-fence"){client_case(false,"cache-fence");return 0;}
 if(which=="client-pending-relay-return"){client_case(false,"pending-relay-return");return 0;}
 if(which=="client-relay-return"){client_case(false,"relay-return");return 0;}
 if(which=="client"||which=="client-pin"){client_case(which=="client-pin");return 0;}
 if(which=="client-scan"||which=="client-return"||which=="client-expired"||which=="client-recovery-pin"||which=="client-no-source"||which=="client-hint"){
  client_case(which=="client-recovery-pin",which=="client-scan"?"scan":which=="client-expired"?"expired":which=="client-recovery-pin"?"pin":which=="client-no-source"?"no-source":which=="client-hint"?"hint":"return");return 0;
 }
 const auto* mac=v12::native_device_binding()->factory;assert(v12::r2_init(44,mac)&&v12::r2_host_init()&&v12::r2_host_sync());
 step(8);assert(v12::client.running);
 if(which=="rc7-placement-while-lora-live"||which=="rc7-hold-while-lora-live"){
  const bool placement=which=="rc7-placement-while-lora-live";
  rc7_path root{};root.source_epoch=9;root.node[0]={11,22,1,1,0,0};
  const uint64_t ns=fake_now*1000;
  const rc5_origin origin={11,22,9,ns,ns+1650000000,1700000000000000000,ns/62500000*62500000,0,0};
  const v12::G1ClockPrediction map{fake_now*20,int64_t(ns),0,0,origin.utc_offset};
  assert(v12::relay_map.input(root,origin,nullptr,map,fake_now*20,1,fake_now*20,0,
    v12::provision.identity,44,v12::provision.group));
  v12::joined_peer=v12::sync_identity=11;v12::joined_boot=22;v12::synced=true;v12::sync_handle=17;
  v12::anchor={true,11,22,1700000000000000,fake_now,fake_now+100000,9,1,500,fake_now+10000000};
  if(!placement){
   v12::relay_retained_slot=v12::relay_map.path.node[1].slot;
   v12::relay_startup_checked=true;
  }
  v12::Rc7Client child;assert(!child_request(child,300,RC_DYNAMIC_LEASE_US).ready);
  if(!placement){fake_now+=16000;v12::relay_poll(fake_now,false);}
  assert(v12::relay_slots.scanning==placement&&pa_active==!placement&&v12::relay_admissions==1);
  const auto limit=v12::relay_slots.scan_limit;
  fake_now+=200000;fixture_parent_locked=false;radio_live();
  v12::relay_poll(fake_now,false);
  assert(v12::relay_profile==7&&v12::relay_slots.scanning==placement&&v12::relay_slots.scan_limit==limit);
  assert(!v12::relay_admission_locked&&v12::relay_demand_live(fake_now)&&pa_active==!placement&&!ext_active[1]);
  if(!placement){
   rc7_packet standby{};
   assert(!rc7_decode(&standby,pa_wire.data(),190,&v12::relay_map.path,&v12::provision.group)&&!standby.count);
  }
  v12::sync_terminating=true;
  v12::relay_poll(fake_now,false);
  assert(!v12::fault&&v12::relay_profile==9&&pa_active&&ext_active[1]&&!v12::relay_slots.scanning);
  puts("PASS live LoRa preserves the RC7 owner through a gap and starts RC9 after BLE acquisition loss");
  return 0;
 }
 fixture_parent_locked=false;radio_live();v12::relay_poll(fake_now,false);
 std::fprintf(stderr,"state fault=%u profile=%u pa=%u adv=%u/%u selected=%u configured=%u stopping=%u api=%u rc=%d\n",v12::fault,v12::relay_profile,pa_active,ext_active[0],ext_active[1],v12::rc9_server_selected(fake_now),v12::relay_configured,v12::relay_stopping,unsigned(v12::first_error_api),v12::first_error_rc);
 assert(!v12::fault&&v12::relay_profile==9&&pa_active&&ext_active[0]&&ext_active[1]&&!v12::periodic_tx::tx_installed);
 assert(!v12::relay_flags(fake_now*20));v12::rc9::Time p;assert(v12::rc9::decode_time(p,pa_wire.data(),190,v12::provision.group)&&p.valid);
 assert(p.link_rms_ns==123&&p.root_boot==102&&p.emitter_boot==101&&p.edge_us==rc9_radio.edge_us&&p.until_us==rc9_radio.until_us);
 assert(p.utc_us==rc9_radio.utc_us&&!p.created_ns&&!p.expires_ns);
 const auto physical_until=p.until_us;
 if(which=="speculative"){
  v12::relay_poll(fake_now,true);assert(ext_active[1]&&v12::rc9_server_flags()==15&&!v12::fault);
  v12::relay_poll(fake_now,true,true);assert(!ext_active[1]&&v12::rc9_server_flags()==7&&!v12::fault);
  v12::relay_poll(fake_now,true);assert(ext_active[1]&&v12::rc9_server_flags()==15);
 }else if(which=="client-pin-local-lora"){
  assert(v12::r2_rc9_parent(77));step(8);
  assert(v12::parent_diagnostic.state==3&&!v12::rc9_server_selected(fake_now));
  assert(!pa_active&&!ext_active[0]&&!ext_active[1]);
 }else if(which=="race"){
  snapshot_race=true;fake_now+=300000;v12::relay_poll(fake_now,false);
  assert(v12::rc9::decode_time(p,pa_wire.data(),190,v12::provision.group)&&p.valid&&p.sampled_us>=rc9_radio.sampled_us);
 }else{
  server_connect();os_mbuf old;ble_gatt_access_ctxt wrong{};wrong.op=BLE_GATT_ACCESS_OP_READ_CHR;wrong.om=&old;
  assert(v12::relay_access(33,0,&wrong,nullptr)==BLE_ATT_ERR_UNLIKELY&&old.bytes.empty());
  const auto cap=read9();v12::rc9::Client child;assert(child.make_request(cap,77,88,99,fake_now));write9(child.request);
  const auto reply=read9();assert(child.bind(reply,fake_now));server_disconnect();v12::relay_poll(fake_now,false);
  const auto lease=v12::rc9_server_demand_until();assert(lease==fake_now+v12::rc9::LeaseUs);
  assert(v12::rc9_server_clients(fake_now)==1&&v12::rc9_server_selected(fake_now));
  if(which=="recover"){
   // The request is queued before recovery but the actual worker observes a
   // qualified normal source before accepting it. Neither renewal nor a new
   // client may perpetually pin fallback after this point.
   server_connect();const auto renewal_cap=read9();v12::rc9::Client renewal;
   assert(renewal.make_request(renewal_cap,77,88,100,fake_now));
   const auto admissions=v12::relay_admissions;fixture_parent_locked=true;server_slow=true;
   write9(renewal.request);assert(v12::relay_session.terminating);
   const auto drain=v12::relay_session.drain;
   assert(!v12::relay_admission_locked&&!ext_active[1]&&pa_active&&ext_active[0]);
   assert(v12::relay_admissions==admissions&&v12::rc9_server_demand_until()==lease&&!v12::fault);
   fake_now+=1000;v12::relay_poll(fake_now,false);assert(v12::relay_session.drain==drain);
   server_disconnect();server_slow=false;
   server_late_connect();assert(v12::relay_session.handle==v12::None&&v12::relay_quarantine_owned()&&!v12::fault);
   v12::relay_poll(fake_now,false);assert(!v12::relay_quarantine_owned()&&!v12::fault);
   fixture_parent_locked=false;radio_live();v12::relay_poll(fake_now,false);
   assert(ext_active[1]&&v12::relay_admission_locked&&v12::rc9_server_demand_until()==lease);
   fixture_parent_locked=true;v12::relay_poll(fake_now,false);
   assert(!ext_active[1]&&!v12::relay_admission_locked&&v12::rc9_server_demand_until()==lease);
   fake_now=lease-1;radio_live();v12::relay_poll(fake_now,false);
   assert(v12::relay_profile==9&&pa_active&&!ext_active[1]&&v12::rc9_server_demand_until()==lease);
   fake_now=lease;radio_live();v12::relay_poll(fake_now,false);
   assert(v12::relay_profile==7&&!pa_active&&!v12::rc9_server_demand_until()&&!v12::fault);
  }else if(which=="withdraw"||which=="source-expired"){
   if(which=="withdraw")rc9_radio.time_valid=false;
   else {fake_now=rc9_radio.until_us;rc9_radio.sampled_us=fake_now;}
   v12::relay_poll(fake_now,false);
   assert(v12::rc9::decode_time(p,pa_wire.data(),190,v12::provision.group)&&!p.valid);
   assert(!v12::rc9_server_serving(fake_now)&&!ext_active[1]&&
     v12::rc9_server_demand_until()==lease&&v12::rc9_server_selected(fake_now));
  }else if(which=="lease"){
   const auto before=v12::rc9_publication;fake_now+=300000;radio_live();rc9_radio.until_us=physical_until;
   v12::relay_poll(fake_now,false);assert(v12::rc9_publication>before&&v12::rc9_server_demand_until()==lease);
   v12::rc9_publish_status(fake_now);fake_now+=300000;auto s=v12::r2_rc9_status();
   assert(!s.valid&&!s.serving&&s.clients==1&&s.client_until_us==lease);
   fake_now=lease;assert(!v12::rc9_server_clients(fake_now));s=v12::r2_rc9_status();assert(!s.client_until_us&&!s.clients);
  }else if(which=="rebind"){
   assert(v12::r2_rc_parent(0));step(8);
   assert(v12::parent_diagnostic.state==3&&!v12::fault&&v12::rc9_server_demand_until()==lease);
   radio_live();v12::relay_poll(fake_now,false);
   assert(!v12::relay_stopping&&pa_active&&ext_active[0]&&ext_active[1]&&v12::rc9_server_clients(fake_now)==1);
   server_connect();(void)read9();server_disconnect();
  }else if(which=="off"){
   rc9_radio.mode=2;v12::relay_poll(fake_now,false);assert(v12::relay_profile==7&&!pa_active&&!ext_active[0]&&!v12::rc9_server_demand_until());
  }else assert(false);
 }
 v12::r2_stop();step(8);assert(!v12::relay_owned()&&!pa_active&&!ext_active[0]&&!ext_active[1]&&v12::r2_status().off_ack);
 puts("PASS actual native RC9 source/class/lease/withdrawal/original drain");
}
