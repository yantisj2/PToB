// Included in the sole G1 NimBLE worker namespace. Fixed callbacks copy only;
// HMAC, replay, source mapping and public controller calls run in relay_poll.
#ifdef V12_R2_RC7_TX_ESTIMATE_LAB
static_assert(V12_R2_G1_LISTENING_PERCENT==100,"RC7 server requires BLE100");
Rc7RelayEstimate relay_map;
struct RelayChild {uint64_t id=0;rc5_demand demand{};};
struct RelaySession {
  uint64_t until=0,challenge=0,drain=0;uint32_t generation=0;
  uint16_t handle=None;bool replied=false,terminating=false;
  ble_addr_t address{};rc7_path issued_path{};uint8_t reply[212]{};
};
struct RelayWrite {uint64_t at=0;uint32_t generation=0;uint16_t handle=None;uint8_t wire[212]{};};
RelayChild relay_children[8]{};RelaySession relay_session;
Ring<RelayWrite,4> relay_writes;
uint64_t relay_challenge=0,relay_cap_at=0,relay_start20=0;
uint64_t relay_trace_at=0;uint8_t relay_trace_remaining=60,relay_last_channel=255;
uint32_t relay_generation=0,relay_admissions=0,relay_refused=0,relay_selected=0,relay_estimate_refused=0;
uint8_t relay_selection_reason=0;
struct RelayEstimateDiagnostic {
  uint64_t tick=0,before=0,anchor_before=0,anchor_after=0,map_tick=0,original=0;
  uint32_t cookie=0,half_slots=0,half_us=0;
  uint8_t stage=0,selected_reason=0;
};
RelayEstimateDiagnostic relay_estimate_diagnostic;
struct RelayQueueDiagnostic {
  uint32_t inputs=0,input_refused=0,attempts=0,busy=0,queued=0;
  uint32_t last_us=0,max_us=0,max_busy_us=0,prepared=0,current=0;
  uint32_t packet_refused=0;
  uint64_t busy_since=0;
};
RelayQueueDiagnostic relay_queue_diagnostic;
uint64_t relay_trace_capture=0;
uint8_t relay_cap[212]{};rc7_path relay_cap_path{};
bool relay_configured=false,relay_pa=false,relay_discovery=false,relay_advertising=false,relay_cap_dirty=true,relay_data_dirty=false;
bool relay_stopping=false;uint64_t relay_stop_deadline=0;
bool relay_rescan_pending=false;
uint16_t relay_quarantined[4]={None,None,None,None};bool relay_quarantine_sent[4]{};
uint64_t relay_quarantine_until[4]{};
bool relay_quarantine_timed_out[4]{};
uint32_t relay_admission_overlaps=0,relay_quarantine_timeouts=0;
bool relay_api(int rc,R2Api site){if(!rc)return true;error(rc,site);return false;}
uint32_t relay_flags(uint64_t now20){return 1u|2u|(uint32_t(relay_advertising)<<2)|(uint32_t(relay_pa)<<3)|
  (uint32_t(relay_map.ready(now20)&&relay_pa)<<4)|(uint32_t(relay_session.handle!=None)<<5)|
  (uint32_t(relay_selected!=0)<<6)|(uint32_t(relay_estimate_refused!=0)<<7);}
