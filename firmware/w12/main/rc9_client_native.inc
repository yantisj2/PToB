// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Included in the sole G1 worker. RC9 never enters normal G1/RC7 training.
#ifdef PTOB_RC9
void rc9_receive_control(const Incoming& row,uint64_t now){
 rc9::Control c;
 if(!rc9::decode_control(c,row.bytes,row.length,provision.group)){inc(rejected);peer.dead=true;return;}
 inc(auth);
 if(peer.stage==2&&c.type==rc9::Type::Capability&&c.sender==peer.identity){
  if(nonce==UINT64_MAX||!rc9_client.make_request(c,provision.identity,boot_id,++nonce,now)){inc(rejected);peer.dead=true;return;}
  peer.stage=5;peer.ready=true;peer.send_kind=uint8_t(rc9::Type::Request);
 }else if(peer.stage==6&&c.type==rc9::Type::Response){
  const auto old=rc9_client.view(now);
  if(!old.bound||old.server!=c.sender||old.server_boot!=c.sender_boot)changed_scope(now);
  if(!rc9_client.bind(c,now)){inc(rejected);peer.dead=true;return;}
  joined_peer=c.sender;joined_boot=c.sender_boot;member=true;sync_address=peer.address;
  sync_identity=c.sender;if(!synced&&!sync_pending)sync_selected=true;
  peer.selection_accepted=true;peer.selection_reported=true;peer.close_at=now+2000;inc(join_accepts);rc9_retry_at=0;
 }else if(peer.stage==6&&c.type==rc9::Type::Capability)peer.read_at=now+20000;
 else{inc(rejected);peer.dead=true;}
}
void rc9_send_request(uint64_t now){
 uint8_t wire[rc9::ControlBytes];
 if(!rc9::encode_control(wire,rc9_client.request,provision.group)){peer.dead=true;inc(rejected);return;}
 peer.pending=true;peer.send_kind=0;
 const auto h=peer.handle;const auto generation=peer.incarnation;
 const int rc=ble_gattc_write_flat(h,peer.value,wire,sizeof wire,rc_written,callback_arg());
 if(!rc)inc(tx);else if(peer.handle==h&&peer.incarnation==generation){peer.pending=false;peer.dead=true;note(rc,R2Api::ControlWrite);}
 (void)now;
}
bool rc9_discovery(const Discovery& d){
 if(d.sid!=15)return false;
 for(unsigned i=0;i<d.length;){const unsigned n=d.bytes[i];if(!n||i+n>=d.length)return false;
  if(n==145&&d.bytes[i+1]==255){rc9::Control cap;
   if(!rc9::decode_capability(cap,d.bytes+i+2,144,provision.group)||cap.sender==provision.identity||
      !same_address(d.address,canonical(cap.sender)))return false;
   unsigned at=4;for(unsigned j=0;j<4;++j)if(rc9_locators[j].id==cap.sender){at=j;break;}
   if(at==4){at=0;for(unsigned j=1;j<4;++j)if(rc9_locators[j].seen<rc9_locators[at].seen)at=j;}
   rc9_locators[at]={d,cap.sender,cap.sender_boot,d.at};return true;
  }i+=n+1;
 }return false;
}
// Returns true while a direct RC9 binding owns source selection. All GAP
// owners still use their existing callbacks and original2s drain deadlines.
bool rc9_selection(uint64_t now){
 uint64_t target=0;portENTER_CRITICAL(&mux);target=rc9_target;portEXIT_CRITICAL(&mux);
 const auto view=rc9_client.view(now);
 if(rc9_server_selected(now))return false; // RC8 server cannot recascade RC9.
 const bool bound=view.bound;
 if(bound&&!synced&&!sync_pending&&!sync_cleanup_queued&&!sync_terminating&&peer.handle==None&&!peer.connecting){
  sync_address=canonical(view.server);sync_identity=view.server;sync_selected=true;
 }
 if(peer.handle!=None||peer.connecting||now<retry_at||now<rc9_retry_at)return bound||target;
 const bool seek=target||bound||(!parent_selection.pinned()&&!following(now)&&!lock_protected(now));
 if(!seek)return false;
 if(bound&&now<rc9_client.renew_at&&view.valid)return true;
 unsigned best=4;
 for(unsigned i=0;i<4;++i){const auto& c=rc9_locators[i];
  if(!c.id||now<c.seen||now-c.seen>=2000000||(target&&c.id!=target)||
     (bound&&c.id!=view.server)||cooled(c.id,now))continue;
  if(best==4||c.discovery.rssi>rc9_locators[best].discovery.rssi)best=i;
 }
 if(best<4){const auto& c=rc9_locators[best];connect(c.discovery,c.id,now,true,true);rc9_retry_at=now+1000000;}
 return bound||target||best<4;
}
void rc9_publish_status(uint64_t now){
 const auto view=rc9_client.view(now);Rc9Status result;
 result.sampled_us=now;result.server=view.server;result.server_boot=view.server_boot;
 result.client_until_us=rc9_server_demand_until();result.clients=rc9_server_clients(now);
 result.serving=rc9_server_selected(now)&&result.clients!=0;result.fault=fault;
 result.accepted=rc9_client.accepted;result.rejected=rc9_client.rejected;
 if(!closing&&!fault&&!parent_diagnostic.pending()&&view.valid){
  result.valid=result.following=true;result.utc_us=view.utc_us;result.until_us=view.until_us;
  result.root=view.root;result.root_boot=view.root_boot;result.source_epoch=view.source_epoch;
  result.emitter=view.emitter;result.emitter_boot=view.emitter_boot;result.sequence=view.publication;
  result.rms_ns=view.link_rms_ns;result.assurance=view.assurance;result.authority=view.authority;
  result.metadata_known=view.root_boot&&view.emitter_boot;
 }
 portENTER_CRITICAL(&mux);rc9_published=result;portEXIT_CRITICAL(&mux);
}
#endif
