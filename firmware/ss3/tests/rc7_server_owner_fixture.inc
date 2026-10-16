bool server_connected=false,server_slow=false,pa_active=false,ext_active[2]{};ble_addr_t child_address{0,{1,2,3,4,5,6}};
int server_find_rc=0,server_terminate_rc=0;uint8_t server_role=BLE_GAP_ROLE_SLAVE;
unsigned server_terminate_calls=0;
std::vector<uint8_t> pa_wire;bool estimate_queued=false;uint16_t estimate_event=0;uint8_t estimate_channel=12;
unsigned queue_advance_us=0;bool queue_wrong_publication=false,queue_busy=false;
namespace v12::periodic_tx {
bool tx_installed=false;std::vector<uint8_t> prepared;
bool start(const uint8_t*,unsigned){return false;}
bool stop(){assert(!pa_active&&!ext_active[0]);tx_installed=false;return true;}
bool pop(Record&){return false;}Stats status(){Stats s;s.installed=s.armed=tx_installed;return s;}
bool payload_verified(const Record&){return false;}bool estimate_owner_active(){return tx_installed;}
EstimateQueue estimate_queue(){return {2,1};}
bool start_estimated(const uint8_t* p,unsigned n){assert(n==190&&!tx_installed);prepared.assign(p,p+n);tx_installed=true;return true;}
bool prepare_estimated_payload(const uint8_t* p,unsigned n){assert(n==190&&tx_installed);if(queue_busy)return false;prepared.assign(p,p+n);return true;}
bool pop_estimated(EstimateRecord& r){if(!estimate_queued)return false;estimate_queued=false;
 r={};r.estimated=true;r.tick20=fake_now*20;r.channel=estimate_channel;r.observed.selected.event=++estimate_event;
 std::memcpy(r.observed.selected.data,pa_wire.data(),190);return true;}
}
int ble_gap_ext_adv_active(unsigned i){return ext_active[i];}
int ble_gap_ext_adv_stop(unsigned i){ext_active[i]=false;return 0;}
int ble_gap_ext_adv_remove(unsigned i){assert(!ext_active[i]);return 0;}
int ble_gap_ext_adv_configure(unsigned,const ble_gap_ext_adv_params*,int8_t*,int(*)(ble_gap_event*,void*),void*){return 0;}
int ble_gap_ext_adv_set_data(unsigned,os_mbuf* b){os_mbuf_free_chain(b);return 0;}
int ble_gap_ext_adv_start(unsigned i,int,unsigned){ext_active[i]=true;return 0;}
int ble_gap_periodic_adv_configure(unsigned i,const ble_gap_periodic_adv_params* p){assert(i==0&&p->itvl_min==50&&p->itvl_max==50);return 0;}
int ble_gap_periodic_adv_set_data(unsigned i,os_mbuf* b){assert(i==0&&b->bytes.size()==190);
 if(v12::periodic_tx::tx_installed)assert(b->bytes==v12::periodic_tx::prepared);pa_wire=b->bytes;os_mbuf_free_chain(b);
 if(pa_active){fake_now+=queue_advance_us;if(queue_wrong_publication)++v12::relay_map.publication;}return 0;}
int ble_gap_periodic_adv_start(unsigned i){assert(i==0);pa_active=true;return 0;}
int ble_gap_periodic_adv_stop(unsigned i){assert(i==0);pa_active=false;return 0;}
void server_connect(){assert(ext_active[1]&&v12::relay_acl_available());server_connected=true;ext_active[1]=false;
 ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=33;v12::relay_gap(&e,nullptr);assert(v12::relay_session.handle==33);}
rc7_control server_read(){os_mbuf b;ble_gatt_access_ctxt c{};c.op=BLE_GATT_ACCESS_OP_READ_CHR;c.om=&b;
 assert(!v12::relay_access(33,0,&c,nullptr));assert(b.bytes.size()==212);rc7_control out{};
 assert(!rc7_control_decode(&out,b.bytes.data(),b.bytes.size(),&v12::provision.group));return out;}
void server_request(const rc7_control& c,bool bad=false){os_mbuf b;b.bytes.resize(212);assert(!rc7_control_encode(b.bytes.data(),&c,&v12::provision.group));
 if(bad)b.bytes[211]^=1;ble_gatt_access_ctxt ctx{};ctx.om=&b;assert(!v12::relay_access(33,0,&ctx,nullptr));v12::relay_poll(fake_now,false);}