bool relay_quarantine_owned(){for(const auto h:relay_quarantined)if(h!=None)return true;return false;}
bool relay_owned(){const auto tx=periodic_tx::status();return relay_configured||relay_pa||relay_discovery||relay_advertising||relay_session.handle!=None||relay_quarantine_owned()||tx.installed||tx.armed;}
bool relay_acl_available(){return relay_session.handle==None&&!relay_quarantine_owned();}
void relay_quarantine_poll(uint64_t now){
  for(unsigned i=0;i<4;i++)if(relay_quarantined[i]!=None){
    if(now>=relay_quarantine_until[i]&&!relay_quarantine_timed_out[i]){
      relay_quarantine_timed_out[i]=true;inc(relay_quarantine_timeouts);
      error(BLE_HS_ETIMEOUT,R2Api::Disconnect);
    }
    if(relay_quarantine_sent[i])continue;
    const auto handle=relay_quarantined[i];const int rc=ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
    if(relay_quarantined[i]!=handle)continue;
    if(rc==BLE_HS_ENOTCONN){relay_quarantined[i]=None;relay_quarantine_until[i]=0;}
    else if(!rc||rc==BLE_HS_EALREADY)relay_quarantine_sent[i]=true;
    else relay_api(rc,R2Api::Disconnect);
  }
}
void relay_fence(){relay_map.fence();relay_cap_dirty=true;relay_data_dirty=false;relay_start20=0;relay_queue_diagnostic.busy_since=0;}
void relay_pause_cap(){if(!relay_advertising)return;const int rc=ble_gap_ext_adv_stop(1);
  if(!rc||rc==BLE_HS_EALREADY)relay_advertising=false;else relay_api(rc,R2Api::AdvStop);}
