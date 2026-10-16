/* A previously yielded spare must not resume on timing/HOT expiry alone. */
#define main pair_hint_fixture_main
#include "gm_pair_hint_test.c"
#undef main
static void positive(struct r2_auth_record *r,uint64_t at)
{
 r->sequence++;r->source_second=at-200000;r->created_us=r->evidence_us=at-1000;
 r->expires_us=at+249000;r->permit_until_us=at+1499000;
 r->pair_until_us=r->pair_root_boot=r->pair_standby_boot=0;
 r->pair_root=r->pair_standby=0;r->pair_epoch=0;
}
static void yielded(struct r2_network *n,struct r2_auth_record *r)
{
 setup(n,true);*r=claim(true,false);assert(!receive(n,*r,now));tick(n,now);
 assert(!n->local_gm&&n->choice==1&&n->discovery_pair.root==20);
 /* Age timing separately from the current direct authenticated identity. */
 n->peer[1].seen=n->peer[1].map_anchor=now-20000000;
 n->peer[1].offset_low=-100000;n->peer[1].offset_high=100000;
}
int main(void)
{
 struct r2_network n;struct r2_auth_record r;yielded(&n,&r);
 uint64_t seen=n.peer[1].seen,permit=n.peer[1].candidate_until;
 for(unsigned i=1;i<=25;i++){
  uint64_t at=now+i*1000000;positive(&r,at);if(i==12)r.source_epoch=2;
  assert(!receive(&n,r,at));tick(&n,at);
  assert(!n.local_gm&&!n.hot_ready&&!n.ready&&n.choice!=n.self);
  assert(n.peer[1].seen==seen&&n.peer[1].candidate_until==permit&&!n.pair.until&&n.management_until==0);
 }
 /* A brief direct-primary gap releases inhibition, but must not erase yield
  * history before the original promotion fence finishes. Its return can still
  * cancel that pending promotion without recovering any timing authority. */
 yielded(&n,&r);positive(&r,now+1000000);assert(!receive(&n,r,now+1000000));tick(&n,now+1000000);
 advance(&n,now+3000000);assert(!n.local_gm&&n.choice==n.self);
 positive(&r,now+4000000);assert(!receive(&n,r,now+4000000));tick(&n,now+4000000);
 for(unsigned i=5;i<=12;i++){
  positive(&r,now+i*1000000);assert(!receive(&n,r,now+i*1000000));tick(&n,now+i*1000000);
  assert(!n.local_gm&&!n.hot_ready&&!n.ready&&n.choice!=n.self);
 }
 /* Genuine silence still completes the same fence, then discards old yield
  * history. Later alive-only evidence cannot interrupt the new primary. */
 advance(&n,now+18000001);assert(n.local_gm&&!n.discovery_pair.root);
 positive(&r,now+18000002);assert(!receive(&n,r,now+18000002));tick(&n,now+18000002);
 assert(n.local_gm&&n.choice==n.self&&!n.hot_ready);
 /* A surviving HOT identity alone cannot immortalize an undiscovered root. */
 setup(&n,false);r=claim(false,true);assert(!receive(&n,r,now));tick(&n,now);
 for(unsigned i=1;i<=12;i++){uint64_t at=now+i*1000000;positive(&r,at);assert(!receive(&n,r,at));tick(&n,at);}
 assert(n.local_gm&&!n.hot_ready&&!n.discovery_pair.root);
 /* A worse pair hint that our serving primary ignored is not yield history.
  * Losing only our HOT reservation later must not interrupt this primary. */
 setup(&n,true);n.count=3;r2_network_capabilities(&n,7,7);n.coordinator=2;n.coordinator_boot=300;
 n.peer[2]=n.peer[1];n.peer[2].identity=n.peer[2].quality.identity=5;n.peer[2].boot=300;
 n.pair=(struct r2_pair){200,300,now+5000000,1,0,2};r=claim(true,false);
 assert(!receive(&n,r,now));tick(&n,now);assert(n.local_gm&&n.choice==0);
 n.pair.until=now;n.peer[2].candidate_until=0;
 n.peer[1].seen=n.peer[1].map_anchor=now-20000000;
 n.peer[1].offset_low=-100000;n.peer[1].offset_high=100000;
 positive(&r,now+1);assert(!receive(&n,r,now+1));tick(&n,now+1);
 assert(n.local_gm&&n.choice==0&&!n.hot_ready);
 /* Real direct-primary withdrawal/loss releases suppression through the
  * same3s role fence, rather than being kept alive by historical HOT identity. */
 for(unsigned loss=0;loss<4;loss++){
  yielded(&n,&r);uint64_t at=now+1000000;positive(&r,at);assert(!receive(&n,r,at));tick(&n,at);
  if(loss==0){positive(&r,at+1);r.root=255;r.flags=0;r.source_epoch=0;r.permit_until_us=0;assert(!receive(&n,r,at+1));at++;}
  if(loss==1)n.peer[1].boot++;
  if(loss==2)r2_network_link(&n,1,false,at+1),at++;
  if(loss==3){advance(&n,at+2000000);at+=2000000;} /* own GNSS keeps updating */
  tick(&n,at);assert(!n.local_gm&&!n.hot_ready);
  advance(&n,at+3000001);printf("release loss=%u root=%u local=%u fence=%llu now=%llu admission=%u hint=%llu\n",loss,n.choice,n.local_gm,(unsigned long long)n.fence,(unsigned long long)n.now,n.admission.eligible,(unsigned long long)n.discovery_pair.root);fflush(stdout);assert(n.local_gm&&!n.hot_ready);
 }
 puts("PASS previously yielded lowest-ID spare stays passive beyond all map/source/HOT/pair expiry while exact direct primary lives; same-boot epoch change retains yield; real negative/boot/disconnect/silence releases original fence");
 return 0;
}
