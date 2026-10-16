/* SPDX-License-Identifier: Apache-2.0 */
#include "group_control.h"
#include "group_precision.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct r2_group_config g={.psk={1,2,3},.id=0x123456789abcdef0,.epoch=1};
static const uint64_t a[4]={111,222,333,0},b[4]={333,111,222,0};
static void control(void){
 struct r2_auth_record r={.kind=R2_AUTH_HELLO,.sender=0,.target=2,.root=255,.boot=9,.coordinator_boot=9,.challenge=5,.sequence=1,.decision=2,.created_us=100,.evidence_us=100,.expires_us=200,.config_epoch=R2_AUTH_CONFIG},got;
 uint8_t wire[R2_GROUP_CONTROL_MAX],caps;char name[25];
 assert(!r2_group_control_encode(wire,&r,&g,a,"Kitchen clock",3));assert(r2_control_wire_size(wire)==223);
 assert(!r2_group_control_decode(&got,wire,223,&g,b,111,name,&caps));assert(got.sender==1&&got.target==0&&got.root==255&&caps==3&&!strcmp(name,"Kitchen clock"));
 struct r2_group_config wrong=g;wrong.psk[0]^=1;assert(r2_group_control_decode(&got,wire,223,&wrong,b,111,name,&caps));
 wrong=g;wrong.epoch=2;assert(r2_group_control_decode(&got,wire,223,&wrong,b,111,name,&caps));
 for(unsigned i=0;i<223;i++){wire[i]^=1;assert(r2_group_control_decode(&got,wire,223,&g,b,111,name,&caps));wire[i]^=1;}
 r.pair_root=0;r.pair_standby=1;r.pair_root_boot=9;r.pair_standby_boot=10;r.pair_epoch=1;r.pair_until_us=1000;
 assert(!r2_group_control_encode(wire,&r,&g,a,"Kitchen clock",3));uint64_t partial[4]={333,111,0,0};
 assert(!r2_group_control_decode(&got,wire,223,&g,partial,111,name,&caps));assert(!got.pair_until_us&&got.sender==1&&caps==3);
 r.pair_root=r.pair_standby=0;r.pair_root_boot=r.pair_standby_boot=r.pair_until_us=0;r.pair_epoch=0;
 r.kind=R2_AUTH_SERVICE;r.root=0;assert(!r2_group_control_encode(wire,&r,&g,a,"Kitchen clock",3));assert(r2_control_wire_size(wire)==168);
 assert(!r2_group_control_decode(&got,wire,168,&g,b,111,name,&caps));assert(got.root==1);
 puts("PASS group control: identity remapping, names, wrong key/epoch, every-byte corruption, fixed sizes");
}
static void radio(void){
 struct r2_precision_packet p={.root=0,.boot=9,.source_epoch=1,.train=1,.publication=1,.capture_epoch=1,.flags=1,.created_ns=1000000000,.expires_ns=2000000000},q;
 uint8_t ad[190];assert(!r2_group_precision_encode(ad,&p,&g,a));assert(!r2_group_precision_decode(&q,ad,158,&g,b));assert(q.root==1&&q.created_ns==p.created_ns);
 for(unsigned i=0;i<158;i++){ad[i]^=1;assert(r2_group_precision_decode(&q,ad,158,&g,b));ad[i]^=1;}
 struct r2_relay_packet r={.origin={.root=0,.relay=1,.boot=9,.source_epoch=1,.created_ns=1000000000,.expires_ns=2000000000,.frame_ns=1000000000},
  .route={.transmitter=1,.parent=0,.depth=1,.slot=1,.boot=10,.train=1,.publication=1,.capture_epoch=1,.map_generation=1,.bound_ns=UINT32_MAX}},s;
 uint8_t proof[32];assert(!r2_group_origin_sign(proof,&r.origin,&g,a));assert(!r2_group_relay_encode(ad,&r,proof,&g,a));
 assert(!r2_group_relay_decode(&s,ad,190,&g,b));assert(s.origin.root==1&&s.origin.relay==2&&s.route.transmitter==2&&s.route.parent==1&&s.route.bound_ns==UINT32_MAX);
 for(unsigned i=0;i<190;i++){ad[i]^=1;assert(r2_group_relay_decode(&s,ad,190,&g,b));ad[i]^=1;}
 puts("PASS group PA: stable-ID remap, immutable root and hop MACs, every-byte corruption, 158/190-byte size");
}
/* Population is not a recipient list: five independent listeners decode the
 * SAME authenticated advertisement using only their local root/path dictionary.
 * The dictionary indexes a decoded path, not server-side client membership. */
