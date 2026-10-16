/* Production network tick at the JOIN-authenticated/HELLO-unmapped boundary.
 * Synthetic source/session facts are explicit; no physical qualification. */
#include "network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint64_t start=65000000;
static struct r2_clock source(uint64_t now){
 struct r2_clock c={0};c.view.source_available=true;c.view.source_epoch=1;
 c.last_pair_edge=c.last_rmc=(int64_t)now;c.last_pair_utc=1791504000000000LL;
 return c;
}
static void setup(struct r2_network *n){
 assert(!r2_network_init(n,0,2,20,200,123,40000,0));
 n->group=(struct r2_group_config){.psk={1},.id=7,.epoch=9};n->capabilities=3;
 r2_network_capabilities(n,3,3);n->relative_source=true;
 n->choice=0;n->root_boot=200;n->fence=start-1;
 n->admission=(struct r2_source_admission){.boot_at=0,.stable_at=0,.now=start-1,
  .last_expiry=start+1000000,.source_epoch=1,.observing=true,.eligible=true};
 struct r2_net_peer *p=&n->peer[1];p->identity=10;p->boot=100;
 p->connected=p->authenticated=p->member=true;p->capabilities=3;
 p->seen=p->alive=p->member_at=start-1000;p->quality=n->quality;p->quality.identity=10;
}
static void tick(struct r2_network *n,uint64_t now){struct r2_clock c=source(now);r2_network_tick(n,&c,now);}
int main(void){
 struct r2_network n;setup(&n);tick(&n,start);
 printf("JOIN authenticated, causal candidate unknown: local_gm=%u ready=%u\n",n.local_gm,n.ready);fflush(stdout);
 assert(!n.local_gm&&!n.ready); /* Rejected baseline promoted here. */
 n.peer[1].map_valid=true;n.peer[1].candidate_until=start+1000000;n.peer[1].candidate_epoch=1;
 tick(&n,start+1000);assert(n.choice==1&&!n.local_gm); /* Fresh proof elects by quality/identity. */
 setup(&n);n.peer[1].map_valid=true;n.peer[1].source_withdrawn=true;tick(&n,start);
 assert(n.local_gm); /* Mapped negative source is resolved, not a permanent veto. */
 setup(&n);n.peer[1].map_valid=true;n.peer[1].candidate_until=start-1;tick(&n,start);
 assert(n.local_gm); /* Expired known source retains original takeover behavior. */
 setup(&n);n.peer[1].seen=start-10000000;tick(&n,start);
 assert(n.local_gm&&n.peer[1].seen==start-10000000); /* Silent HELLO proof cannot refresh10s identity bound. */
 setup(&n);n.peer[1].connected=false;tick(&n,start);assert(n.local_gm);
 setup(&n);n.local_gm=n.ready=true;n.service_until=start+1000000;tick(&n,start);assert(n.local_gm);
 /* Real prior HOT root loss keeps its original suspicion and3s output fence. */
 setup(&n);n.choice=1;n.coordinator=0;n.root_boot=100;n.fence=0;
 n.peer[1].map_valid=true;n.peer[1].candidate_until=start-1;
 n.pair=(struct r2_pair){.root=1,.standby=0,.root_boot=100,.standby_boot=200,.until=start+10000000,.epoch=1};
 n.standby_authorized=true;n.standby_root_boot=100;n.standby_source_epoch=1;n.root_health_at=start-2000000;
 n.peer[1].alive=n.root_health_at; /* Genuine root silence, not a timing-permit gap. */
 tick(&n,start);assert(n.takeover_pending&&n.takeover_not_before==start+3000000&&!n.local_gm);
 for(uint64_t now=start+250000;now<start+3000000;now+=250000){tick(&n,now);assert(!n.local_gm);}
 tick(&n,start+3000000);assert(n.local_gm);
 /* An unrelated third JOIN must not veto an already authorized HOT takeover.
  * It remains unmapped throughout the original suspicion+output fence. */
 setup(&n);n.count=3;r2_network_capabilities(&n,7,7);
 n.choice=1;n.coordinator=0;n.root_boot=100;n.fence=0;
 n.peer[1].map_valid=true;n.peer[1].candidate_until=start-1;
 n.pair=(struct r2_pair){.root=1,.standby=0,.root_boot=100,.standby_boot=200,.until=start+10000000,.epoch=1};
 n.standby_authorized=true;n.standby_root_boot=100;n.standby_source_epoch=1;n.root_health_at=start-2000000;
 n.peer[1].alive=n.root_health_at;
 n.peer[2]=n.peer[1];n.peer[2].identity=30;n.peer[2].boot=300;n.peer[2].map_valid=false;
 n.peer[2].alive=start; /* Fresh JOIN still cannot veto legitimate prior HOT. */
 tick(&n,start);assert(n.takeover_pending&&n.takeover_not_before==start+3000000&&!n.local_gm);
 for(uint64_t now=start+250000;now<start+3000000;now+=250000){tick(&n,now);assert(!n.local_gm);}
 tick(&n,start+3000000);assert(n.local_gm);
 /* Reservation expiry is not evidence that either qualified established
  * partner disappeared. A lower-ID newcomer may not replace the live standby. */
 setup(&n);n.count=3;r2_network_capabilities(&n,7,7);n.choice=1;n.root_boot=100;
 n.coordinator=0;n.pair=(struct r2_pair){.root=1,.standby=0,.root_boot=100,.standby_boot=200,.until=start,.epoch=1};
 n.peer[1].map_valid=true;n.peer[1].candidate_until=start+1000000;n.peer[1].candidate_epoch=1;
 n.peer[1].serving_until=start+1000000;n.peer[1].serving_epoch=1;
 n.peer[2]=n.peer[1];n.peer[2].identity=n.peer[2].quality.identity=5;n.peer[2].boot=300;n.peer[2].serving_until=0;
 tick(&n,start);printf("expired reservation, both original partners eligible: root=%u standby=%u\n",n.choice,n.coordinator);fflush(stdout);
 assert(n.choice==1&&n.coordinator==0&&n.pair.root==1&&n.pair.standby==0&&n.pair.until==start);
 /* Timing permission expiry alone retains live partner preference and cannot
  * claim HOT. An explicit source withdrawal really releases that preference. */
 n.peer[1].candidate_until=start;n.peer[1].serving_until=0;n.pair.until=start;
 tick(&n,start+1);assert(n.choice==1&&n.coordinator==0&&!n.ready&&!n.hot_ready&&n.pair.until==start);
 n.peer[1].source_withdrawn=true;
 tick(&n,start+2);assert(n.choice==0&&n.coordinator==2&&!n.pair.until&&n.pair.root==255); /* Surviving qualified old standby takes over. */
 /* Management acknowledgements with independent sources are not HOT while
  * precision timing is unsynced. They cannot manufacture a pair reservation. */
 setup(&n);n.choice=1;n.root_boot=100;n.coordinator=0;n.fence=0;
 n.peer[1].map_valid=true;n.peer[1].candidate_until=start+1000000;n.peer[1].candidate_epoch=1;
 n.view.available=true;n.view.generation=1;n.view.valid_until_us=start+1000000;
 n.service_until=start+1000000;n.publication=1;n.timing_valid=false;
 tick(&n,start);assert(n.ready&&n.management_hash&&n.coordinator==0&&!n.hot_ready);
 n.management_ready=3;n.management_committed=true;
 tick(&n,start+1);printf("acknowledged management, unsynced: hot=%u pair_until=%llu\n",n.hot_ready,(unsigned long long)n.pair.until);fflush(stdout);
 assert(n.management_committed&&!n.hot_ready&&!n.pair.until);
 n.timing_valid=true;n.timing_boot=100;n.timing_epoch=1;n.timing_train=1;n.timing_at=start+2;
 tick(&n,start+2);assert(n.hot_ready&&n.pair.root==1&&n.pair.standby==0&&n.pair.root_boot==100&&n.pair.standby_boot==200&&n.pair.epoch==1);
 uint64_t reserve=n.pair.until;n.timing_valid=false;tick(&n,start+1000);
 assert(!n.hot_ready&&n.pair.until==reserve); /* Prior actual reservation stays original, not renewed on unsynced management. */

 /* A fresh boot/connection incarnation must supply its own source map. */
 setup(&n);n.peer[1].map_valid=true;r2_network_link(&n,1,false,start);
 struct r2_join_exchange join={.accepted=true,.control=true,.peer=10,.peer_boot=101,
  .nonce=10,.remote_nonce=20,.peer_capabilities=3};
 r2_network_joined(&n,1,&join,start+1);tick(&n,start+2);
 assert(!n.peer[1].map_valid&&!n.local_gm&&n.peer[1].boot==101);
 setup(&n);struct r2_clock c={0};r2_network_tick(&n,&c,start);assert(!n.local_gm&&!n.admission.eligible);
 puts("PASS unresolved JOIN, source proof/negative, known expiry, silence/disconnect, incumbent, prior HOT fence, boot change and own-source withdrawal");
}
