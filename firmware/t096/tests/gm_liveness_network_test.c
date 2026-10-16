/* Existing pair preference is identity liveness, never timing permission. */
#include <errno.h>
#define main pair_hint_fixture_main
#include "gm_pair_hint_test.c"
#undef main
static void no_pair(struct r2_auth_record *r){r->pair_until_us=r->pair_root_boot=r->pair_standby_boot=0;r->pair_root=r->pair_standby=0;r->pair_epoch=0;}
static void fresh(struct r2_auth_record *r,uint64_t at){
 r->sequence++;r->source_second=at-200000;r->created_us=r->evidence_us=at-1000;
 r->expires_us=at+249000;r->permit_until_us=at+1499000;no_pair(r);
}
static void established(struct r2_network *n){
 setup(n,true);n->choice=1;n->coordinator=0;n->root_boot=100;n->coordinator_boot=200;
 n->local_gm=n->ready=false;n->service_until=now-1;
 n->pair=(struct r2_pair){100,200,now-1,1,1,0};
 n->peer[1].candidate_until=now-1;n->peer[1].seen=n->peer[1].map_anchor=now-20000000;
 n->peer[1].alive=n->root_health_at=now-3000000;
 n->standby_authorized=true;n->standby_root_boot=100;n->standby_source_epoch=1;
}
int main(void){
 struct r2_network n;established(&n);struct r2_auth_record r=claim(true,true);no_pair(&r);
 uint64_t seen=n.peer[1].seen,permit=n.peer[1].candidate_until,pair=n.pair.until;
 for(unsigned i=0;i<=12;i++){
  uint64_t at=now+i*1000000;fresh(&r,at);assert(!receive(&n,r,at));
  assert(n.peer[1].alive==at&&n.root_health_at==at);
  assert(n.peer[1].seen==seen&&n.peer[1].candidate_until==permit&&n.peer[1].map_valid);
  tick(&n,at);assert(n.choice==1&&n.coordinator==0&&!n.local_gm&&!n.ready&&!n.hot_ready&&!n.takeover_pending);
  assert(n.pair.root==1&&n.pair.standby==0&&n.pair.until==pair&&n.management_until==0&&n.service_until<=at);
 }
 /* Preference must release on actual liveness loss, source withdrawal,
  * disconnected identity or changed physical incarnation. No old authority
  * is extended, and reselection still uses the existing output fence. */
 for(unsigned fail=0;fail<4;fail++){
  established(&n);r=claim(true,true);no_pair(&r);assert(!receive(&n,r,now));tick(&n,now);
  uint64_t at=now+1;
  if(fail==0)at=now+2000001;
  if(fail==1){fresh(&r,at);r.root=255;r.flags=0;r.source_epoch=0;r.permit_until_us=0;assert(!receive(&n,r,at));}
  if(fail==2)r2_network_link(&n,1,false,at);
  if(fail==3)n.peer[1].boot++;
  tick(&n,at);assert(n.choice!=1&&!n.local_gm&&!n.hot_ready&&!n.ready);
 }
 /* An old accepted map must not veto a fresh current causal proof. The
  * current wide interval proves only liveness; its timing map is still refused. */
 established(&n);r=claim(true,true);uint64_t late=now+300000000;fresh(&r,late);
 seen=n.peer[1].seen;permit=n.peer[1].candidate_until;
 assert(!receive(&n,r,late));assert(n.peer[1].alive==late&&n.root_health_at==late);
 assert(n.peer[1].seen==seen&&n.peer[1].candidate_until==permit&&!n.hot_ready&&!n.ready);
 /* A late authenticated positive HELLO is still refused for all authority.
  * Its conservatively mapped creation can prove only past identity activity;
  * receipt time must never rearm the original2s failure suspicion. */
 established(&n);r=claim(true,true);no_pair(&r);r.source_second=now-600000;
 r.created_us=r.evidence_us=now-400000;r.expires_us=now-150000;r.permit_until_us=now+1100000;
 seen=n.peer[1].seen;permit=n.peer[1].candidate_until;pair=n.pair.until;
 assert(receive(&n,r,now)==-ESTALE);uint64_t activity=n.peer[1].alive;
 assert(activity>now-500000&&activity<=r.created_us&&activity==n.root_health_at&&activity<now);
 assert(n.peer[1].seen==seen&&n.peer[1].candidate_until==permit&&n.pair.until==pair&&!n.ready&&!n.hot_ready);
 tick(&n,now);assert(n.choice==1&&!n.takeover_pending&&!n.local_gm);
 assert(receive(&n,r,now+500000)==-ESTALE);assert(n.peer[1].alive==activity&&n.root_health_at==activity);
 advance(&n,activity+2000000);assert(n.choice!=1&&!n.local_gm&&!n.hot_ready);
 /* Old/replayed/future/negative/session/epoch evidence cannot postpone a
  * dead or explicitly withdrawn root. Future means beyond the current
  * accepted map's conservative lower bound, not a remote clock ahead. */
 for(unsigned fail=0;fail<8;fail++){
  established(&n);r=claim(true,true);no_pair(&r);r.source_second=now-600000;
  r.created_us=r.evidence_us=now-400000;r.expires_us=now-150000;r.permit_until_us=now+1100000;
  if(fail==0){r.created_us=r.evidence_us=now-3000000;r.expires_us=r.created_us+250000;r.permit_until_us=r.created_us+1500000;}
  if(fail==1){r.created_us=r.evidence_us=now+1000000;r.expires_us=r.created_us+250000;r.permit_until_us=r.created_us+1500000;}
  if(fail==2){r.root=255;r.flags=0;r.source_epoch=0;r.permit_until_us=0;}
  if(fail==3)n.peer[1].source_withdrawn=true;
  if(fail==4)r.boot=r.coordinator_boot=101;
  if(fail==5)r.challenge++;
  if(fail==6)r.decision++;
  if(fail==7)r.source_epoch++;
  uint64_t before=n.peer[1].alive,health=n.root_health_at;(void)receive(&n,r,now);
  assert(n.peer[1].alive==before&&n.root_health_at==health);
  tick(&n,now);assert(n.choice!=1&&!n.local_gm&&!n.hot_ready);
 }
 /* Two unpaired actual serving GMs resolve using existing authenticated
  * SERVICE evidence, without any delivered precision/P1 or HOT readiness. */
 setup(&n,true);strcpy(n.name,"local");n.peer[1].identity=n.peer[1].quality.identity=5;
 n.choice=0;n.root_boot=200;n.timing_valid=n.hot_ready=false;n.publication=8;
 n.now=now;n.view=(struct v12_dc_coarse){.available=true,.generation=1,.utc_second=1791504000,.evidence_us=now-1000,.valid_until_us=now+1000000};
 n.peer[1].acked_publication=8;n.peer[1].acked_permit=now+1000000;
 struct v12_dc_snapshot status;r2_network_snapshot(&n,&status);assert(status.serving);
 r=(struct r2_auth_record){.kind=R2_AUTH_SERVICE,.sender=1,.target=0,.root=1,
  .boot=100,.root_boot=100,.coordinator_boot=100,.challenge=11,.sequence=1,.decision=7,
  .source_epoch=1,.source_second=1791504000,.evidence_us=now-1000,.created_us=now-1000,
  .expires_us=now+249000,.permit_until_us=now+1000000,.config_epoch=R2_AUTH_CONFIG,.flags=1};
 uint64_t ids[4]={10,5,0,0};uint8_t wire[R2_GROUP_CONTROL_MAX];
 assert(!r2_group_control_encode14(wire,&r,&n.group,ids,"peer",3));
 assert(!r2_network_receive(&n,1,wire,r2_control_wire_size(wire),now,now));
 assert(n.peer[1].serving_until>now&&n.peer[1].serving_epoch==1&&!n.timing_valid&&!n.hot_ready);
 tick(&n,now);assert(n.choice==1&&!n.local_gm&&!n.hot_ready&&!n.timing_valid);
 puts("PASS >10s fresh rejected-map liveness preserves expired pair preference without authority; backdated activity never renews authority or receipt-stamps replay; negative/boot/disconnect/expiry releases; unpaired serving tie without HOT/precision");
}
