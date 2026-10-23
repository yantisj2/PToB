// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
w12_lora_view rc9_radio{};bool snapshot_race=false;
void w12_lora_snapshot(w12_lora_view& out){
 if(snapshot_race){++fake_now;rc9_radio.sampled_us=fake_now;}out=rc9_radio;
}
void radio_live(){
 auto& l=rc9_radio;l={};l.requested=l.active=true;l.state=2;l.sampled_us=fake_now;
 l.utc_us=1700000000000000ULL+fake_now;l.edge_us=fake_now-100;l.until_us=l.edge_us+12000000;
 l.event_ns=(l.utc_us-100)*1000;l.created_ns=l.event_ns-1000000;l.expires_ns=l.event_ns+1000000000;
 l.emitter=55;l.root=66;l.source_epoch=1;l.generation=1;l.corrected_sequence=1;l.context[0]=1;l.kind=2;
 l.link_rms_ns=123;l.link_samples=8;
}
void* rc9_arg(){return reinterpret_cast<void*>(uintptr_t(9));}
v12::rc9::Control read9(){os_mbuf b;ble_gatt_access_ctxt c{};c.op=BLE_GATT_ACCESS_OP_READ_CHR;c.om=&b;
 assert(!v12::relay_access(33,0,&c,rc9_arg()));v12::rc9::Control cap;
 assert(v12::rc9::decode_control(cap,b.bytes.data(),b.bytes.size(),v12::provision.group));return cap;}
void write9(const v12::rc9::Control& q){os_mbuf b;b.bytes.resize(212);assert(v12::rc9::encode_control(b.bytes.data(),q,v12::provision.group));
 ble_gatt_access_ctxt c{};c.om=&b;assert(!v12::relay_access(33,0,&c,rc9_arg()));v12::relay_poll(fake_now,false);}
int main(int argc,char** argv){assert(argc==2);const std::string which=argv[1];
 if(which=="client"){client_case();return 0;}
 const auto* mac=v12::native_device_binding()->factory;assert(v12::r2_init(44,mac)&&v12::r2_host_init()&&v12::r2_host_sync());
 step(8);assert(v12::client.running);radio_live();v12::relay_poll(fake_now,false);
 std::fprintf(stderr,"state fault=%u profile=%u pa=%u adv=%u/%u selected=%u configured=%u stopping=%u api=%u rc=%d\n",v12::fault,v12::relay_profile,pa_active,ext_active[0],ext_active[1],v12::rc9_server_selected(fake_now),v12::relay_configured,v12::relay_stopping,unsigned(v12::first_error_api),v12::first_error_rc);
 assert(!v12::fault&&v12::relay_profile==9&&pa_active&&ext_active[0]&&ext_active[1]&&!v12::periodic_tx::tx_installed);
 assert(!v12::relay_flags(fake_now*20));v12::rc9::Time p;assert(v12::rc9::decode_time(p,pa_wire.data(),190,v12::provision.group)&&p.valid);
 assert(p.link_rms_ns==123&&p.root_boot==0&&p.emitter_boot==0&&p.edge_us==rc9_radio.edge_us&&p.until_us==rc9_radio.until_us);
 const auto physical_until=p.until_us;
 if(which=="race"){
  snapshot_race=true;fake_now+=300000;v12::relay_poll(fake_now,false);
  assert(v12::rc9::decode_time(p,pa_wire.data(),190,v12::provision.group)&&p.valid&&p.sampled_us>=rc9_radio.sampled_us);
 }else{
  server_connect();os_mbuf old;ble_gatt_access_ctxt wrong{};wrong.op=BLE_GATT_ACCESS_OP_READ_CHR;wrong.om=&old;
  assert(v12::relay_access(33,0,&wrong,nullptr)==BLE_ATT_ERR_UNLIKELY&&old.bytes.empty());
  const auto cap=read9();v12::rc9::Client child;assert(child.make_request(cap,77,88,99,fake_now));write9(child.request);
  const auto reply=read9();assert(child.bind(reply,fake_now));server_disconnect();v12::relay_poll(fake_now,false);
  const auto lease=v12::rc9_server_demand_until();assert(lease==fake_now+v12::rc9::LeaseUs);
  assert(v12::rc9_server_clients(fake_now)==1&&v12::rc9_server_selected(fake_now));
  if(which=="withdraw"){
   rc9_radio.state=1;v12::relay_poll(fake_now,false);
   assert(v12::rc9::decode_time(p,pa_wire.data(),190,v12::provision.group)&&!p.valid);
   assert(v12::rc9_server_demand_until()==lease&&v12::rc9_server_selected(fake_now));
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
