// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Exercise the actual passive AD dispatcher/publication with synthetic packets.
#include "mesh_owner.hpp"
extern "C" {
#include "../../t096/protocol/role_announcement.h"
#include "../../t096/protocol/gnss_diagnostics.h"
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
 std::puts("PASS actual passive owner: canonical advertiser and SID, independent GS/GD dispatch, no GS history reset, publication preserves original GD deadline");
}
