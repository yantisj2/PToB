// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Public synthetic enrollment, actual frozen GM7 implementation4 codec/HMAC.
#include "mesh_diagnostics.hpp"
extern "C" {
#include "../protocol/gm7_codec.h"
}
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace corese::mesh;
int main(){
 r2_group_config group{{1,2,3},17,9};Gm7Monitor m;
 constexpr uint64_t birth=1792986374188267;Scope selected{1,2,3};
 gm7_packet p{};p.group=group.id;p.key_epoch=group.epoch;p.kind=GM7_ANNOUNCE;p.flags=GM7_ELIGIBLE;
 auto& c=p.body.control;c.sender=c.active=1;c.boot=c.active_boot=2;c.sequence=UINT64_C(0x100000001);
 c.sent_utc_us=birth+3600000000;c.max_age_us=2750000;c.source_valid_us=1500000;
 c.source_gen=c.capture_gen=c.train_gen=1;c.birth_us=birth;c.founder=1;c.term=16;c.timing_epoch=3;
 c.hot=4;c.hot_boot=5;c.sent_mono_us=1000000;
 uint8_t ad[GM7_MAX_AD_BYTES]{};size_t n=0;
 auto send=[&](uint64_t at,Scope scope=Scope{1,2,3},uint64_t now=0){
  n=gm7_wire_size(p.kind)+4;const int encoded=gm7_ad_encode(ad,sizeof ad,&p,&group);if(encoded)std::fprintf(stderr,"fixture rejected: kind=%u at=%llu flags=%u rc=%d\n",unsigned(p.kind),(unsigned long long)at,unsigned(p.flags),encoded);assert(!encoded);
  const auto sender=p.kind<=GM7_WITHDRAW?p.body.control.sender:p.kind==GM7_ROLE?p.body.role.sender:p.body.gnss.sender;
  return m.receive(ad,n,group,sender,scope,at,now?now:at);
 };
 assert(send(1000000));auto r=m.roles(1000000);
 assert(r.known&&r.wire_version==4&&r.birth_utc_us==birth&&r.term==16&&r.sequence==UINT64_C(0x100000001));
 assert(r.hot==4&&!r.hot_confirmed&&r.hot_since_us==1000000&&r.primary_since_us==1000000);
 uint64_t elapsed=0;assert(epoch_elapsed(r,birth+10000000,elapsed)&&elapsed==10000000);
 assert(!epoch_elapsed(r,birth-1,elapsed)&&!elapsed);
 assert(!epoch_elapsed(r,UINT64_MAX,elapsed)&&!elapsed);
 const auto deadline=r.until_us;
 assert(!m.receive(ad,n,group,1,selected,1100000,1100000)&&m.roles(1100000).until_us==deadline);
 ad[n-1]^=1;assert(!m.receive(ad,n,group,1,selected,1200000,1200000));ad[n-1]^=1;
 assert(!m.receive(ad,n,group,9,selected,1200000,1200000));
 assert(!m.receive(ad,n,group,1,{1,9,3},1200000,1200000));
 auto wrong=group;++wrong.epoch;assert(!m.receive(ad,n,wrong,1,selected,1200000,1200000));
 ++c.sequence;++c.sent_mono_us;assert(!send(1300000,selected,1300000+c.max_age_us));
 assert(!send(1300000,selected,1299999));
 assert(send(1400000));assert(m.roles(1400000).primary_since_us==1000000);
 const auto ann=p;
 p.kind=GM7_STATUS;p.flags=GM7_ELIGIBLE|GM7_CAPABLE|GM7_SHADOW|GM7_HOT;
 c.sender=4;c.boot=5;c.sequence=1;c.source_valid_us=0;c.shadow_gen=7;c.max_age_us=4500000;c.sent_mono_us=2000000;
 c.train_gen=2;assert(send(1900000)&&!m.roles(1900000).hot_confirmed);++c.sequence;c.train_gen=1;
 assert(send(2000000));r=m.roles(2000000);
 assert(r.hot_confirmed&&r.hot_since_us==1000000&&r.until_us==1400000+2750000);
 assert(!m.roles(6500000).hot_confirmed&&m.roles(6500000).hot_since_us==1000000); // Gap keeps event history.
 ++c.sequence;c.sent_mono_us=3000000;assert(send(3000000));assert(m.roles(3000000).hot_since_us==1000000);
 p.flags=GM7_CAPABLE;++c.sequence;c.sent_mono_us=4000000;c.shadow_gen=c.train_gen=0;c.hot=c.hot_boot=0;
 assert(send(4000000));assert(!m.roles(4000000).hot_confirmed&&m.roles(4000000).hot_since_us==1000000);
 // Normal capability advertisements may have no current epoch at all.
 c.sender=6;c.boot=7;c.sequence=1;c.birth_us=c.founder=c.term=c.active=c.active_boot=0;c.timing_epoch=0;
 assert(send(4100000));assert(m.roles(4100000).statuses==5&&m.roles(4100000).root==1);
 // ROLE and GNSS have independent sequence namespaces, even at the same boot.
 p={};p.group=group.id;p.key_epoch=group.epoch;p.kind=GM7_ROLE;
 p.body.role={};auto& role=p.body.role;role.sender=role.active=1;role.boot=role.active_boot=2;role.sequence=1;
 role.sampled_mono_us=5000000;role.birth_us=birth;role.founder=1;role.term=16;role.timing_epoch=3;role.valid_us=4500000;
 role.hot=4;role.hot_boot=5;role.train_gen=1;role.role=GM7_W_ACTIVE;role.hot_duration_ms=role.active_duration_ms=role.pps_duration_ms=UINT32_MAX;
 assert(send(5000000));assert(m.roles(5000000).role_reports==1&&m.roles(5000000).birth_utc_us==birth);
 // Positive ROLE=HOT is a real descriptive report, independent of STATUS.
 role.sender=4;role.boot=5;role.role=GM7_W_HOT;role.hot=4;role.hot_boot=5;role.sequence=1;role.sampled_mono_us=5200000;
 assert(send(5200000));r=m.roles(5200000);
 assert(r.hot_confirmed&&r.hot_report_known&&r.hot_report_kind==GM7_ROLE&&r.hot_received_us==5200000&&r.hot_since_us==1000000);
 // A nominal2s report can be lost: the4.5s original receipt TTL covers the
 // following report. Expiry changes freshness, never stable selection age.
 assert(m.roles(9200000).hot_confirmed&&m.roles(9200000).hot_until_us==9700000);
 r=m.roles(9700000);assert(!r.hot_confirmed&&r.hot_report_known&&r.hot_role==GM7_W_HOT&&r.hot_since_us==1000000);
 role.sequence=2;role.sampled_mono_us=5300000;role.role=GM7_W_PREPARING;
 assert(send(5300000));assert(!m.roles(5300000).hot_confirmed&&m.roles(5300000).hot_role==GM7_W_PREPARING&&m.roles(5300000).hot_since_us==1000000);

 p={};p.group=group.id;p.key_epoch=group.epoch;p.kind=GM7_GNSS;auto& g=p.body.gnss;
 g.sender=g.active=1;g.boot=g.active_boot=2;g.sequence=1;g.sampled_mono_us=6000000;
 g.birth_us=birth;g.founder=1;g.term=16;g.timing_epoch=3;g.valid_us=30000000;
 g.known=63;g.values=7;g.sats=19;g.hdop_centi=80;g.clock_class=6;g.accuracy=254;g.variance=UINT16_MAX;g.utc_error_us=UINT32_MAX;
 assert(send(6000000));auto q=m.reference(6000000);
 assert(q.reported&&q.wire_version==4&&q.hdop_centi==80&&q.sequence==1&&q.scope.root==1&&q.until_us==36000000);
 assert(!send(6100000)&&m.reference(6100000).until_us==36000000);
 ++g.sequence;g.sender=7;g.boot=8;assert(!send(6200000));g.sender=1;g.boot=2;
 ++g.term;assert(!send(6200000));
 // A primary changes term, identity and timing incarnation within one epoch.
 p=ann;auto& next=p.body.control;next.sequence=1;next.sender=next.active=10;next.boot=next.active_boot=20;
 next.term=17;next.timing_epoch=4;next.sent_mono_us=7000000;next.sent_utc_us=birth+7200000000;
 assert(send(7000000,{10,20,4}));r=m.roles(7000000);
 assert(r.birth_utc_us==birth&&r.epoch_since_us==1000000&&r.primary_since_us==7000000&&r.term==17&&r.hot_since_us==1000000&&!r.hot_report_known);
 assert(epoch_elapsed(r,birth+7200000000,elapsed)&&elapsed==7200000000);
 const auto new_leader=p;
 p=ann;++p.body.control.sequence;assert(!send(7100000,{10,20,4})); // Old selection cannot overwrite.
 assert(!send(7100000,{})); // Earlier term cannot overwrite an empty-scope cache either.
 p=new_leader;++p.body.control.sequence;p.flags|=GM7_YIELD;p.body.control.yield_utc_us=p.body.control.sent_utc_us+750000;
 assert(send(7200000,{10,20,4}));assert(m.roles(7200000).flags&GM7_YIELD);
 p.kind=GM7_WITHDRAW;p.flags=0;++p.body.control.sequence;p.body.control.source_valid_us=0;p.body.control.yield_utc_us=0;p.body.control.max_age_us=1000000;
 assert(send(7300000,{10,20,4}));assert(m.roles(7300000).withdrawn&&!live(m.roles(7300000),7300000)&&m.roles(7300000).birth_utc_us==birth);
 p=new_leader;++p.body.control.sequence; // Replayed sequence cannot resurrect a withdrawal.
 assert(!send(7400000,{10,20,4}));
 p.body.control.sequence+=10;p.body.control.birth_us=birth+86400000000;p.body.control.sent_utc_us=p.body.control.birth_us+1000000;
 assert(send(7500000,{10,20,4}));assert(m.roles(7500000).epoch_since_us==7500000);
 // First discovery can show a signed report while timing remains unadmitted.
 Gm7Monitor discovery;assert(discovery.receive(ad,n,group,10,{},7500000,7500000));
 assert(discovery.roles(7500000).known); // No clock model exists in this class.
 char date[32];epoch_start_text(date,sizeof date,946684800000000);assert(!std::strcmp(date,"2000-01-01 00:00:00"));
 epoch_start_text(date,sizeof date,951782400000000);assert(!std::strcmp(date,"2000-02-29 00:00:00"));
 epoch_start_text(date,sizeof date,0);assert(!std::strcmp(date,"--"));
 epoch_start_text(date,sizeof date,4102444800000000);assert(!std::strcmp(date,"--"));
 std::puts("PASS actual GM7 implementation4: ANN/STATUS/ROLE/GNSS, canonical scope, 64-bit sequences, independent replay fences, original expiry, HOT nomination/confirmation/withdrawal, YIELD/WITHDRAW, actual UTC birth and epoch age across leadership changes");
}