void server_unadmitted(){
 const auto admissions=v12::relay_admissions,generation=v12::relay_generation;const auto challenge=v12::relay_challenge;
 assert(v12::relay_session.handle==UINT16_MAX);
 for(const auto op:{BLE_GATT_ACCESS_OP_READ_CHR,BLE_GATT_ACCESS_OP_WRITE_CHR}){
   os_mbuf b;if(op==BLE_GATT_ACCESS_OP_WRITE_CHR)b.bytes.resize(212);
   ble_gatt_access_ctxt c{};c.op=op;c.om=&b;
   assert(v12::relay_access(33,0,&c,nullptr)==BLE_ATT_ERR_UNLIKELY);
   assert(b.bytes.size()==(op==BLE_GATT_ACCESS_OP_WRITE_CHR?212u:0u));
 }
 assert(v12::relay_admissions==admissions&&v12::relay_challenge==challenge&&v12::relay_generation==generation);
}
void server_late_connect(){server_connected=true;ext_active[1]=false;
 ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=33;v12::relay_gap(&e,nullptr);}
void server_disconnect(){ble_gap_event e{};e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=33;
 e.disconnect.conn.role=server_role;e.disconnect.conn.peer_id_addr=child_address;server_connected=false;v12::relay_gap(&e,nullptr);}
