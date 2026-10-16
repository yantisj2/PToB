#include "network.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 struct r2_network n;assert(!r2_network_init(&n,1,2,22,200,123,50000,0));
 struct r2_net_peer *p=&n.peer[0];p->identity=11;p->boot=100;p->member=true;p->member_at=100;
 p->connected=p->authenticated=true;p->seen=p->alive=100;p->own_nonce=15;
 n.now=100;r2_network_link(&n,0,false,200);assert(r2_network_member(&n,0));assert(!p->authenticated&&!p->connected);
 uint64_t old_nonce=p->own_nonce;r2_network_link(&n,0,true,300);assert(p->own_nonce!=old_nonce&&!p->authenticated);assert(r2_network_member(&n,0));
 n.now=10000099;assert(r2_network_member(&n,0));n.now=10000100;assert(!r2_network_member(&n,0));
 /* No timer or reconnect itself refreshes membership. */
 assert(p->member_at==100);
 /* Fresh-session challenge refusal is covered by actual network/HMAC tests.
  * An unqualified node cannot retain authorized aggressive takeover. */
 n.standby_authorized=true;n.standby_root_boot=100;n.root_boot=100;n.root_health_at=100;n.gm_mask=2;n.gnss_mask=2;
 struct r2_clock clock={0};r2_network_tick(&n,&clock,10000101);assert(!n.standby_authorized&&!n.takeover_pending);
 /* Exact2s suspicion with genuine independently qualified fixture source;
  * retain old root selection until its reservation, never promote unfenced. */
 assert(!r2_network_init(&n,1,2,22,200,123,50000,0));p=&n.peer[0];
 p->identity=11;p->boot=100;n.gm_mask=n.gnss_mask=3;
 n.root_boot=100;n.choice=0;n.coordinator=1;
 n.pair=(struct r2_pair){.root=0,.standby=1,.root_boot=100,.standby_boot=200,.until=80000000,.epoch=1};
 n.standby_authorized=true;n.standby_root_boot=100;n.standby_source_epoch=1;n.root_health_at=68000000;
 n.admission=(struct r2_source_admission){.boot_at=0,.stable_at=0,.now=69000000,.last_expiry=71000000,.source_epoch=1,.observing=true,.eligible=true};
 clock=(struct r2_clock){0};clock.view.source_available=true;clock.view.source_epoch=1;
 clock.last_pair_edge=clock.last_rmc=69000000;clock.last_pair_utc=1791504000000000LL;
 r2_network_tick(&n,&clock,69999999);assert(!n.takeover_pending);
 r2_network_tick(&n,&clock,70000000);assert(n.takeover_pending&&n.takeover_started==70000000);
 assert(n.takeover_not_before==73000000&&!n.local_gm);
 /* V14 controls reject old group semantics despite identical inner layout. */
 struct r2_group_config g={.psk={1},.id=123,.epoch=1};uint64_t ids[4]={11,22,0,0};
 struct r2_auth_record r={.kind=R2_AUTH_ACK,.sender=0,.target=1,.root=0,.boot=100,.root_boot=100,.coordinator_boot=100,.challenge=3,.sequence=1,.decision=1,.source_epoch=1,.config_epoch=R2_AUTH_CONFIG,.created_us=100,.evidence_us=100,.expires_us=200};
 uint8_t wire[R2_GROUP_CONTROL_MAX],caps;char name[25];struct r2_auth_record parsed;
 assert(!r2_group_control_encode14(wire,&r,&g,ids,"",0));
 assert(!r2_group_control_decode14(&parsed,wire,r2_control_wire_size(wire),&g,ids,11,name,&caps));
 assert(r2_group_control_decode(&parsed,wire,r2_control_wire_size(wire),&g,ids,11,name,&caps));
 wire[0]=15;size_t old_size=r2_control_wire_size(wire);
 assert(!r2_group_mac(wire+old_size-16,&g,"PTOBG3C3",wire,old_size-16));
 assert(r2_group_control_decode14(&parsed,wire,old_size,&g,ids,11,name,&caps));
 puts("V14 cached membership exact10s, ACL reconnect proof separation and unqualified takeover refusal PASS");
}