void relay_stop_pa(){
  if(relay_pa){const int rc=ble_gap_periodic_adv_stop(0);if(!rc||rc==BLE_HS_EALREADY)relay_pa=false;else relay_api(rc,R2Api::AdvStop);}
  if(relay_discovery){const int rc=ble_gap_ext_adv_stop(0);if(!rc||rc==BLE_HS_EALREADY)relay_discovery=false;else relay_api(rc,R2Api::AdvStop);}
  if(!relay_pa&&!relay_discovery&&!periodic_tx::stop())fault=true;
}
void relay_terminate(uint64_t now){if(relay_session.handle==None||relay_session.terminating)return;
  const auto handle=relay_session.handle;const auto generation=relay_session.generation;
  const int rc=ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
  if(relay_session.handle!=handle||relay_session.generation!=generation)return;
  if(!rc||rc==BLE_HS_EALREADY||rc==BLE_HS_ENOTCONN){relay_session.terminating=true;relay_session.drain=now+2000000;
    if(relay_stopping&&relay_session.drain>relay_stop_deadline)relay_session.drain=relay_stop_deadline;
    if(rc==BLE_HS_ENOTCONN)relay_session={};
  }else relay_api(rc,R2Api::Disconnect);
}
void relay_close(uint64_t now){
  if(!relay_stopping){relay_stopping=true;relay_stop_deadline=closing?close_until:client.stop_deadline_us?client.stop_deadline_us:now+2000000;relay_fence();}
  relay_pause_cap();relay_stop_pa();relay_terminate(now);
  // Retire frozen-bank references before a later explicit activation. No
  // stopped-scope estimate may survive as publication or block a fresh start.
  periodic_tx::EstimateRecord discarded{};
  for(unsigned i=0;i<periodic_tx::Capacity&&periodic_tx::pop_estimated(discarded);i++){}
  relay_quarantine_poll(now);
  RelayWrite discard{};while(relay_writes.pop(discard)){}
  if(relay_session.handle!=None&&now>=relay_session.drain&&relay_session.terminating)fault=true;
  if(!relay_pa&&!relay_discovery&&!relay_advertising&&relay_session.handle==None&&relay_configured){
    const int a=ble_gap_ext_adv_remove(0),b=ble_gap_ext_adv_remove(1);
    if((!a||a==BLE_HS_ENOENT)&&(!b||b==BLE_HS_ENOENT))relay_configured=false;else fault=true;
  }
  if(relay_owned()&&now>=relay_stop_deadline)fault=true;
}
int relay_gap(ble_gap_event* e,void*){
  const auto now=micros();
  if(e->type==BLE_GAP_EVENT_CONNECT){relay_advertising=false;
    if(e->connect.status){relay_cap_dirty=true;return 0;}
    ble_gap_conn_desc d{};
    const bool descriptor=!ble_gap_conn_find(e->connect.conn_handle,&d)&&d.role==BLE_GAP_ROLE_SLAVE;
    const bool owned=descriptor&&
      relay_acl_available()&&peer.handle==None&&!peer.connecting&&full_listening()&&!closing&&!fault&&!relay_stopping&&
      relay_generation!=UINT32_MAX&&relay_challenge<UINT64_MAX-1;
    if(!owned){
      // A delayed CONNECT is still a real controller owner. Preserve it for
      // worker teardown even after an earlier fault closed normal service.
      unsigned at=4;for(unsigned i=0;i<4;i++)if(relay_quarantined[i]==e->connect.conn_handle){at=i;break;}
      if(at==4)for(unsigned i=0;i<4;i++)if(relay_quarantined[i]==None){at=i;break;}
      // Stopping CAP advertising can yield while an already established
      // incoming ACL's CONNECT is still queued. Refuse all access, account
      // this real owner and drain it in the worker. A healthy upstream
      // admission overlap is recoverable; malformed/aliased/late owners and
      // a missed original drain remain hard faults.
      const bool overlap=descriptor&&at<4&&relay_quarantined[at]==None&&
        e->connect.conn_handle!=peer.handle&&e->connect.conn_handle!=relay_session.handle&&
        relay_session.handle==None&&(peer.connecting||peer.handle!=None)&&
        full_listening()&&client.running&&!client.stopping&&!closing&&!fault&&!relay_stopping&&
        !stop_pending&&relay_generation!=UINT32_MAX&&relay_challenge<UINT64_MAX-1&&now<=UINT64_MAX-2000000;
      if(at<4&&relay_quarantined[at]==None){
        relay_quarantined[at]=e->connect.conn_handle;relay_quarantine_sent[at]=relay_quarantine_timed_out[at]=false;
        relay_quarantine_until[at]=now<=UINT64_MAX-2000000?now+2000000:now;
        if(closing&&close_until<relay_quarantine_until[at])relay_quarantine_until[at]=close_until;
        if(relay_stopping&&relay_stop_deadline<relay_quarantine_until[at])relay_quarantine_until[at]=relay_stop_deadline;
      }
      if(overlap){inc(relay_admission_overlaps);inc(relay_refused);note(BLE_HS_EBUSY,R2Api::Connect);}
      else error(BLE_HS_EBUSY,R2Api::Connect,2);
      return 0;
    }
    relay_session={};relay_session.handle=e->connect.conn_handle;relay_session.generation=++relay_generation;
    relay_session.until=now+2000000;relay_session.address=d.peer_id_addr;relay_session.challenge=relay_challenge++;
    relay_session.issued_path=relay_cap_path;std::memcpy(relay_session.reply,relay_cap,212);relay_cap_dirty=true;
  }else if(e->type==BLE_GAP_EVENT_DISCONNECT){const auto& d=e->disconnect.conn;
    for(unsigned i=0;i<4;i++)if(d.conn_handle==relay_quarantined[i]){
      if(now>=relay_quarantine_until[i]&&!relay_quarantine_timed_out[i]){inc(relay_quarantine_timeouts);error(BLE_HS_ETIMEOUT,R2Api::Disconnect);}
      relay_quarantined[i]=None;relay_quarantine_sent[i]=relay_quarantine_timed_out[i]=false;relay_quarantine_until[i]=0;
    }
    if(d.conn_handle==relay_session.handle&&d.role==BLE_GAP_ROLE_SLAVE&&same_address(d.peer_id_addr,relay_session.address)){
      if(relay_stopping&&now>=relay_stop_deadline)fault=true;
      relay_session={};relay_cap_dirty=true;
    }
  }else if(e->type==BLE_GAP_EVENT_ADV_COMPLETE){
    if(e->adv_complete.instance==1)relay_advertising=false;
    else if(e->adv_complete.instance==0){relay_discovery=false;if(!relay_stopping){fault=true;relay_fence();}}
  }
  return 0;
}
int relay_access(uint16_t h,uint16_t,ble_gatt_access_ctxt* ctx,void*){
  if(h!=relay_session.handle||relay_session.terminating||relay_stopping||closing||fault||!full_listening()||
    micros()>=relay_session.until||ble_att_mtu(h)<226)return BLE_ATT_ERR_UNLIKELY;
  if(ctx->offset)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  if(ctx->op==BLE_GATT_ACCESS_OP_READ_CHR){
    return os_mbuf_append(ctx->om,relay_session.reply,212)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
  }
  if(ctx->op!=BLE_GATT_ACCESS_OP_WRITE_CHR||OS_MBUF_PKTLEN(ctx->om)!=212||relay_session.replied)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  RelayWrite row{};row.at=micros();row.generation=relay_session.generation;row.handle=h;
  if(os_mbuf_copydata(ctx->om,0,212,row.wire))return BLE_ATT_ERR_UNLIKELY;
  return relay_writes.push(row)?0:BLE_ATT_ERR_INSUFFICIENT_RES;
}
const ble_gatt_chr_def relay_chars[]={[]{ble_gatt_chr_def c{};c.uuid=&rc_data_uuid.u;c.access_cb=relay_access;
  c.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_READ;return c;}(),{}};