int main(int argc,char** argv){assert(argc==2);const std::string which=argv[1];
 const uint8_t mac[]={0x44,0xb1,0x76,0xae,0x17,0x78};assert(v12::r2_init(44,mac));assert(v12::r2_host_init());assert(v12::r2_host_sync());
 v12::mode=v12::R2Mode::Client;v12::relay_poll(fake_now,false);assert(ext_active[1]&&!pa_active);
 if(which=="capture-budget"){
   const auto initial=v12::diagnostic_records.size();assert(initial==1&&v12::diagnostic_records.back().length==173&&v12::diagnostic_records.back().bytes[0]==4);
   for(unsigned i=0;i<70;++i){fake_now+=1000000;v12::relay_poll(fake_now,false);}
   assert(v12::diagnostic_records.size()==60&&!v12::relay_trace_remaining);
   v12::fixture_log_status.capture_active=true;v12::fixture_log_status.capture_id=1;
   fake_now+=1000000;v12::relay_poll(fake_now,false);assert(v12::diagnostic_records.size()==61&&v12::relay_trace_remaining==59);
   for(unsigned i=0;i<70;++i){fake_now+=1000000;v12::relay_poll(fake_now,false);}
   assert(v12::diagnostic_records.size()==120&&!v12::relay_trace_remaining);
   v12::fixture_log_status.capture_active=false;fake_now+=1000000;v12::relay_poll(fake_now,false);assert(v12::diagnostic_records.size()==120);
   v12::fixture_log_status.capture_active=true;v12::fixture_log_status.capture_id=2;
   fake_now+=1000000;v12::relay_poll(fake_now,false);assert(v12::diagnostic_records.size()==121&&v12::relay_trace_remaining==59);
   v12::r2_stop();step(6);assert(v12::r2_status().off_ack);puts("PASS bounded relay diagnostics reset only for a new finite capture");return 0;
 }
 if(which=="late-connect"||which=="late-connect-past-deadline"){
   v12::r2_stop();step(6);assert(!v12::relay_owned());const auto deadline=v12::client.stop_deadline_us;
   if(which=="late-connect-past-deadline")fake_now=deadline+1;
   server_connected=true;ble_gap_event e{};e.type=BLE_GAP_EVENT_CONNECT;e.connect.conn_handle=33;
   v12::relay_gap(&e,nullptr);assert(v12::fault&&v12::relay_quarantine_owned());
   v12::r2_host_poll();assert(!v12::relay_quarantine_owned()&&!server_connected);
   assert(v12::client.stop_deadline_us==deadline&&!v12::r2_status().off_ack);
   puts("PASS late incoming ACL tracked/drained with original deadline and sticky fault");return 0;
 }
 if(which=="pending"){
   server_connect();auto cap=server_read();assert(rc7_path_empty(&cap.path)&&!cap.publishing);
   v12::Rc7Client child;assert(child.make_request(cap,300,400,9,fake_now,false));server_request(child.request);
   auto response=server_read();assert(!response.ready&&!response.source_expires_ns&&v12::relay_admissions==1);
 }else{
   fake_now=1000010;rc7_path root{};root.source_epoch=9;root.node[0]={11,22,1,1,0,0};
   rc5_origin origin={11,22,9,1000000000,2650000000,1700000000000000000,1000000000,0,0};
   if(which=="queue-envelope-expiry")origin.expires=1100000000;
   v12::G1ClockPrediction map{fake_now*20,1000010000,0,0,origin.utc_offset};
   const uint32_t inherited=which=="queue-inherited-expiry"?100000:0;
   assert(v12::relay_map.input(root,origin,nullptr,map,20000000,1,fake_now*20,inherited,v12::provision.identity,44,v12::provision.group));
   v12::relay_data_dirty=v12::relay_cap_dirty=true;v12::relay_poll(fake_now,false);fake_now+=16000;v12::relay_poll(fake_now,false);
   assert(pa_active&&ext_active[0]&&v12::periodic_tx::tx_installed);estimate_queued=true;v12::relay_poll(fake_now,false);
   assert(v12::relay_map.ready(fake_now*20)&&v12::relay_selected==1);
   if(which.rfind("overlap-",0)==0){
     // An active upstream admission is followed by a delayed successful slave
     // ACL after CAP stop. The existing periodic train must remain owned.
     assert(v12::client.pending&&v12::client.start(fake_now));
     v12::relay_pause_cap();assert(!ext_active[1]);v12::peer.connecting=true;
     v12::peer.identity=55;v12::peer.incarnation=1;v12::peer.attempt_until=fake_now+2000000;
     const auto errors=v12::api_errors;const auto admissions=v12::relay_admissions;
     if(which=="overlap-alias"){v12::peer.connecting=false;v12::peer.handle=33;}
     if(which=="overlap-invalid-descriptor")server_find_rc=BLE_HS_ENOTCONN;
     if(which=="overlap-wrong-role")server_role=BLE_GAP_ROLE_MASTER;
     server_late_connect();const auto deadline=v12::relay_quarantine_until[0];
     assert(v12::relay_quarantined[0]==33&&deadline==fake_now+2000000);
     assert(!v12::relay_acl_available());server_unadmitted();
     if(which=="overlap-alias"||which=="overlap-invalid-descriptor"||which=="overlap-wrong-role"){
       assert(v12::fault&&v12::api_errors==errors+1&&v12::first_fault_site==2);
       assert(!v12::relay_admission_overlaps&&!v12::relay_admissions);
       puts("PASS malformed or aliased incoming owner remains a hard fault with no admission");return 0;
     }
     assert(!v12::fault&&v12::api_errors==errors&&v12::relay_admission_overlaps==1&&
       v12::relay_refused==1&&v12::relay_admissions==admissions);
     if(which=="overlap-fast"||which=="overlap-enotconn"){
       if(which=="overlap-enotconn")server_terminate_rc=BLE_HS_ENOTCONN;
       v12::relay_poll(fake_now,true);
       assert(!v12::fault&&v12::api_errors==errors&&!v12::relay_quarantine_owned()&&!server_connected);
       assert(server_terminate_calls==1&&!ext_active[1]&&pa_active&&ext_active[0]);
       assert(v12::relay_quarantine_until[0]==0&&v12::relay_acl_available());
       v12::peer={};v12::relay_poll(fake_now,false);assert(ext_active[1]);
     }else{
       server_slow=true;v12::relay_poll(fake_now,true);
       assert(server_terminate_calls==1&&v12::relay_quarantine_sent[0]&&v12::relay_quarantine_owned());
       // Upstream completion alone cannot restart CAP or another outgoing ACL.
       v12::peer={};v12::relay_poll(fake_now,false);assert(!ext_active[1]);
       const auto calls=connected;v12::Discovery d{};d.address=v12::canonical(55);d.at=fake_now;
       v12::connect(d,55,fake_now);assert(connected==calls&&!v12::peer.connecting&&!ext_active[1]);
       server_unadmitted();assert(v12::relay_quarantine_until[0]==deadline);
       if(which=="overlap-duplicate"){
         fake_now+=1000;server_late_connect();
         assert(v12::fault&&v12::api_errors==errors+1&&v12::relay_admission_overlaps==1);
         assert(v12::relay_quarantine_until[0]==deadline&&server_terminate_calls==1);
         server_unadmitted();puts("PASS duplicate CONNECT stays hard fault without deadline extension or GATT authority");return 0;
       }
       fake_now=deadline-1;v12::relay_poll(fake_now,false);
       assert(!v12::fault&&v12::api_errors==errors&&v12::relay_quarantine_owned()&&
         v12::relay_quarantine_until[0]==deadline&&server_terminate_calls==1&&!ext_active[1]);
       if(which=="overlap-deadline"||which=="overlap-late-disconnect"){
         fake_now=deadline;
         if(which=="overlap-late-disconnect")server_disconnect();else v12::relay_poll(fake_now,false);
         assert(v12::fault&&v12::api_errors==errors+1&&v12::relay_quarantine_timeouts==1);
         assert(v12::first_error_api==v12::R2Api::Disconnect&&v12::first_error_rc==BLE_HS_ETIMEOUT);
         if(which=="overlap-deadline"){
           assert(v12::relay_quarantine_owned()&&v12::relay_quarantine_until[0]==deadline);
           v12::relay_poll(fake_now,false);assert(v12::relay_quarantine_timeouts==1);
           server_disconnect();assert(!v12::relay_quarantine_owned()&&v12::relay_quarantine_timeouts==1);
         }
         server_unadmitted();puts("PASS original incoming ACL 2s deadline is strict with one timeout and retained fault");return 0;
       }
       assert(which=="overlap-slow");server_disconnect();v12::relay_poll(fake_now,false);
       assert(!v12::relay_quarantine_owned()&&v12::relay_acl_available()&&ext_active[1]);
     }
     assert(!v12::fault&&v12::api_errors==errors&&pa_active&&ext_active[0]&&v12::periodic_tx::tx_installed);
     assert(v12::relay_admissions==admissions&&!v12::relay_quarantine_timeouts);
     v12::r2_stop();step(6);assert(v12::r2_status().off_ack);
     assert(v12::client.completed_us<v12::client.stop_deadline_us);
     puts("PASS healthy upstream overlap denied authority and drained in sole worker without interrupting PA");return 0;
   }
   if(which=="queue-trace"){
     const auto attempts=v12::relay_queue_diagnostic.attempts,queued=v12::relay_queue_diagnostic.queued;
     queue_busy=true;v12::relay_data_dirty=true;fake_now+=2000;v12::relay_poll(fake_now,false);
     fake_now+=100000;v12::relay_poll(fake_now,false);
     const auto& q=v12::relay_queue_diagnostic;
     assert(q.busy==2&&q.max_busy_us==100000&&q.prepared==2&&q.current==1&&q.queued==queued);
     queue_busy=false;queue_advance_us=10000;v12::relay_poll(fake_now,false);
     assert(q.attempts==attempts+3&&q.queued==queued+1&&q.last_us==10000&&q.max_us==10000&&!q.busy_since);
     fake_now+=1000000;v12::relay_poll(fake_now,false);const auto& row=v12::diagnostic_records.back();
     assert(row.length==173&&row.bytes[0]==4);
     assert(r2_group_get(row.bytes+109+7*4,4)==100000&&r2_group_get(row.bytes+109+8*4,4)==2);
     assert(!v12::relay_map.ready(fake_now*20)&&pa_active);v12::r2_stop();step(6);assert(v12::r2_status().off_ack);
     puts("PASS bounded worker queue trace distinguishes BUSY duration from successful public API latency without extending source");return 0;
   }
   if(which=="context-withdraw"){
     v12::relay_fence();v12::relay_poll(fake_now,false);
     assert(!pa_active&&!ext_active[0]&&!v12::periodic_tx::tx_installed&&!v12::relay_map.valid);
     v12::r2_stop();step(6);assert(v12::r2_status().off_ack);
     puts("PASS full scope withdrawal still stops PA and restores TX owner");return 0;
   }
   if(which=="queue-local-expiry"||which=="queue-inherited-expiry"||which=="queue-envelope-expiry"||which=="queue-invariant"){
     const auto train=v12::relay_map.path.node[1].train;const auto publication=v12::relay_map.publication;
     if(which=="queue-local-expiry")fake_now=1499999;
     else if(which=="queue-inherited-expiry")fake_now=1399999;
     else if(which=="queue-envelope-expiry")fake_now=1099999;
     queue_wrong_publication=which=="queue-invariant";queue_advance_us=2;
     assert(v12::relay_map.live(fake_now*20));v12::relay_data_dirty=true;v12::relay_poll(fake_now,false);
     assert(v12::api_errors==0&&v12::fault==queue_wrong_publication);
     if(queue_wrong_publication)assert(!pa_active&&!ext_active[0]&&!v12::periodic_tx::tx_installed&&!v12::relay_map.valid);
     else{
       assert(pa_active&&ext_active[0]&&v12::periodic_tx::tx_installed&&v12::relay_map.valid);
       assert(!v12::relay_map.live(fake_now*20)&&!v12::relay_map.ready(fake_now*20)&&!v12::relay_map.sent[0].publication);
       assert(v12::relay_map.publication==publication+1); // Burn the already queued bytes.
       v12::relay_poll(fake_now,false);rc7_packet standby{};
       assert(!rc7_decode(&standby,pa_wire.data(),190,&v12::relay_map.path,&v12::provision.group));
       assert(!standby.count&&standby.publication==publication+2&&standby.train==train);
       map.tick=fake_now*20;map.epoch=fake_now*1000;origin.expires=uint64_t(map.epoch)+1000000000;
       assert(v12::relay_map.input(root,origin,nullptr,map,fake_now*20,1,fake_now*20,0,v12::provision.identity,44,v12::provision.group));
       v12::relay_data_dirty=true;v12::relay_poll(fake_now,false);assert(pa_active&&v12::relay_map.path.node[1].train==train);
       assert(!v12::relay_map.ready(fake_now*20));estimate_queued=true;v12::relay_poll(fake_now,false);
       assert(v12::relay_map.ready(fake_now*20)&&v12::relay_map.path.node[1].train==train);
     }
     v12::r2_stop();step(6);assert(!v12::relay_owned()&&v12::client.completed_us<v12::client.stop_deadline_us);
     puts("PASS source expiry withholds time and burns queued publication; fresh input resumes same PA train; invariant failure stays hard fault");return 0;
   }
   server_connect();auto cap=server_read();assert(cap.publishing&&cap.path.depth==1);
   v12::Rc7Client child;assert(child.make_request(cap,300,400,9,fake_now,false));
   if(which=="small-mtu"){small_mtu=true;os_mbuf b;ble_gatt_access_ctxt c{};c.op=0;c.om=&b;assert(v12::relay_access(33,0,&c,nullptr));small_mtu=false;}
   if(which=="offset"){os_mbuf b;ble_gatt_access_ctxt c{};c.op=0;c.om=&b;c.offset=1;assert(v12::relay_access(33,0,&c,nullptr));}
   server_request(child.request,which=="bad-tag");
   if(which=="bad-tag"){assert(v12::relay_refused==1&&v12::relay_session.handle==UINT16_MAX&&!v12::fault);}
   else {const auto response=server_read();assert(response.ready&&response.path.depth==1&&response.publication_floor);
     assert(child.bind(response,fake_now,fake_now*20));assert(v12::relay_admissions==1);
     if(which=="upstream-yield"){v12::relay_poll(fake_now,true);assert(!ext_active[1]&&!server_connected&&pa_active&&ext_active[0]);}
     if(which=="expired-source"){
       const auto train=v12::relay_map.path.node[1].train;fake_now+=500000;v12::relay_poll(fake_now,false);
       assert(pa_active&&ext_active[0]&&v12::relay_map.valid&&!v12::relay_map.ready(fake_now*20));
       rc7_packet standby{};assert(!rc7_decode(&standby,pa_wire.data(),190,&v12::relay_map.path,&v12::provision.group));
       assert(!standby.count&&standby.train==train);
     }
     if(which=="late-drain"){server_slow=true;v12::r2_stop();step();const auto deadline=v12::close_until;fake_now=deadline+1;v12::r2_host_poll();assert(v12::fault&&!v12::snap.off_ack);puts("PASS native server late drain retained fault");return 0;}
   }
 }
 v12::r2_stop();step(6);assert(!v12::relay_owned()&&!pa_active&&!ext_active[0]&&!ext_active[1]&&v12::r2_status().off_ack);
 assert(v12::client.completed_us<v12::client.stop_deadline_us);
 puts("PASS native RC7 server callback/worker owner fixture");
}
