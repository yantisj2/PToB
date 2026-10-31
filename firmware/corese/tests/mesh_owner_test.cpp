// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Exercise the actual passive AD dispatcher/publication with synthetic packets.
#include "mesh_owner.hpp"
extern "C" {
#include "../../t096/protocol/role_announcement.h"
#include "../../t096/protocol/gnss_diagnostics.h"
#include "../protocol/gm7_codec.h"
}
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace corese;
int main(){
 r2_group_config group{{1,2,3},17,9};mesh::Scope scope{1,2,912345678};
 uint8_t address[16];r2_group_pair_key(address,&group,1,1);address[5]|=0xc0;
 assert(owner::init());
 r2_role_record r{};r.root=r.sender=1;r.root_boot=r.sender_boot=2;r.source_epoch=scope.epoch;
 r.coordinator=1;r.flags=1;r.members=1;r.sequence=1;r.created_us=1000000;r.valid_us=2000000;
 uint8_t gs[R2_ROLE_AD_BYTES];assert(!r2_role_encode(gs,&r,&group));
 owner::advertisement(gs,sizeof gs,12,address,scope,group,1000000,1000000);
 owner::Observation o;assert(owner::snapshot(o,1000000)&&o.roles.known&&o.roles.primary_since_us==1000000&&!o.reference.reported);
 r2_gnss_record q{};q.root=1;q.root_boot=2;q.mesh_epoch=scope.epoch;q.sequence=1;q.sampled_us=2000000;q.valid_us=30000000;
 q.known=7;q.hdop_centi=UINT16_MAX;q.sats=UINT8_MAX;q.source_kind=1;q.rmc_age_ms=q.pps_age_ms=q.gga_age_ms=UINT16_MAX;
 q.priority1=q.priority2=128;q.clock_class=248;q.accuracy=254;q.variance=UINT16_MAX;
 uint8_t gd[R2_GNSS_AD_BYTES];assert(!r2_gnss_encode(gd,&q,&group));
 owner::advertisement(gd,sizeof gd,12,address,scope,group,2000000,2000000);
 assert(owner::snapshot(o,2000000)&&o.reference.accepted==1&&o.roles.primary_since_us==1000000&&o.roles.received_us==1000000);
 ++q.sequence;q.sampled_us=3000000;assert(!r2_gnss_encode(gd,&q,&group));
 address[0]^=1;owner::advertisement(gd,sizeof gd,12,address,scope,group,3000000,3000000);address[0]^=1;
 owner::advertisement(gd,sizeof gd,13,address,scope,group,3000000,3000000);
 assert(owner::snapshot(o,3000000)&&o.reference.accepted==1);
 owner::advertisement(gd,sizeof gd,12,address,scope,group,3000000,3000000);
 owner::advertisement(gd,sizeof gd,12,address,scope,group,4000000,4000000);
 assert(owner::snapshot(o,4000000)&&o.reference.accepted==2&&o.reference.replayed==1&&o.reference.until_us==33000000);
 assert(o.roles.primary_since_us==1000000&&o.roles.received_us==1000000&&!mesh::live(o.roles,4000000));
 // GD-only windows and absent/malformed diagnostics cannot reset GS history.
 const uint8_t unrelated[]={2,1,6},malformed[]={255,0};
 owner::advertisement(unrelated,sizeof unrelated,12,address,scope,group,5000000,5000000);
 owner::advertisement(malformed,sizeof malformed,12,address,scope,group,6000000,6000000);
 ++r.sequence;r.created_us=10000000;assert(!r2_role_encode(gs,&r,&group));
 owner::advertisement(gs,sizeof gs,12,address,scope,group,10000000,10000000);
 assert(owner::snapshot(o,10000000)&&o.roles.primary_since_us==1000000&&o.roles.accepted==2&&o.reference.accepted==2&&o.reference.until_us==33000000);
 // A strict legacy88 body must also traverse the actual AD length dispatcher.
 ++q.sequence;q.sampled_us=11000000;assert(!r2_gnss_encode(gd,&q,&group));
 uint8_t legacy[R2_GNSS_LEGACY_AD_BYTES];std::memcpy(legacy,gd,R2_GNSS_LEGACY_AD_BODY);legacy[0]=sizeof legacy-1;legacy[6]=1;
 assert(!r2_group_mac(legacy+R2_GNSS_LEGACY_AD_BODY,&group,"R2GNDG01",legacy,R2_GNSS_LEGACY_AD_BODY));
 owner::advertisement(legacy,sizeof legacy,12,address,scope,group,11000000,11000000);
 assert(owner::snapshot(o,11000000)&&o.reference.accepted==3&&o.reference.wire_version==1&&o.reference.hdop_centi==UINT16_MAX&&o.reference.until_us==41000000);
 owner::capabilities(1,2,7,12000000,13000000);
 owner::scan_state(true,12012000,8,12000000);owner::source_lost(12000001);
 assert(owner::snapshot(o,12000001)&&!o.cap.known&&!o.offset_valid&&!o.offset_at&&!o.offset_scope.root&&!o.scan_allowed&&!o.scan_deadline);
 assert(o.reference.accepted==3&&o.reference.until_us==41000000); // History cannot renew or become authority.
 // New implementation4 management uses SID14, independently of SID12 timing
 // locators and the legacy diagnostic AD sizes.
 gm7_packet p{};p.group=group.id;p.key_epoch=group.epoch;p.kind=GM7_ANNOUNCE;p.flags=GM7_ELIGIBLE;
 auto& c=p.body.control;c.sender=c.active=1;c.boot=c.active_boot=2;c.sequence=UINT64_C(0x100000001);
 c.sent_utc_us=1792986375188267;c.birth_us=1792986374188267;c.founder=1;c.term=16;
 c.timing_epoch=scope.epoch;c.max_age_us=2750000;c.source_valid_us=1500000;c.source_gen=c.capture_gen=c.train_gen=1;
 uint8_t gm7[GM7_MAX_AD_BYTES];assert(!gm7_ad_encode(gm7,sizeof gm7,&p,&group));
 owner::advertisement(gm7,sizeof gm7,12,address,scope,group,14000000,14000000);
 assert(owner::snapshot(o,14000000)&&o.roles.wire_version==1);
 address[0]^=1;owner::advertisement(gm7,sizeof gm7,14,address,scope,group,14000000,14000000);address[0]^=1;
 assert(owner::snapshot(o,14000000)&&o.roles.wire_version==1);
 // Complete HCI reports remain usable after a bounded radio window closes.
 // The callback is not an RF timestamp; authentication/replay/TTL still apply.
 owner::advertisement(gm7,sizeof gm7,14,address,scope,group,14000000,14001000,true);
 assert(owner::snapshot(o,14001000)&&o.roles.wire_version==4&&o.roles.birth_utc_us==c.birth_us&&o.roles.sequence==c.sequence&&o.scan_outside_callbacks==1);
 const auto g7_until=o.roles.until_us;
 owner::advertisement(gm7,sizeof gm7,14,address,scope,group,15000000,15000000);
 assert(owner::snapshot(o,15000000)&&o.roles.replayed==1&&o.roles.until_us==g7_until);
 // Truncated fields must not be inspected before length validation.
 uint8_t short_g7[32]{};short_g7[0]=31;short_g7[1]=255;std::memcpy(short_g7+4,"G7V4",4);
 owner::advertisement(short_g7,sizeof short_g7,14,address,scope,group,16000000,16000000);
 assert(owner::snapshot(o,16000000)&&o.roles.accepted==1&&o.roles.until_us==g7_until);
 owner::source_lost(17000000);assert(owner::snapshot(o,17000000)&&o.roles.birth_utc_us==c.birth_us&&!mesh::live(o.roles,17000000));
 // Every accepted reference is visited, even when a NO and recovery both land
 // between display publications. Replays cannot renew the observation deadline.
 q.values=3;q.rmc_age_ms=q.pps_age_ms=100;++q.sequence;q.sampled_us=18000000;assert(!r2_gnss_encode(gd,&q,&group));
 owner::advertisement(gd,sizeof gd,12,address,scope,group,18000000,18000000);
 assert(owner::snapshot(o,18000000)&&o.reference_observation.tracking&&o.reference_observation.since_us==18000000);
 const auto observation_until=o.reference_observation.until_us;
 owner::advertisement(gd,sizeof gd,12,address,scope,group,19000000,19000000);
 assert(owner::snapshot(o,19000000)&&o.reference_observation.until_us==observation_until);
 q.values=1;++q.sequence;q.sampled_us=20000000;assert(!r2_gnss_encode(gd,&q,&group));
 owner::advertisement(gd,sizeof gd,12,address,scope,group,20000000,20000000);
 assert(owner::snapshot(o,20000000)&&!o.reference_observation.tracking);
 q.values=3;++q.sequence;q.sampled_us=20000001;assert(!r2_gnss_encode(gd,&q,&group));
 owner::advertisement(gd,sizeof gd,12,address,scope,group,20000001,20000001);
 assert(owner::snapshot(o,20000001)&&o.reference_observation.since_us==20000001);
 owner::source_lost(20000002);assert(owner::snapshot(o,20000002)&&!o.reference_observation.tracking);
 std::puts("PASS actual passive owner: canonical advertiser and SID, independent GS/GD dispatch, no GS history reset, publication preserves original GD deadline");
}
