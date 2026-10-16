/* Actual-body expired role preference must not split two serving partitions. */
#define main pair_hint_fixture_main
#include "gm_pair_hint_test.c"
#undef main
static void pair_fixture(struct r2_network *n,bool reserved)
{
 setup(n,true);n->count=3;r2_network_capabilities(n,7,7);
 n->choice=1;n->coordinator=0;n->root_boot=100;n->coordinator_boot=200;
 n->ready=n->local_gm=n->hot_ready=false;n->timing_valid=false;
 n->pair=(struct r2_pair){100,200,reserved?now+5000000:now-1,1,1,0};
 n->peer[1].alive=n->root_health_at=now-1;
 n->peer[1].serving_until=now+1000000;n->peer[1].serving_epoch=1;
 n->peer[2]=n->peer[1];n->peer[2].identity=n->peer[2].quality.identity=5;
 n->peer[2].boot=300;n->peer[2].sequence=0;n->peer[2].serving_until=0;
}
static void serving(struct r2_network *n)
{
 struct r2_auth_record r={.kind=R2_AUTH_SERVICE,.sender=2,.target=0,.root=2,
  .boot=300,.root_boot=300,.coordinator_boot=300,.challenge=11,.sequence=1,.decision=7,
  .source_epoch=1,.source_second=1791504000,.evidence_us=now-1000,.created_us=now-1000,
  .expires_us=now+249000,.permit_until_us=now+1000000,.config_epoch=R2_AUTH_CONFIG,.flags=1};
 uint64_t ids[4]={10,20,5,0};uint8_t wire[R2_GROUP_CONTROL_MAX];
 assert(!r2_group_control_encode14(wire,&r,&n->group,ids,"other-serving",3));
 assert(!r2_network_receive(n,2,wire,r2_control_wire_size(wire),now,now));
 assert(n->peer[2].serving_until>now&&!n->ready&&!n->hot_ready);
}
int main(void)
{
 struct r2_network n;
 /* A live original reservation retains its qualified pair preference. */
 pair_fixture(&n,true);serving(&n);tick(&n,now);assert(n.choice==1&&!n.local_gm&&!n.hot_ready);
 /* A source-capable spare or expired/epoch-mismatched service is not an
  * actual competing incumbent and cannot preempt the former pair. */
 for(unsigned kind=0;kind<3;kind++){
  pair_fixture(&n,false);if(kind)serving(&n);
  if(kind==1)n.peer[2].serving_until=now;
  if(kind==2)n.peer[2].serving_epoch=2;
  tick(&n,now);assert(n.choice==1&&!n.local_gm&&!n.hot_ready);
 }
 /* Past-HOT identity preference is not a live HOT reservation. A fresh
  * authenticated lower-ID serving GM must win the ordinary deterministic
  * partition tie even when both old partner identities remain alive. */
 pair_fixture(&n,false);serving(&n);tick(&n,now);
 assert(n.choice==2&&!n.local_gm&&!n.hot_ready&&!n.timing_valid);
 assert(n.pair.until<=now);
 puts("PASS live reservation preserved; spare/expired proof cannot preempt; expired old pair yields to authenticated serving lower-ID GM without HOT or precision");
 return 0;
}
