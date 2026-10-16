#include "network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 struct r2_network n;assert(!r2_network_init(&n,1,2,22,200,99,40000,0));
 n.group=(struct r2_group_config){.psk={1},.id=123,.epoch=1};
 struct r2_join_exchange e={.accepted=true,.control=true,.peer=11,.peer_boot=100,.nonce=15,.remote_nonce=30,.peer_capabilities=3};strcpy(e.name,"Peer Clock");
 r2_network_joined(&n,0,&e,100000);struct r2_net_peer *p=&n.peer[0];n.peer[1].identity=22;
 assert(p->authenticated&&p->member&&!p->map_valid&&!p->candidate_until);
 assert(!strcmp(p->name,"Peer Clock"));
 /* An authenticated identity without a causal offset cannot grant SERVICE. */
 uint64_t ids[4]={11,22};uint8_t wire[R2_GROUP_CONTROL_MAX];
 struct r2_auth_record r={.kind=R2_AUTH_ACK,.sender=0,.target=1,.root=0,.boot=100,.coordinator_boot=100,.root_boot=100,.challenge=15,.sequence=1,.decision=1,.source_epoch=1,.config_epoch=R2_AUTH_CONFIG,.created_us=100000,.evidence_us=100000,.expires_us=350000};
 assert(!r2_group_control_encode14(wire,&r,&n.group,ids,"",0));
 assert(r2_network_receive(&n,0,wire,r2_control_wire_size(wire),102000,200000));assert(!p->map_valid);
 /* 98ms control queue delay is legitimate when original TTL is still live. */
 r.kind=R2_AUTH_HELLO;r.root=255;r.root_boot=200;r.challenge=30;r.decision=15;r.source_epoch=0;r.source_second=100000;
 p->sent[0]=100000;
 assert(!r2_group_control_encode14(wire,&r,&n.group,ids,"Peer Clock",3));
 assert(!r2_network_receive(&n,0,wire,r2_control_wire_size(wire),102000,200000));
 assert(p->authenticated&&p->map_valid&&p->member&&p->map_anchor==100000);
 assert(r2_network_receive(&n,0,wire,r2_control_wire_size(wire),102000,200001));
 /* Backlog policy cannot revive a packet that expired in its original map. */
 r.sequence=2;r.created_us=r.evidence_us=r.source_second=300000;r.expires_us=550000;p->sent[1]=300000;
 assert(!r2_group_control_encode14(wire,&r,&n.group,ids,"Peer Clock",3));
 assert(r2_network_receive(&n,0,wire,r2_control_wire_size(wire),302000,600000));
 assert(p->hello_sequence==1&&p->member_at==102000);
 /* Wrong boot and reconnect never inherit the JOIN proof or causal map. */
 r.boot=101;r.sequence=3;r.created_us=r.evidence_us=600000;r.expires_us=850000;r.decision=16;
 assert(!r2_group_control_encode14(wire,&r,&n.group,ids,"Peer Clock",3));
 assert(r2_network_receive(&n,0,wire,r2_control_wire_size(wire),601000,602000));
 r2_network_link(&n,0,false,603000);assert(!p->authenticated&&!p->map_valid&&r2_network_member(&n,0));
 puts("Control delayed98ms/expired298ms/replay/wrongboot, JOIN identity without map, reconnect proof separation PASS");
}
