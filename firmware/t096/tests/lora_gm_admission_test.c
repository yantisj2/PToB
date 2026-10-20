/* Real discovery/JOIN owner with a fake immutable LoRa publication only.
 * Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define R2_LORA 1
#include "lora_service.h"
static struct r2_gm_coordination observed;
void r2_lora_coordination(struct r2_gm_coordination *out){*out=observed;}
#define main existing_route_fixture_main
#include "auto_route_test.c"
#undef main
int main(void){
 const uint64_t now=1100000;
 struct r2_gm_coordination c={.root=111,.boot=1000,.epoch=1,.until=13000000};
 for(unsigned fail=0;fail<16;fail++){
  reset();client=(struct r2_group_client){.source=true,.peer=111,.peer_boot=1000,.root=111,.boot=1000,.source_epoch=1,.at=1000000,.until=2000000};
  timing_context=client;timing_context.map_until=1500000;observed=c;
  switch(fail){
   case 0:break;case 1:client.peer=222;break;case 2:client.peer_boot++;break;case 3:client.root++;break;
   case 4:client.boot++;break;case 5:client.source_epoch++;break;case 6:client.source=false;break;
   case 7:client.until=now;break;case 8:timing_context.source=false;break;case 9:timing_context.peer=222;break;
   case 10:timing_context.boot++;break;case 11:timing_context.source_epoch++;break;
   case 12:timing_context.map_until=now;break;case 13:timing_context.until=now;break;
   case 14:observed.boot=0;break;case 15:observed.until=now;break;
  }
  assert(r2_group_admission_coordination_direct(&observed,now)==(fail==0));
 }
 reset();observed=c;struct r2_network n={.identity=333,.boot=33,.capabilities=3,.map_bound_us=40000,.group={.id=7}};
 client=(struct r2_group_client){.source=true,.passive=true,.peer=222,.peer_boot=2000,.root=222,.boot=2000,.source_epoch=2,.at=1000000,.until=2000000};
 r2_group_admission_tick(&n,now);assert(!client.source&&admission_parent==111&&!admission_seek_relay);
 advertisement(222,false,-20,now);advertisement(444,true,-10,now+1);advertisement(111,false,-100,now+2);
 candidate_select(now+40000);assert(peers[4].identity==111); /* quality/RSSI cannot choose another root */
 /* An already-owned unrelated JOIN may finish authentication, but supplies
  * no selected time; root pinning never rebinds its live callback/controller. */
 reset();observed=c;client.passive=true;exchange=(struct r2_join_exchange){.peer=222,.peer_boot=2000};
 struct r2_join_record r=gm_time(now);r.sender=r.root=222;r.sender_boot=r.root_boot=2000;
 accepted_peer(&n,&r,now-1000,now);assert(!client.source);
 exchange=(struct r2_join_exchange){.peer=111,.peer_boot=1000};r=gm_time(now);
 accepted_peer(&n,&r,now-1000,now);assert(client.source&&client.root==111&&client.peer==111);
 timing_context=client;timing_context.map_until=now+100000;
 assert(r2_group_admission_coordination_direct(&c,now));
 timing_context.peer=222;assert(!r2_group_admission_coordination_direct(&c,now));
 puts("PASS actual JOIN/discovery:16 direct contact fences, canonical root pin, wrong-root refusal and cold direct acquisition; fake controller only");
 return 0;
}