static void passive_population(void){
 const uint64_t core[4]={111,222,333,444};
 struct r2_precision_packet p={.root=0,.boot=9,.source_epoch=1,.train=1,.publication=1,.capture_epoch=1,.flags=1,.created_ns=1000000000,.expires_ns=2000000000},got;
 uint8_t wire[158];assert(!r2_group_precision_encode(wire,&p,&g,core));
 for(unsigned i=0;i<5;i++){
  uint64_t listener[4]={1000+i,111,0,0};
  assert(!r2_group_precision_decode(&got,wire,sizeof wire,&g,listener));
  assert(got.root==1&&got.boot==9&&got.source_epoch==1);
  /* Successful decoding does not assert freshness, precision or a control seat. */
 }
 struct r2_auth_record control={.kind=R2_AUTH_HELLO,.sender=4,.target=0};
 uint8_t control_wire[R2_GROUP_CONTROL_MAX];
 assert(r2_group_control_encode(control_wire,&control,&g,core,"Fifth clock",3));
 puts("PASS five sessionless PSK receivers decode one packet independently of four-seat GM control; no timing qualification claim");
}
static void generic_origin(void){
 const uint64_t root_only[4]={111,0,0,0},path[4]={111,99999,77777,0};
 struct r2_relay_packet p={.origin={.root=0,.relay=1,.boot=9,.source_epoch=1,.created_ns=1000000000,.expires_ns=2000000000,.frame_ns=1000000000},
 .route={.transmitter=0,.parent=0,.depth=0,.boot=9,.train=1,.publication=1,.capture_epoch=1,.map_generation=1,.bound_ns=UINT32_MAX}},got;
 uint8_t proof[32],wire[190];assert(!r2_group_origin_sign2(proof,&p.origin,&g,root_only));
 assert(!r2_group_relay_encode2(wire,&p,proof,&g,root_only));
 assert(!r2_group_get(wire+22,8));assert(!r2_group_relay_decode2(&got,wire,190,&g,path));
 assert(r2_group_relay_decode(&got,wire,190,&g,path));
 p.route.transmitter=1;p.route.depth=1;p.route.slot=1;p.route.boot=87;
 /* The same root proof authorizes no named relay: an unlisted local path
  * forwards source provenance under its separate route MAC. */
 assert(!r2_group_relay_encode2(wire,&p,proof,&g,path));
 assert(!r2_group_relay_decode2(&got,wire,190,&g,path));
 for(unsigned i=0;i<190;i++){wire[i]^=1;assert(r2_group_relay_decode2(&got,wire,190,&g,path));wire[i]^=1;}
 assert(!r2_group_origin_sign(proof,&p.origin,&g,path));assert(!r2_group_relay_encode(wire,&p,proof,&g,path));
 assert(r2_group_relay_decode2(&got,wire,190,&g,path));
 puts("PASS GRv2 generic origin, unlisted relay, strict version/domain separation, every-byte corruption");
}
int main(void){control();radio();passive_population();generic_origin();assert(r2_group_name("Room 1",6));assert(r2_group_name("\xc3\xa9",2));assert(!r2_group_name("\xc0\x80",2));assert(!r2_group_name("bad\n",4));assert(!r2_group_name("\xe2\x80\xae",3));puts("PASS bounded UTF-8 name validation");}
