/* SPDX-License-Identifier: Apache-2.0 */
#include "group_join.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
 struct r2_join_time anchor={.utc_us=1791504000000000ULL,.until=1500000,.uncertainty_us=40000};uint64_t upper;
 assert(r2_group_coarse_upper(&anchor,1000000,3000000,&upper));assert(upper==anchor.utc_us+2044100);
 assert(anchor.until==1500000); /* anchor reuse never renews permission */
 assert(!r2_group_coarse_upper(&anchor,1000000,999999,&upper));
 assert(!r2_group_coarse_upper(&anchor,1000000,106000000,&upper));

 struct r2_group_config g={.psk={1,2,3},.id=7,.epoch=2},wrong=g;wrong.psk[3]=1;
 struct r2_join_exchange client,server;
 assert(!r2_group_join_begin(&client,false,100,200,1000,111,1000000));
 assert(!r2_group_join_begin(&server,true,200,100,2000,222,1100000));
 client.control=true;client.request_at=1010000;
 struct r2_join_record r={.kind=R2_JOIN_REQUEST,.capabilities=3,.control=1,.sender=100,.receiver=200,.sender_boot=1000,.client_nonce=111,.sequence=1,.created_us=client.request_at,.name="Same name"},x;
 uint8_t wire[R2_GROUP_JOIN_BYTES];assert(!r2_group_join_encode(wire,&r,&g));assert(!r2_group_join_decode(&x,wire,R2_GROUP_JOIN_BYTES,&g));
 assert(r2_group_join_decode(&x,wire,R2_GROUP_JOIN_BYTES,&wrong));
 for(unsigned i=0;i<R2_GROUP_JOIN_BYTES;i++){wire[i]^=1;assert(r2_group_join_decode(&x,wire,R2_GROUP_JOIN_BYTES,&g));wire[i]^=1;}
 assert(!r2_group_join_receive(&server,&r,1110000));assert(!server.proved);
 server.challenge_at=1120000;
 r=(struct r2_join_record){.kind=R2_JOIN_CHALLENGE,.capabilities=3,.control=1,.sender=200,.receiver=100,.sender_boot=2000,.receiver_boot=1000,.client_nonce=111,.server_nonce=222,.sequence=1,.created_us=server.challenge_at,.echo_us=client.request_at,.name="Same name"};
 struct r2_join_exchange no_control=client;no_control.control=false;assert(r2_group_join_receive(&no_control,&r,1040000));
 assert(!r2_group_join_receive(&client,&r,1040000));assert(client.challenged&&!client.accepted);
 assert(r2_group_join_receive(&client,&r,1041000));
 client.proof_at=1050000;
 r=(struct r2_join_record){.kind=R2_JOIN_PROOF,.capabilities=3,.control=1,.sender=100,.receiver=200,.sender_boot=1000,.receiver_boot=2000,.client_nonce=111,.server_nonce=222,.sequence=2,.created_us=client.proof_at,.echo_us=server.challenge_at,.name="Renamed client"};
 struct r2_join_record bad=r;bad.server_nonce++;assert(r2_group_join_receive(&server,&bad,1140000));
 assert(!r2_group_join_receive(&server,&r,1140000));assert(server.proved&&!strcmp(server.name,"Renamed client"));
 assert(r2_group_join_receive(&server,&r,1141000));
 r=(struct r2_join_record){.kind=R2_JOIN_ACCEPT,.capabilities=3,.control=1,.sender=200,.receiver=100,.sender_boot=2000,.receiver_boot=1000,.client_nonce=111,.server_nonce=222,.sequence=2,.created_us=1150000,.echo_us=client.proof_at,.name="Same name"};
 bad=r;bad.receiver_boot++;assert(r2_group_join_receive(&client,&bad,1070000));
 assert(!r2_group_join_receive(&client,&r,1070000));assert(client.accepted&&!r.source_flags);
 assert(r2_group_join_receive(&client,&r,1070001));
 /* Reboot/link replacement changes the challenge even with identical name. */
 assert(!r2_group_join_begin(&server,true,200,100,2001,223,2000000));
 bad=(struct r2_join_record){.kind=R2_JOIN_PROOF,.sender=100,.receiver=200,.sender_boot=1000,.receiver_boot=2000,.client_nonce=111,.server_nonce=222,.sequence=2,.created_us=1050000,.echo_us=1120000,.name="Renamed client"};
 assert(r2_group_join_receive(&server,&bad,2001000));
 assert(r2_group_join_receive(&server,&bad,7000000));
 struct r2_join_time time;
 r.source_flags=1;r.source_bound_us=40000;r.root=200;r.root_boot=2000;r.source_epoch=1;r.publication=1;r.utc_us=1791417600000000ULL;r.remaining_us=1000000;
 assert(r2_group_join_time(&time,&r,1000000,1020000,40000));
 assert(time.until==1959896&&time.uncertainty_us==50104);
 assert(r2_group_join_time(&time,&r,1000000,1070000,40000));
 assert(time.until==1959796&&time.uncertainty_us==75204);
 assert(time.utc_us==r.utc_us+35000);
 r.source_bound_us=45000;assert(r2_group_join_time(&time,&r,1000000,1020000,40000));
 assert(time.uncertainty_us==55104&&time.until==1954896); /* advertised aged-map bound never narrowed */
 r.source_bound_us=40000;
 /* Original source permission can expire during an otherwise valid echo. */
 r.remaining_us=50000;assert(!r2_group_join_time(&time,&r,1000000,1020000,40000));
 r.remaining_us=1000000;assert(!r2_group_join_time(&time,&r,1000000,1200000,40000));
 assert(!r2_group_join_time(&time,&r,1000000,1300000,40000));
 r.source_flags=0;r.source_bound_us=0;r.root=r.root_boot=r.utc_us=0;r.source_epoch=r.publication=r.remaining_us=0;
 assert(!r2_group_join_time(&time,&r,1000000,1020000,40000));
 struct r2_join_display cache[8]={0};
 for(unsigned i=0;i<12;i++){struct r2_join_exchange member={.peer=100+i,.peer_boot=1000+i,.proved=true,.name="Duplicate name"};r2_group_join_remember(cache,&member,10000+i);}
 for(unsigned i=0;i<8;i++)assert(cache[i].identity>=104);
 /* Eviction is display-only: an old ID still starts a fresh authorized join
  * and has no new recipient index or enrollment requirement. */
 assert(!r2_group_join_begin(&server,true,200,100,2001,999,3000000));
 struct r2_join_record fresh={.kind=R2_JOIN_REQUEST,.sender=100,.receiver=200,.sender_boot=1001,.client_nonce=777,.sequence=1,.created_us=2000000,.name="Returns after eviction"};
 assert(!r2_group_join_receive(&server,&fresh,3001000));
 server.challenge_at=3002000;fresh.kind=R2_JOIN_PROOF;fresh.receiver_boot=2001;fresh.server_nonce=999;fresh.sequence=2;fresh.echo_us=server.challenge_at;
 struct r2_join_exchange delayed=server;
 assert(!r2_group_join_receive(&delayed,&fresh,4000000)); /* auth at998ms, no UTC */
 assert(r2_group_join_receive(&delayed,&fresh,4000001)); /* consumed proof */
 delayed=server;assert(r2_group_join_receive(&delayed,&fresh,8000000)); /* original5s */
 fresh.demand=1;fresh.root=fresh.root_boot=0;fresh.source_epoch=fresh.publication=0;
 assert(!r2_group_join_encode(wire,&fresh,&g));assert(wire[2]==4);
 wire[2]=3;assert(r2_group_join_decode(&x,wire,sizeof wire,&g));wire[2]=4;
 assert(!r2_group_join_receive(&server,&fresh,3004000));assert(server.proved);
 r2_group_join_remember(cache,&server,3004000);bool returned=false;for(unsigned i=0;i<8;i++)returned|=cache[i].identity==100;assert(returned);
 puts("PASS transient admission: fixed wire, wrong PSK, every-byte corruption, dual fresh challenge, duplicate name, submitted rename, replay/reboot/timeout fencing, membership without source authority");
}
