// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
if(which=="corese-mesh"){
 using namespace v12;
 assert(!corese_group.id&&!member&&!anchor.source);
 auto first=provision.group,second=provision.group;first.id=11111;first.epoch=11;second.id=22222;second.epoch=12;
 auto announce=[&](const r2_group_config& group){
  gm7_packet p{};p.kind=GM7_ANNOUNCE;p.flags=GM7_ELIGIBLE;p.group=group.id;p.key_epoch=group.epoch;
  auto& c=p.body.control;c.sender=c.active=111;c.boot=c.active_boot=3;c.birth_us=1699999000000000ull;c.founder=111;c.term=1;
  c.timing_epoch=4;c.train_gen=c.source_gen=c.capture_gen=5;c.max_age_us=2750000;c.source_valid_us=1500000;
  c.sent_mono_us=fake_now;c.sent_utc_us=1700000000000000ull;c.sequence=1;c.tz={2,-300};
  Discovery d{};d.sid=14;d.at=fake_now;d.address.type=1;corese::mesh_address(d.address.val,group,111);
  d.length=gm7_wire_size(p.kind)+4;assert(!gm7_ad_encode(d.bytes,sizeof d.bytes,&p,&group));return d;
 };
 auto one=announce(first),two=announce(second);
 auto foreign=second;foreign.psk[0]^=1;auto wrong=announce(foreign);corese_select_mesh(wrong);assert(!corese_group.id);
 for(unsigned i=0;i<one.length;++i){auto damaged=one;damaged.bytes[i]^=1;corese_select_mesh(damaged);assert(!corese_group.id);}
 auto wrong_address=one;wrong_address.address.val[0]^=1;corese_select_mesh(wrong_address);assert(!corese_group.id);
 Discovery locator{};locator.props=1;locator.length=25;locator.at=fake_now;locator.address=one.address;
 locator.bytes[0]=24;locator.bytes[1]=255;auto* l=locator.bytes+2;l[0]='G';l[1]='1';l[2]=65;
 r2_group_put(l+3,first.id,8);r2_group_put(l+11,first.epoch,4);r2_group_put(l+15,111,8);
 corese_select_mesh(locator);assert(corese_group.id==first.id&&corese_group.epoch==11&&!member&&!anchor.source&&!following(fake_now));
 // The volatile context must not pin discovery after actual source loss.
 coarse_tz.value={2,-300};coarse_tz.until_us=fake_now+750000;joined_peer=111;joined_boot=3;
 parent_selection.seen(999,false,-40,fake_now);retry_at=fake_now+1000000;
 const auto deadline=fake_now+123456;sync_pending=true;sync_drain_until=deadline;
 corese_select_mesh(two);assert(corese_group.id==first.id&&sync_pending&&sync_drain_until==deadline);sync_pending=false;
 peer.connecting=true;corese_select_mesh(two);assert(corese_group.id==first.id);peer.connecting=false;
 member=true;corese_select_mesh(two);assert(corese_group.id==first.id);member=false;
 anchor.source=true;corese_select_mesh(two);assert(corese_group.id==first.id);anchor={};
 corese_select_mesh(two);assert(corese_group.id==second.id&&corese_group.epoch==12&&!member&&!anchor.source);
 assert(!coarse_tz.value.id&&!joined_peer&&!joined_boot&&!retry_at&&!parent_selection.history().entries(fake_now));
 assert(same_address(own_address,canonical(provision.identity)));
 // An actual fresh nonce-bound JOIN, encoded under the discovered context,
 // is still necessary before coarse FOLLOW; no locator supplies permission.
 fresh_direct(111,3,4,5);assert(member&&corese_group.id==second.id&&join_accepts==1);
 one.at=fake_now;corese_select_mesh(one);assert(corese_group.id==second.id&&member);
 std::puts("PASS CoreSE AUTO: two nonenrolled IDs/epochs, MAC/address rejects, exact-owner fence, scope reset, fresh JOIN, live-source stability");return 0;
}
if(which=="corese-rc-tz"){
 using namespace v12;fake_now=1000000;
 rc7_control cap{};cap.type=RC7_CAPABILITY;cap.sender=222;cap.sender_boot=4;cap.next_challenge=55;cap.tz={2,-300};
 cap.path.source_epoch=14;cap.path.depth=1;cap.path.node[0]={111,3,1,3,0,0};cap.path.node[1]={222,4,2,2,1,1};
 peer={};peer.handle=22;peer.identity=222;peer.relay=true;peer.stage=2;peer.address=canonical(222);peer.incarnation=++incarnation;
 Incoming row{};row.at=fake_now;row.length=RC_DYNAMIC_CONTROL_BYTES;row.incarnation=peer.incarnation;
 assert(row.length==216&&!rc7_control_encode(row.bytes,&cap,&corese_group));receive_rc_control(row,fake_now);
 assert(peer.stage==5&&peer.send_kind==RC7_REQUEST&&!peer.dead&&rc_client.capabilities==1);
 corese_rc_write=true;send_rc_request(fake_now);assert(peer.stage==6&&!peer.dead&&output.back().size()==216);
 rc7_control transmitted{};assert(!rc7_control_decode(&transmitted,output.back().data(),output.back().size(),&corese_group)&&transmitted.type==RC7_REQUEST);
 auto reply=rc_client.request;reply.type=RC7_RESPONSE;reply.sender=222;reply.sender_boot=4;reply.target=provision.identity;reply.target_boot=22;
 reply.ready=true;reply.next_challenge=56;reply.publication_floor=10;reply.root_lo_ns=fake_now*1000-1000000;
 reply.root_hi_ns=fake_now*1000+1000000;reply.source_expires_ns=fake_now*1000+1500000000;reply.tz={2,-300};
 peer.stage=6;assert(!rc7_control_encode(row.bytes,&reply,&corese_group));receive_rc_control(row,fake_now);
 assert(rc_bound()&&member&&joined_peer==222&&coarse_tz.value.id==2&&coarse_tz.until_us==fake_now+300000);
 Discovery d{};d.sid=13;d.props=1;d.at=fake_now;d.address=canonical(222);d.length=RC_DYNAMIC_CAPABILITY_BYTES+2;
 d.bytes[0]=RC_DYNAMIC_CAPABILITY_BYTES+1;d.bytes[1]=0xff;
 auto send=[&](){assert(!rc7_capability_encode(d.bytes+2,&cap,&corese_group));corese_zone(d);};
 cap.next_challenge=56;send();assert(coarse_tz.value.offset_min==-300);
 cap.next_challenge=57;cap.tz.offset_min=-360;send();assert(coarse_tz.value.offset_min==-360);
 cap.next_challenge=56;cap.tz.offset_min=-300;send();assert(coarse_tz.value.offset_min==-360);
 cap.next_challenge=58;cap.tz=r2_tz_unknown();send();assert(!coarse_tz.value.id);
 cap.next_challenge=59;cap.tz={2,-300};d.address.val[0]^=1;send();assert(!coarse_tz.value.id);
 d.address=canonical(222);send();assert(coarse_tz.value.id==2);
 changed_scope(fake_now);assert(!coarse_tz.value.id&&!coarse_tz_order.ordered);
 std::puts("PASS actual RC8 owner:216-byte admission/response,148-byte capability, ordered DST, stale/address rejection, unknown and scope loss");return 0;
}