void relay_input(const rc7_path& parent,const rc5_origin& origin,const uint8_t* proof,uint64_t original20,
                 uint32_t incarnation,uint64_t completion20,uint32_t inherited_age){
  if(!full_listening()||closing||fault||relay_stopping)return;
  const auto old=relay_map.path;
  if(relay_map.input(parent,origin,proof,corrected_clock.prediction(),original20,incarnation,completion20,
    inherited_age,provision.identity,boot_id,provision.group)){
    inc(relay_queue_diagnostic.inputs);
    relay_data_dirty=true;
    if(!rc7_path_same(&old,&relay_map.path)){relay_cap_dirty=true;relay_stop_pa();relay_start20=0;}
  }else inc(relay_queue_diagnostic.input_refused);
}
bool relay_cap_update(uint64_t now){
  if(relay_session.handle!=None||peer.connecting||peer.handle!=None)return false;
  relay_pause_cap();
  const auto c=relay_map.capability(provision.identity,boot_id,relay_challenge,relay_pa);
  uint8_t wire[167]={2,1,6,17,7};std::memcpy(wire+5,rc_service.value,16);wire[21]=145;wire[22]=255;
  if(rc7_control_encode(relay_cap,&c,&provision.group)||rc7_capability_encode(wire+23,&c,&provision.group)){fault=true;return false;}
  relay_cap_path=c.path;auto* mb=ble_hs_mbuf_from_flat(wire,sizeof wire);
  if(!mb){fault=true;return false;}if(!relay_api(ble_gap_ext_adv_set_data(1,mb),R2Api::AdvData))return false;
  relay_cap_at=now;relay_cap_dirty=false;return true;
}
bool relay_configure(){
  ble_gap_ext_adv_params a{};a.own_addr_type=BLE_OWN_ADDR_RANDOM;a.itvl_min=a.itvl_max=50;
  a.primary_phy=a.secondary_phy=BLE_HCI_LE_PHY_1M;a.sid=12;a.tx_power=0;
  ble_gap_periodic_adv_params p{};p.itvl_min=p.itvl_max=50;
  int8_t power=127;if(!relay_api(ble_gap_ext_adv_configure(0,&a,&power,relay_gap,nullptr),R2Api::AdvConfigure))return false;
  relay_configured=true;
  if(!relay_api(ble_gap_periodic_adv_configure(0,&p),R2Api::AdvConfigure))return false;
  a.sid=13;a.connectable=1;
  return relay_api(ble_gap_ext_adv_configure(1,&a,&power,relay_gap,nullptr),R2Api::AdvConfigure);
}
void relay_requests(uint64_t now,uint64_t now20){
  RelayWrite row{};for(unsigned n=0;n<2&&relay_writes.pop(row);n++){
    if(row.handle!=relay_session.handle||row.generation!=relay_session.generation||relay_session.replied||
      relay_session.terminating||now<row.at||now>=relay_session.until){inc(relay_refused);continue;}
    rc7_control request{};
    if(rc7_control_decode(&request,row.wire,212,&provision.group)||request.type!=RC7_REQUEST||
      request.target!=provision.identity||request.target_boot!=boot_id||request.challenge!=relay_session.challenge||
      !rc7_path_same(&request.path,&relay_session.issued_path)){inc(relay_refused);relay_terminate(now);continue;}
    unsigned at=8;for(unsigned i=0;i<8;i++)if(relay_children[i].id==request.sender){at=i;break;}
    if(at==8)for(unsigned i=0;i<8;i++)if(!relay_children[i].id||!rc5_demand_live(&relay_children[i].demand,now)){at=i;break;}
    if(at==8){inc(relay_refused);relay_terminate(now);continue;}
    auto child=relay_children[at];if(child.id!=request.sender)child={};child.id=request.sender;
    if(!rc5_demand_begin(&child.demand,boot_id,request.sender_boot,request.challenge,now)||
      !rc5_demand_accept(&child.demand,boot_id,request.sender_boot,request.challenge,request.sequence,request.lease_us,now)){
      inc(relay_refused);relay_terminate(now);continue;}
    rc7_control response{};
    if(!relay_map.response(response,request,relay_challenge,now20)){inc(relay_refused);relay_terminate(now);continue;}
    if(!relay_pa||request.operation){response.ready=false;response.path={};response.publication_floor=0;response.root_lo_ns=response.root_hi_ns=response.source_expires_ns=0;}
    uint8_t reply[212];if(rc7_control_encode(reply,&response,&provision.group)){inc(relay_refused);relay_terminate(now);continue;}
    relay_children[at]=child;std::memcpy(relay_session.reply,reply,212);relay_session.replied=true;inc(relay_admissions);
    relay_rescan_pending|=request.operation!=0;
  }
}
void relay_poll(uint64_t now,bool upstream_due){
  if(!full_listening()||closing||fault){if(relay_owned())relay_close(now);return;}
  relay_quarantine_poll(now);
  if(fault)return;
  if(relay_stopping){if(relay_owned())return;relay_stopping=false;relay_stop_deadline=0;relay_cap_dirty=true;}
  if(!relay_configured&&!relay_configure())return;
  if(relay_rescan_pending&&relay_session.handle==None){
    relay_stop_pa();relay_map.rescan();relay_start20=0;relay_data_dirty=false;relay_cap_dirty=true;relay_rescan_pending=false;
  }
  uint64_t now20=0;if(!clock_reference_read(&now20)){fault=true;return;}
  // Full scope withdrawal still destroys the old radio owner. Timing-only
  // standby keeps valid/path set and is deliberately a separate state.
  if((relay_pa||relay_discovery)&&!relay_map.valid)relay_stop_pa();
  if(relay_map.valid&&!relay_map.live(now20)&&relay_map.withdraw_time()){
    relay_data_dirty=true;relay_cap_dirty=true;
  }
  if(relay_session.handle!=None){
    if(now>=relay_session.until||upstream_due)relay_terminate(now);
    if(relay_session.terminating&&now>=relay_session.drain)fault=true;
    relay_requests(now,now20);
  }
  if(upstream_due||peer.connecting||peer.handle!=None)relay_pause_cap();
  if(synced&&!upstream_due)stop_scan();
  if(relay_acl_available()&&peer.handle==None&&!peer.connecting){
    if(relay_cap_dirty&&!relay_cap_update(now))return;
    if(!upstream_due&&!relay_advertising&&relay_api(ble_gap_ext_adv_start(1,0,0),R2Api::AdvStart))relay_advertising=true;
  }
  periodic_tx::EstimateRecord capture{};
  for(unsigned i=0;i<periodic_tx::Capacity&&periodic_tx::pop_estimated(capture);i++){
    // Public controller calls above and the pop's clock brackets may yield.
    // Validate causality against a fresh time, never the pre-call snapshot.
    if(!clock_reference_read(&now20)){fault=true;relay_fence();relay_stop_pa();return;}
    if(capture.estimated&&capture.uncertainty_ns==UINT32_MAX&&relay_map.selected(capture.observed.selected.data,
      capture.observed.selected.event,capture.channel,capture.tick20,now20,provision.group)){
      inc(relay_selected);relay_last_channel=capture.channel;relay_data_dirty=true;
    }else{inc(relay_estimate_refused);relay_selection_reason=capture.estimated?uint8_t(relay_map.selected_reason()):254;
      relay_estimate_diagnostic={capture.tick20,capture.observed.before20,capture.anchor.before,capture.anchor.after,
        relay_map.map.tick,relay_map.original20,capture.observed.kind,capture.anchor.half_slots,capture.anchor.half_us,
        capture.stage,capture.observed.selected.reason};}
  }
  if(periodic_tx::status().fault){fault=true;relay_fence();relay_stop_pa();return;}
  if(now>=relay_trace_at){
    relay_trace_at=now+1000000;const auto logging=log_status();
    if(logging.capture_active&&logging.capture_id!=relay_trace_capture){relay_trace_capture=logging.capture_id;relay_trace_remaining=60;}
    if(relay_trace_remaining){--relay_trace_remaining;Report row{};row.kind=20;row.callback_us=now;
    row.actual_length=row.length=173;row.bytes[0]=4;row.bytes[1]=relay_last_channel;
    const uint32_t values[]={relay_flags(now20),relay_map.publication,relay_admissions,relay_refused,relay_selected,relay_estimate_refused};
    for(unsigned i=0;i<6;i++)for(unsigned j=0;j<4;j++)row.bytes[2+4*i+j]=uint8_t(values[i]>>(8*j));
    row.bytes[26]=relay_selection_reason;
    const auto& d=relay_estimate_diagnostic;row.bytes[27]=d.stage;row.bytes[28]=d.selected_reason;
    const uint64_t times[]={d.tick,d.before,d.anchor_before,d.anchor_after,d.map_tick,d.original};
    for(unsigned i=0;i<6;i++)for(unsigned j=0;j<8;j++)row.bytes[29+8*i+j]=uint8_t(times[i]>>(8*j));
    const uint32_t fields[]={d.cookie,d.half_slots,d.half_us,periodic_tx::status().fault,
      corrected_clock.accepted(),corrected_clock.refused(),corrected_clock.rate_pairs(),corrected_clock.discontinuities()};
    for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++)row.bytes[77+4*i+j]=uint8_t(fields[i]>>(8*j));
    const auto& q=relay_queue_diagnostic;
    const uint32_t queues[]={q.inputs,q.input_refused,q.attempts,q.busy,q.queued,q.last_us,q.max_us,
      q.max_busy_us,q.prepared,q.current,rc_client.receiver.clock.accepted,
      rc_client.receiver.clock.rejected,rc_client.receiver.clock.evicted};
    for(unsigned i=0;i<13;i++)for(unsigned j=0;j<4;j++)row.bytes[109+4*i+j]=uint8_t(queues[i]>>(8*j));
    for(unsigned j=0;j<8;j++)row.bytes[161+j]=uint8_t(relay_map.original20>>(8*j));
    for(unsigned j=0;j<4;j++)row.bytes[169+j]=uint8_t(q.packet_refused>>(8*j));
    transport_record(row);
    }
  }
  if(!clock_reference_read(&now20)){fault=true;relay_fence();relay_stop_pa();return;}
  if(relay_map.valid&&!relay_map.live(now20)&&relay_map.withdraw_time()){
    relay_data_dirty=true;relay_cap_dirty=true;
  }
  if(!relay_pa){
    if(!relay_map.live(now20))return;
    if(!relay_start20&&!relay_map.phase.request(relay_map.path.node[relay_map.path.depth].slot,now20,boot_id,true,relay_start20))return;
    // Intent-only placement at the accepted ORIGINAL parent RX + later slot.
    // The public SDK start/scheduler latency is UNKNOWN and stays unqualified.
    if(now20<relay_start20)return;
    uint8_t packet[190];if(!relay_map.packet(packet,now20,provision.group))return;
    auto* mb=ble_hs_mbuf_from_flat(packet,190);if(!mb){fault=true;return;}
    if(!relay_api(ble_gap_periodic_adv_set_data(0,mb),R2Api::AdvData)||!periodic_tx::start_estimated(packet,190)){fault=true;return;}
    if(!relay_api(ble_gap_periodic_adv_start(0),R2Api::AdvStart))return;
    relay_pa=true;
    // Advertiser0 provides SyncInfo; advertiser1 owns transient RC admission.
    static const uint8_t marker[3]={2,1,6};auto* discovery=ble_hs_mbuf_from_flat(marker,sizeof marker);
    if(!discovery){fault=true;return;}
    if(!relay_api(ble_gap_ext_adv_set_data(0,discovery),R2Api::AdvData)||!relay_api(ble_gap_ext_adv_start(0,0,0),R2Api::AdvStart))return;
    relay_discovery=true;relay_data_dirty=false;relay_cap_dirty=true;
  }else if(relay_data_dirty){
    // Reserve a frozen expected payload before the yielding SDK data update.
    // BUSY retains the request; no unfrozen payload is queued to the controller.
    Rc7RelayEstimate::PreparedPublication prepared{};uint8_t packet[190];
    auto& q=relay_queue_diagnostic;inc(q.attempts);
    if(!relay_map.prepare_packet(packet,prepared,now20,provision.group)){inc(q.packet_refused);return;}
    if(!periodic_tx::prepare_estimated_payload(packet,190)){
      inc(q.busy);const auto busy_now=micros();if(!q.busy_since)q.busy_since=busy_now;
      if(busy_now>=q.busy_since){const auto elapsed=busy_now-q.busy_since;
        if(elapsed>q.max_busy_us){q.max_busy_us=uint32_t(std::min<uint64_t>(elapsed,UINT32_MAX));
          const auto banks=periodic_tx::estimate_queue();q.prepared=banks.prepared;q.current=banks.current;}}
      return;
    }
    q.busy_since=0;
    auto* mb=ble_hs_mbuf_from_flat(packet,190);if(!mb){fault=true;return;}
    const auto queue_begin=micros();
    if(!relay_api(ble_gap_periodic_adv_set_data(0,mb),R2Api::AdvData))return;
    const auto queue_end=micros();inc(q.queued);
    q.last_us=queue_end>=queue_begin?uint32_t(std::min<uint64_t>(queue_end-queue_begin,UINT32_MAX)):UINT32_MAX;
    q.max_us=std::max(q.max_us,q.last_us);
    uint64_t committed20=0;
    if(!clock_reference_read(&committed20)){fault=true;relay_fence();relay_stop_pa();return;}
    if(!relay_map.commit_packet(prepared,committed20)){fault=true;relay_fence();relay_stop_pa();return;}
    relay_data_dirty=false;
    if(!relay_map.live(committed20)&&relay_map.withdraw_time()){
      relay_data_dirty=true;relay_cap_dirty=true;
    }
  }
}
#else
void relay_fence(){}
void relay_close(uint64_t){}
bool relay_owned(){return false;}
bool relay_acl_available(){return true;}
void relay_poll(uint64_t,bool){}
#endif
