/* Actual production receive/tick: authenticated pair discovery only withdraws
 * output. Synthetic sessions/maps are explicit, never physical evidence. */
#include "network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint64_t now=90000000;
static void setup(struct r2_network *n,bool mapped_sender){
 assert(!r2_network_init(n,0,2,10,200,123,40000,0));
 n->group=(struct r2_group_config){.psk={1},.id=7,.epoch=9};n->capabilities=3;
 r2_network_capabilities(n,3,3);n->relative_source=true;
 n->choice=0;n->root_boot=200;n->local_gm=n->ready=true;n->service_until=now+1000000;n->fence=0;
 n->admission=(struct r2_source_admission){.boot_at=0,.stable_at=0,.now=now-1,
  .last_expiry=now+1000000,.source_epoch=1,.observing=true,.eligible=true};
 struct r2_net_peer *p=&n->peer[1];p->identity=20;p->boot=100;
 p->connected=p->authenticated=p->member=true;p->capabilities=3;p->nonce=22;p->own_nonce=11;
 p->seen=p->alive=p->member_at=now-1000;p->quality=n->quality;p->quality.identity=20;
 p->map_valid=mapped_sender;p->candidate_epoch=1;p->candidate_until=now+1000000;
 p->offset_low=-100;p->offset_high=100;p->map_anchor=now-10000;
}
static struct r2_auth_record claim(bool root,bool wide){
 return (struct r2_auth_record){.kind=R2_AUTH_HELLO,.sender=1,.target=0,.root=1,.flags=1,
  .boot=100,.coordinator_boot=100,.root_boot=200,.challenge=22,.decision=11,.sequence=1,
  .source_epoch=1,.config_epoch=R2_AUTH_CONFIG,.boot_age_ms=60000,.source_age_ms=60000,
  .source_second=now-(wide?200000:10000),.created_us=now-1000,.evidence_us=now-1000,
  .expires_us=now+249000,.permit_until_us=now+1499000,
  .pair_root=root?1:2,.pair_standby=root?2:1,.pair_root_boot=root?100:300,
  .pair_standby_boot=root?300:100,.pair_epoch=1,.pair_until_us=now+9999000};
}
static int receive(struct r2_network *n,struct r2_auth_record r,uint64_t at){
 uint64_t ids[4]={10,20,30,0};uint8_t wire[R2_GROUP_CONTROL_MAX];
 assert(!r2_group_control_encode14(wire,&r,&n->group,ids,"peer",3));
 n->peer[r.sender].sent[0]=r.source_second;
 return r2_network_receive(n,r.sender,wire,r2_control_wire_size(wire),at,at);
}
static void tick(struct r2_network *n,uint64_t at){
 struct r2_clock c={0};c.view.source_available=true;c.view.source_epoch=1;
 c.last_pair_edge=c.last_rmc=at;c.last_pair_utc=1791504000000000LL;r2_network_tick(n,&c,at);
}
static void advance(struct r2_network *n,uint64_t end){
 for(uint64_t at=n->now+250000;at<end;at+=250000)tick(n,at);tick(n,end);
}
int main(void){
 struct r2_network n;setup(&n,true);struct r2_auth_record r=claim(true,false);
 assert(!receive(&n,r,now));assert(n.peer[1].map_valid);tick(&n,now);
 printf("mapped root, unknown standby: local_gm=%u root=%u coordinator=%u\n",n.local_gm,n.choice,n.coordinator);fflush(stdout);
 assert(!n.local_gm&&n.choice==1&&n.coordinator==255&&!n.hot_ready&&!n.pair.until);
 assert(n.discovery_pair.root==20&&n.discovery_pair.standby==30&&n.discovery_pair.until<=now+1499000);
 /* Wide causal echo is sufficient to yield, never to admit a source map. */
 for(unsigned root=0;root<2;root++){
  setup(&n,false);r=claim(root,true);uint64_t seen=n.peer[1].seen;
  n.peer[1].candidate_until=0;n.peer[1].candidate_epoch=0;
  assert(!receive(&n,r,now));assert(!n.peer[1].map_valid&&n.peer[1].seen==seen&&!n.peer[1].candidate_until);
  assert(n.hello_map_refusals==1);tick(&n,now);
  assert(!n.local_gm&&n.choice==255&&n.coordinator==255&&!n.hot_ready&&!n.pair.until);
  uint64_t end=n.discovery_pair.until;assert(end>now&&end<now+1500000);
  advance(&n,end-1);assert(!n.local_gm);tick(&n,end);assert(!n.discovery_pair.until&&!n.local_gm);
  advance(&n,end+2999999);assert(!n.local_gm);tick(&n,end+3000000);assert(!n.local_gm);
  /* The separate unresolved JOIN hold retains its original10s identity bound. */
  advance(&n,seen+10000000);assert(n.local_gm);
 }
 /* Replay/late echo, stale original TTL, and incorrect current session cannot
  * introduce a fresh hint or clock map. */
 for(unsigned fail=0;fail<5;fail++){
  setup(&n,false);r=claim(true,true);n.peer[1].candidate_until=0;n.peer[1].candidate_epoch=0;
  if(fail==0)r.source_second=now-500000;
  if(fail==1)r.expires_us=now-1;
  if(fail==2)r.decision++;
  if(fail==3)r.challenge++;
  if(fail==4)n.peer[1].hello_sequence=r.sequence;
  (void)receive(&n,r,now);assert(!n.discovery_pair.until&&!n.peer[1].map_valid);
 }
 setup(&n,false);r=claim(true,true);assert(!receive(&n,r,now));uint64_t end=n.discovery_pair.until;
 assert(receive(&n,r,now));assert(n.discovery_pair.until==end); /* replay cannot renew */
 /* A changed independent source epoch fences an old wide hint even when
  * a stale pair epoch makes the replacement hint invalid. */
 setup(&n,false);n.peer[1].candidate_until=0;n.peer[1].candidate_epoch=0;
 r=claim(true,true);assert(!receive(&n,r,now));assert(n.discovery_pair.until);
 r.sequence++;r.source_epoch=2;assert(!receive(&n,r,now+1));assert(!n.discovery_pair.until&&!n.peer[1].map_valid);
 /* A fresh positive new epoch remains able to suppress using a wide causal
  * interval, even while its older accepted source map/epoch remain unchanged. */
 setup(&n,true);n.peer[1].seen=n.peer[1].map_anchor=now-50000000;
 n.peer[1].candidate_until=now-1;n.peer[1].candidate_epoch=1;
 r=claim(true,true);r.source_epoch=r.pair_epoch=2;uint64_t old_seen=n.peer[1].seen;
 assert(!receive(&n,r,now));assert(n.hello_map_refusals==1&&n.discovery_pair.until);
 assert(n.peer[1].map_valid&&n.peer[1].candidate_epoch==1&&n.peer[1].candidate_until==now-1&&n.peer[1].seen==old_seen);
 tick(&n,now);assert(!n.local_gm&&n.discovery_pair.epoch==2&&n.choice==255&&!n.hot_ready);
 /* A separately joined root's current negative or changed epoch fences a
  * standby-origin hint, even if that root has never admitted a narrow map. */
 for(unsigned negative=0;negative<2;negative++){
  setup(&n,false);n.count=3;n.peer[2]=n.peer[1];n.peer[2].identity=30;n.peer[2].boot=300;
  n.peer[2].candidate_until=0;n.peer[2].candidate_epoch=0;
  r=claim(false,true);assert(!receive(&n,r,now));assert(n.discovery_pair.until);
  r=claim(true,true);r.sender=r.root=r.pair_root=2;r.boot=r.coordinator_boot=300;
  r.pair_standby=1;r.pair_root_boot=300;r.pair_standby_boot=100;r.source_epoch=2;
  if(negative){r.root=255;r.flags=0;r.source_epoch=0;r.permit_until_us=0;r.pair_until_us=0;
   r.pair_root=r.pair_standby=0;r.pair_root_boot=r.pair_standby_boot=0;r.pair_epoch=0;}
  assert(!receive(&n,r,now+1));assert(!n.discovery_pair.until&&!n.peer[2].map_valid);
 }
 /* A MAC and current nonces alone do not make a negative fresh. Neither
  * expired original TTL nor an uncausal echo can clear live wide-only yield
  * evidence, including a hint originally learned from the other standby. */
 for(unsigned other_root=0;other_root<2;other_root++)for(unsigned expired=0;expired<2;expired++){
  setup(&n,false);n.peer[1].candidate_until=0;n.peer[1].candidate_epoch=0;
  if(other_root){n.count=3;n.peer[2]=n.peer[1];n.peer[2].identity=30;n.peer[2].boot=300;}
  r=claim(!other_root,true);assert(!receive(&n,r,now));uint64_t original=n.discovery_pair.until;
  r=claim(true,true);r.sequence=2;r.root=255;r.flags=0;r.source_epoch=0;r.permit_until_us=0;
  r.pair_until_us=0;r.pair_root=r.pair_standby=0;r.pair_root_boot=r.pair_standby_boot=0;r.pair_epoch=0;
  if(other_root){r.sender=2;r.boot=r.coordinator_boot=300;}
  if(expired)r.expires_us=now-1;else r.source_second=now-500000;
  (void)receive(&n,r,now+1);assert(n.discovery_pair.until==original);
  tick(&n,now+1);assert(n.discovery_pair.until==original&&!n.local_gm);
 }
 /* A fresh JOIN incarnation or remote nonce change cannot retain a hint
  * merely because the physical boot field happens to be unchanged. */
 setup(&n,false);r=claim(true,true);assert(!receive(&n,r,now));
 r.sequence++;r.challenge++;assert(!receive(&n,r,now+1));assert(!n.discovery_pair.until);
 setup(&n,false);r=claim(true,true);assert(!receive(&n,r,now));
 struct r2_join_exchange join={.accepted=true,.control=true,.peer=20,.peer_boot=100,.nonce=44,.remote_nonce=55,.peer_capabilities=3};
 r2_network_joined(&n,1,&join,now+1);assert(!n.discovery_pair.until&&!n.peer[1].map_valid);
 /* Current authenticated source negative clears even if a replacement map
  * cannot intersect; physical boot/disconnect and mapped epoch also fence. */
 for(unsigned mapped_sender=0;mapped_sender<2;mapped_sender++){
 setup(&n,mapped_sender);r=claim(true,!mapped_sender);assert(!receive(&n,r,now));
 r.sequence++;r.root=255;r.flags=0;r.source_epoch=0;r.permit_until_us=0;r.pair_until_us=0;
 r.pair_root=r.pair_standby=0;r.pair_root_boot=r.pair_standby_boot=0;r.pair_epoch=0;
 assert(!receive(&n,r,now+1));assert(!n.discovery_pair.until);
 }
 for(unsigned fail=0;fail<3;fail++){
  setup(&n,true);r=claim(true,false);assert(!receive(&n,r,now));
  if(fail==0){r.sequence++;r.source_epoch++;assert(!receive(&n,r,now+1));}
  if(fail==1)n.peer[1].boot++;
  if(fail==2)r2_network_link(&n,1,false,now);
  tick(&n,now+(fail==0));assert(!n.discovery_pair.until);
 }
 puts("PASS mapped/unknown pair; wide yield-only root/standby first; expiry/output fence; late/replay/nonces/TTL; source negative/epoch/boot/disconnect");
 return 0;
}
