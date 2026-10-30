// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
if(which=="corese-tz"){
 using namespace v12;
 fake_now=1000000;anchor={true,111,3,1700000000000000ull,fake_now,fake_now+1500000,4,5,40000};
 joined_peer=111;joined_boot=3;
 gm7_packet p{};p.kind=GM7_ANNOUNCE;p.flags=GM7_ELIGIBLE;p.group=provision.group.id;p.key_epoch=provision.group.epoch;
 auto& c=p.body.control;c.sender=c.active=111;c.boot=c.active_boot=3;c.birth_us=1699999000000000ull;c.founder=111;c.term=1;
 c.timing_epoch=4;c.train_gen=c.source_gen=c.capture_gen=5;c.max_age_us=2750000;c.source_valid_us=1500000;
 c.sent_mono_us=1000000;c.sent_utc_us=anchor.utc;c.sequence=1;c.tz={2,-300};
 Discovery d{};d.sid=14;d.at=fake_now;d.address=canonical(111);
 auto send=[&](){d.length=gm7_wire_size(p.kind)+4;assert(!gm7_ad_encode(d.bytes,sizeof d.bytes,&p,&provision.group));corese_zone(d);};
 send();assert(coarse_tz.value.id==2&&coarse_tz.value.offset_min==-300&&coarse_tz.until_us==1750000);
 ++c.sequence;c.tz.offset_min=-360;send();assert(coarse_tz.value.offset_min==-360);
 --c.sequence;c.tz.offset_min=-300;send();assert(coarse_tz.value.offset_min==-360);
 c.sequence=3;c.tz=r2_tz_unknown();send();assert(!coarse_tz.value.id);
 r2_tz_renew(&coarse_tz,fake_now+1000,R2_TZ_GM_PERIOD_US);assert(!coarse_tz.value.id);
 c.sequence=4;c.tz={2,-300};d.address.val[0]^=1;send();assert(!coarse_tz.value.id);d.address=canonical(111);
 ++c.train_gen;send();assert(!coarse_tz.value.id);--c.train_gen;
 send();assert(coarse_tz.value.id==2);assert(!r2_tz_current(&coarse_tz,1750000).id);
 changed_scope(fake_now);assert(!coarse_tz.value.id&&!coarse_tz_order.ordered);
 std::puts("PASS actual CoreSE TZ owner: GM4 authentication/address/train, DST update ordering, unknown nonresurrection, expiry and scope loss");return 0;
}
