/* SPDX-License-Identifier: Apache-2.0. PUBLIC TEST KEY ONLY. */
#include "group_control.h"
#include "group_join.h"
#include "group_precision.h"
#include <assert.h>
#include <stdio.h>
static void emit(const char *kind,const uint8_t *w,size_t n){printf("%s ",kind);for(size_t i=0;i<n;i++)printf("%02x",w[i]);putchar('\n');}
int main(void){
 struct r2_group_config g={.id=UINT64_C(0x123456789abcdef0),.epoch=0x01020304};for(unsigned i=0;i<32;i++)g.psk[i]=i;
 uint64_t ids[4]={111,222,333,444};uint8_t wire[223];
 struct r2_auth_record h={.kind=10,.sender=0,.target=2,.root=255,.boot=9,.coordinator_boot=9,.challenge=5,.sequence=1,.decision=2,.created_us=100,.evidence_us=100,.expires_us=200,.config_epoch=5};
 assert(!r2_group_control_encode(wire,&h,&g,ids,"Kitchen clock",3));emit("hello",wire,223);
 assert(!r2_group_control_encode14(wire,&h,&g,ids,"Kitchen clock",3));emit("v14_hello",wire,223);
 h.kind=8;h.root=0;assert(!r2_group_control_encode(wire,&h,&g,ids,"Kitchen clock",3));emit("service",wire,168);
 assert(!r2_group_control_encode14(wire,&h,&g,ids,"Kitchen clock",3));emit("v14_service",wire,168);
 struct r2_precision_packet p={.root=0,.boot=9,.source_epoch=1,.train=1,.publication=3,.capture_epoch=1,.flags=1,.created_ns=1000000000,.expires_ns=2000000000,.count=2};
 for(unsigned i=0;i<2;i++)p.correction[i]=(struct r2_precise_correction){.publication=i+1,.event=257+i,.channel=8+i,.ticks=16000000+i*1000000,.generation=1,.expires_ns=2000000000};
 assert(!r2_group_precision_encode(wire,&p,&g,ids));emit("direct",wire,158);
 struct r2_relay_packet r={.origin={.root=0,.relay=1,.boot=9,.source_epoch=1,.created_ns=1000000000,.expires_ns=2000000000,.frame_ns=1000000000},.route={.transmitter=1,.parent=0,.depth=1,.slot=1,.boot=10,.train=1,.publication=3,.capture_epoch=1,.map_generation=1,.bound_ns=UINT32_MAX},.count=2};
 for(unsigned i=0;i<2;i++)r.correction[i]=(struct r2_relay_correction){.publication=i+1,.event=257+i,.channel=8+i,.ticks=16000000+i*1000000,.generation=1};
 uint8_t proof[32];assert(!r2_group_origin_sign(proof,&r.origin,&g,ids));assert(!r2_group_relay_encode(wire,&r,proof,&g,ids));emit("relay",wire,190);
 r.origin.hold_horizon_us=5000000;r.origin.hold_budget_ns=1000;r.route.bound_ns=100;
 assert(!r2_group_origin_sign3(proof,&r.origin,&g,ids));assert(!r2_group_relay_encode3(wire,&r,proof,&g,ids));emit("v14_held_relay",wire,190);
 r.origin.hold_horizon_us=r.origin.hold_budget_ns=0;r.origin.expires_ns=2650000000;r.route.bound_ns=UINT32_MAX;
 assert(!r2_group_origin_sign3(proof,&r.origin,&g,ids));assert(!r2_group_relay_encode3(wire,&r,proof,&g,ids));emit("v14_serving1650_relay",wire,190);
 struct r2_join_record j={.kind=4,.capabilities=3,.control=1,.sender=111,.receiver=555,.sender_boot=9,.receiver_boot=11,.client_nonce=123,.server_nonce=456,.sequence=2,.created_us=1000000,.echo_us=500000,.root=111,.root_boot=9,.source_epoch=1,.publication=1,.utc_us=1791417600000000ULL,.remaining_us=1000000,.source_flags=1,.source_bound_us=40000,.name="Kitchen clock"};
 assert(!r2_group_join_encode(wire,&j,&g));emit("join_accept",wire,R2_GROUP_JOIN_BYTES);
}
