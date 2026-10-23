/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual G5 network/JOIN, authenticated wire and physical-tuple precision fitter.
 * Fake RF/clock/controller boundaries, two native control slots and owner budgets.
 * A network ready bit is NEVER fed back as evidence of precision synchronization.
 */
#include "network.h"
#include "radio_precision.h"
#include "gm_diagnostics.h"
#include "owner_budget.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 4
static struct r2_network n[N];static struct r2_radio_precision pt[N];
static bool online[N],source[N],pa[N];static uint32_t epoch[N],packet[N],boots[N];
static int paired[N];static unsigned nodes=4,scenario;static bool split,drop_commit;
static uint64_t attempt_origin[N],attempt_deadline[N],next_send[N];
static uint64_t return_at,return_hot_deadline,phase_anchor,phase_failure;
static unsigned failure_phase;
static uint64_t renewal_deadline[N],renewal_gap[N];
static uint64_t failure_at,contact_origin,active_deadline,hot_deadline,merge_deadline,replacement_deadline;
static uint64_t control_gap_until;
static uint64_t now,join_at[N],next_status[N][N],nonce=1000;
static uint64_t local(unsigned p,uint64_t t){return t+1000000+p*137000;}
static uint64_t identity_offset[N]; /* Optional successive replacement fixture. */
static uint64_t identity(unsigned p){return 100+p+identity_offset[p];}
static bool reach(unsigned a,unsigned b){return online[a]&&online[b]&&(!split||(a<2)==(b<2));}
static uint64_t model_replacement_budget(void)
{
 /* Frozen fixture budget after stale inventory has expired: one serialized
  * inventory round, each bounded inventory identity's CONTACT+ACQUIRE and
  * failed-attempt spacing, old repair/uncertain-grant retirement and stability.
  * The fixture has delivery/PA for qualified survivors; this is no RF promise.
  * Silent unreachable hints cannot renew their original120s cache deadline. */
 return 30000000+R2_GM5_CANDIDATES*(R2_GM5_CONTACT_US+R2_GM5_ACQUIRE_US+5000000)+
  R2_GM5_REPAIR_US+R2_GM5_GRANT_US+R2_GM5_RETIRE_GUARD_US+R2_GM5_STABLE_US+100000;
}
static uint64_t inventory_expiry(void)
{
 uint64_t expiry=now;
 for(unsigned p=0;p<nodes;p++){
  if(!online[p])continue;
  for(unsigned c=0;c<R2_GM5_CANDIDATES;c++){
   uint64_t until=n[p].gm5.candidate[c].until;if(until>local(p,0)&&until-local(p,0)>expiry)expiry=until-local(p,0);
  }
 }
 return expiry;
}
static void init(unsigned p)
{
 memset(&pt[p],0,sizeof pt[p]);packet[p]=0;paired[p]=-1;uint32_t incarnation=++boots[p];
 assert(!r2_network_init(&n[p],0,2,identity(p),10000+p+100*incarnation,50000+p+100*incarnation,40000,local(p,now)));
 n[p].group=(struct r2_group_config){.id=77,.epoch=1,.psk={1}};n[p].capabilities=3;n[p].relative_source=true;n[p].gm5.enabled=true;
 r2_network_capabilities(&n[p],1,1);n[p].peer[0].identity=identity(p);snprintf(n[p].name,sizeof n[p].name,"Synthetic-%u",p);
 if(now)n[p].quality.priority1=1;
 if(scenario==12&&now==350000000)n[p].gm5.generation=100;
 online[p]=source[p]=pa[p]=true;
}
static struct r2_join_record join_record(unsigned a,unsigned b,unsigned kind,const struct r2_join_exchange *client,const struct r2_join_exchange *server)
{
 return (struct r2_join_record){.kind=kind,.capabilities=3,.sender=identity(a),.receiver=identity(b),.sender_boot=n[a].boot,.receiver_boot=kind==R2_JOIN_REQUEST?0:n[b].boot,
 .client_nonce=client->nonce,.server_nonce=kind==R2_JOIN_REQUEST?0:server->nonce,.sequence=kind,.created_us=local(a,now),
 .echo_us=kind==R2_JOIN_CHALLENGE?client->request_at:kind==R2_JOIN_PROOF?server->challenge_at:kind==R2_JOIN_ACCEPT?client->proof_at:0,.name="Synthetic"};
}
static void join(unsigned a,unsigned b)
{
 struct r2_join_exchange c,s;uint8_t wire[R2_GROUP_JOIN_BYTES];struct r2_join_record r;
 assert(!r2_group_join_begin(&c,false,identity(a),identity(b),n[a].boot,++nonce,local(a,now)));
 assert(!r2_group_join_begin(&s,true,identity(b),identity(a),n[b].boot,++nonce,local(b,now)));
 c.request_at=local(a,now);r=join_record(a,b,R2_JOIN_REQUEST,&c,&s);assert(!r2_group_join_encode(wire,&r,&n[a].group));assert(!r2_group_join_decode(&r,wire,sizeof wire,&n[b].group));assert(!r2_group_join_receive(&s,&r,local(b,now)));
 s.challenge_at=local(b,now);r=join_record(b,a,R2_JOIN_CHALLENGE,&c,&s);assert(!r2_group_join_encode(wire,&r,&n[b].group));assert(!r2_group_join_decode(&r,wire,sizeof wire,&n[a].group));assert(!r2_group_join_receive(&c,&r,local(a,now)));
 c.proof_at=local(a,now);r=join_record(a,b,R2_JOIN_PROOF,&c,&s);assert(!r2_group_join_encode(wire,&r,&n[a].group));assert(!r2_group_join_decode(&r,wire,sizeof wire,&n[b].group));assert(!r2_group_join_receive(&s,&r,local(b,now)));
 r=join_record(b,a,R2_JOIN_ACCEPT,&c,&s);assert(!r2_group_join_encode(wire,&r,&n[b].group));assert(!r2_group_join_decode(&r,wire,sizeof wire,&n[a].group));assert(!r2_group_join_receive(&c,&r,local(a,now)));s.accepted=true;
 uint8_t status[R2_GM5_STATUS_BYTES];
 n[a].now=local(a,now)+1;
 assert(!r2_gm5_status_encode(&n[a],&c,0,status));assert(!r2_gm5_status_receive(&n[b],&s,0,status,sizeof status,local(b,now)));
 n[b].now=local(b,now)+2;
 assert(!r2_gm5_status_encode(&n[b],&s,1,status));assert(!r2_gm5_status_receive(&n[a],&c,1,status,sizeof status,local(a,now)));
 n[a].now=local(a,now)+3;
 assert(!r2_gm5_status_encode(&n[a],&c,2,status));assert(!r2_gm5_status_receive(&n[b],&s,2,status,sizeof status,local(b,now)));
 /* A fresh JOIN nonce, target incarnation, MAC and explicit G5 envelope are
  * all required even though STATUS never grants a timing map or takeover. */
 struct r2_network copy=n[b];struct r2_join_exchange stale=s;stale.nonce++;
 assert(r2_gm5_status_receive(&copy,&stale,2,status,sizeof status,copy.now));
 status[2]=4;assert(r2_gm5_status_receive(&copy,&s,2,status,sizeof status,copy.now));
 if(r2_gm5_wants_control(&n[a],identity(b))&&r2_gm5_wants_control(&n[b],identity(a))&&paired[a]<0&&paired[b]<0){
  c.control=s.control=true;r2_network_joined(&n[a],1,&c,n[a].now);r2_network_joined(&n[b],1,&s,n[b].now);
  n[a].gm_mask|=2;n[a].gnss_mask|=2;n[b].gm_mask|=2;n[b].gnss_mask|=2;paired[a]=b;paired[b]=a;
 }
 r2_gm5_poll_completed(&n[a],n[a].now);r2_gm5_poll_completed(&n[b],n[b].now);
}
struct flight {uint64_t due;unsigned a,b;uint8_t data[R2_GROUP_CONTROL_MAX];size_t size;};
static struct flight q[256];static unsigned head,tail,rx_refused,tx_counts[4][14];
static void transport(void)
{
 unsigned received[N]={0};
 while(head<tail&&q[head%256].due<=now){struct flight f=q[head++%256];
  if(!reach(f.a,f.b)||paired[f.b]!=(int)f.a)continue;
  if(received[f.b]++>=R2_OWNER_CONTROL_RX){++rx_refused;continue;}
  int rc=r2_network_receive(&n[f.b],1,f.data,f.size,local(f.b,now),local(f.b,now));
  if(getenv("GM5_DEBUG")&&now>90000000&&now<93000000)fprintf(stderr,"receive t%llu %u->%u kind%u rc%d bound%u map%u hello%u peeruntil%llu ownnonce%llu remote%llu\n",(unsigned long long)now,f.a,f.b,f.data[1],rc,n[f.b].peer[1].authenticated,n[f.b].peer[1].map_valid,n[f.b].hello_rx,(unsigned long long)n[f.b].peer[1].candidate_until,(unsigned long long)n[f.b].peer[1].own_nonce,(unsigned long long)n[f.b].peer[1].nonce);
 }
 for(unsigned p=0;p<nodes;p++)if(online[p]&&paired[p]>=0){
  unsigned other=(unsigned)paired[p];
  if(!reach(p,other)||!r2_gm5_wants_control(&n[p],identity(other))){
   r2_network_link(&n[p],1,false,local(p,now));memset(&n[p].peer[1],0,sizeof n[p].peer[1]);paired[p]=-1;continue;
  }
  if((scenario==16||scenario==17||scenario==18)&&now<next_send[p])continue;
  uint8_t dest,w[R2_GROUP_CONTROL_MAX];bool hello;
  bool sent=r2_network_next_ready(&n[p],2,&dest,w,&hello);
  if(getenv("GM5_DEBUG")&&now>90000000&&now<90030000)fprintf(stderr,"send t%llu p%u yes%u stopped%u wanted%llu id%llu connected%u nonce%llu/%llu role%u\n",(unsigned long long)now,p,sent,n[p].stopped,(unsigned long long)r2_gm5_control_peer(&n[p]),(unsigned long long)n[p].peer[1].identity,n[p].peer[1].connected,(unsigned long long)n[p].peer[1].own_nonce,(unsigned long long)n[p].peer[1].nonce,n[p].gm5.role);
  if(sent){
   if(scenario==16||scenario==17||scenario==18)next_send[p]=now+100000;
   assert(dest==1);r2_network_sent(&n[p],hello,1,w);tx_counts[p][w[1]]++;
   if(drop_commit&&w[1]==R2_AUTH_COMMIT)continue;
   if(now>=200000000&&now<control_gap_until&&(w[1]==R2_AUTH_COMMIT||w[1]==R2_AUTH_READY))continue;
   if(scenario==17&&phase_anchor&&now<=phase_failure&&(w[1]==R2_AUTH_COMMIT||w[1]==R2_AUTH_READY))continue;
   /* Selective management reply loss: ordinary HELLO/SERVICE/ACK and actual
    * PA tuples continue. A1.1s outage exceeds the old independent READY bound. */
   if(scenario==15&&now>=200000000&&now<240000000&&
      (now-200000000)%2000000<1100000&&w[1]==R2_AUTH_READY)continue;
   /* A renewal is delayed by selective losses, not by accepting old packets.
    * Vary burst phase/length while HELLO/SERVICE and actual PA remain fresh. */
   if(scenario==16&&(w[1]==R2_AUTH_COMMIT||w[1]==R2_AUTH_READY)){
    if(now>=200000000&&now<240000000){
     uint64_t t=now-200000000,cycle=t/10000000;
     if(t%10000000<2400000+(cycle%3)*100000)continue;
    }
    if(now>=250000000&&now<300000000)continue; /* Beyond the45s lease. */
   }
   for(unsigned copy=0;copy<((scenario==13&&now>=200000000&&now<240000000)?4u:1u);copy++){assert(tail-head<256);struct flight *f=&q[tail++%256];f->a=p;f->b=other;f->due=now+25000;f->size=r2_control_wire_size(w);memcpy(f->data,w,f->size);}
  }
 }
}
static void precision(void)
{
 for(unsigned p=0;p<nodes;p++)if(online[p]){
  struct r2_radio_scope scope={.boot=n[p].root_boot,.root=n[p].choice,.root_identity=n[p].choice==0?identity(p):paired[p]>=0?identity((unsigned)paired[p]):0,
   .source_epoch=r2_network_source_epoch(&n[p]),.publication_epoch=n[p].publication,.until_us=n[p].service_until,.gm=n[p].local_gm,.follower=n[p].follower_valid};
  r2_radio_precision_scope(&pt[p],scope,epoch[p]+1);
 }
 if(now%62500==0)for(unsigned p=0;p<nodes;p++)if(online[p]&&n[p].local_gm&&n[p].ready){
  uint64_t us=local(p,now),ns=us*1000;
  struct r2_precision_packet out={.boot=n[p].boot,.source_epoch=r2_network_source_epoch(&n[p]),.root=0,.train=n[p].publication,.capture_epoch=epoch[p]+1,.flags=1,.publication=++packet[p],.created_ns=ns-1000000,.expires_ns=ns+1000000000,.utc_offset_ns=INT64_C(1791417600000000000)-(int64_t)(local(p,0)*1000),.count=pt[p].sent_count};
  memcpy(out.correction,pt[p].sent,sizeof out.correction);uint16_t event=(uint16_t)packet[p];unsigned channel=packet[p]%37;
  if(paired[p]>=0){unsigned b=(unsigned)paired[p];if(reach(p,b)&&pa[b]&&n[b].follower_valid){
   struct r2_precision_packet rx=out;rx.root=1;
   struct r2_capture edge={local(b,now)*16+160,epoch[b]+1,R2_CAPTURE_RADIO},available={local(b,now)*16+16000,epoch[b]+1,R2_CAPTURE_NOW};
   (void)r2_radio_precision_rx(&pt[b],&rx,edge,event,channel,available,local(b,now),INT64_C(1791417600000000000)+(int64_t)now*1000);
   (void)r2_radio_precision_step(&pt[b],available,local(b,now));
  }}
  assert(r2_radio_precision_tx(&pt[p],&out,(struct r2_capture){us*16,epoch[p]+1,R2_CAPTURE_RADIO},event,channel,ns,1,out.expires_ns,us));
 }
 for(unsigned p=0;p<nodes;p++)if(online[p]){
  struct r2_radio_precision_view v;struct r2_capture sample={local(p,now)*16+16000,epoch[p]+1,R2_CAPTURE_NOW};
  r2_radio_precision_view(&pt[p],sample,local(p,now),&v);
  r2_network_timing(&n[p],pt[p].scope.boot,pt[p].scope.source_epoch,pt[p].train,n[p].local_gm,v.authorized&&(n[p].local_gm?v.rms_valid:v.map_valid),local(p,now));
 }
}
static void step(void)
{
 for(unsigned p=0;p<nodes;p++)if(!online[p]&&now==join_at[p])init(p);
 transport();precision();
 for(unsigned p=0;p<nodes;p++)if(online[p]){
  struct r2_clock c={0};c.view.source_available=source[p];c.view.source_epoch=epoch[p]+1;c.last_pair_edge=c.last_rmc=(int64_t)local(p,now/1000000*1000000);c.last_pair_utc=INT64_C(1791417600000000)+(int64_t)(now/1000000*1000000);
  uint32_t changes=n[p].gm5.transitions;unsigned previous_role=n[p].gm5.role;
  r2_network_tick(&n[p],&c,local(p,now));
  if(getenv("GM5_DEBUG")&&changes!=n[p].gm5.transitions)fprintf(stderr,"event t%llu p%u role%u root%u co%u pair%d selected%llu why%u ready%u map%u src%u peeruntil%llu sync%u rootboot%llu generation%u hash%llu\n",(unsigned long long)now,p,n[p].gm5.role,n[p].choice,n[p].coordinator,paired[p],(unsigned long long)n[p].gm5.selected,n[p].gm5.last_reason,n[p].ready,pt[p].servo.output.valid,n[p].admission.eligible,(unsigned long long)n[p].peer[1].candidate_until,n[p].timing_valid,(unsigned long long)n[p].root_boot,n[p].gm5.generation,(unsigned long long)n[p].management_hash);
  assert(!n[p].local_gm||n[p].admission.eligible);assert(!n[p].ready||n[p].now<n[p].service_until);
  if(n[p].gm5.role==R2_GM5_COLD){assert(!n[p].hot_ready&&!n[p].standby_authorized);if(previous_role==R2_GM5_COLD)assert(!pt[p].scope.follower);}
  if(n[p].gm5.attempt_started&&n[p].gm5.attempt_until){
   if(attempt_origin[p]==n[p].gm5.attempt_started)assert(attempt_deadline[p]==n[p].gm5.attempt_until);
   else{attempt_origin[p]=n[p].gm5.attempt_started;attempt_deadline[p]=n[p].gm5.attempt_until;}
  }
  if(n[p].standby_authorized)assert(n[p].now<n[p].gm5.grant_until);
 }
 bool exchanged[N]={0};
 if(now%1000000==0)for(unsigned a=0;a<nodes;a++)for(unsigned b=a+1;b<nodes;b++)if(!exchanged[a]&&!exchanged[b]&&reach(a,b)&&(now>=next_status[a][b]||((r2_gm5_control_peer(&n[a])==identity(b)||r2_gm5_control_peer(&n[b])==identity(a))&&(paired[a]<0||paired[b]<0)))){
  /* Native admission is globally serialized per node; one bounded exchange per
   * opportunity, then cadence/cooldown. No all-member standing connection. */
  if(r2_gm5_poll_due(&n[a],n[a].now)||r2_gm5_poll_due(&n[b],n[b].now)){
   join(a,b);exchanged[a]=exchanged[b]=true;next_status[a][b]=now+30000000;
  }
 }
}
static bool merging(void){return scenario==11&&now>=370000000&&now<merge_deadline;}
static int active(void){int found=-1;for(unsigned p=0;p<nodes;p++)if(online[p]&&n[p].local_gm){assert(found<0||split||merging());found=(int)p;}return found;}
static int hot(void){int found=-1;for(unsigned p=0;p<nodes;p++)if(online[p]&&n[p].hot_ready&&n[p].coordinator==0){assert(found<0||split||merging());found=(int)p;}return found;}
int main(int argc,char **argv)
{
 scenario=argc>1?(unsigned)atoi(argv[1]):0;unsigned order=argc>2?(unsigned)atoi(argv[2]):0;nodes=argc>3?(unsigned)atoi(argv[3]):4;assert(nodes>=3&&nodes<=4);
 failure_phase=argc>4?(unsigned)atoi(argv[4]):0;assert(failure_phase<6);
 unsigned permutation[4]={0,1,2,3};for(unsigned i=0;i<nodes;i++){unsigned j=i+order%(nodes-i);order/=nodes-i;unsigned t=permutation[i];permutation[i]=permutation[j];permutation[j]=t;}
 for(unsigned i=0;i<N;i++){paired[i]=-1;join_at[i]=UINT64_MAX;}
 join_at[permutation[0]]=0;join_at[permutation[1]]=0;if(nodes>=3)join_at[permutation[2]]=130000000;if(nodes==4)join_at[permutation[3]]=150000000;
 int root=-1,standby=-1;uint32_t original_publication=0;uint64_t source_gaps=0,hot_gaps=0,observed=0;bool phase_failed=false;int fallback=-1;uint64_t first_active=0,first_hot=0;
 for(now=0;now<600000000||(hot_deadline&&now<hot_deadline+5000000);now+=2500){
  if(now==125000000){root=active();standby=hot();
   if(root<0||standby<0){for(unsigned p=0;p<N;p++)fprintf(stderr,"p%u role%u root%u co%u ready%u hot%u src%u attempt%llu readyat%llu stable%llu map%u ptaccepted%u reject%u\n",p,n[p].gm5.role,n[p].choice,n[p].coordinator,n[p].ready,n[p].hot_ready,n[p].admission.eligible,(unsigned long long)n[p].gm5.attempt_until,(unsigned long long)n[p].gm5.ready_at,(unsigned long long)n[p].gm5.stable_since,pt[p].servo.output.valid,pt[p].accepted,pt[p].rejected);}
   assert(root>=0&&standby>=0);original_publication=n[root].publication;
  }
  if(scenario==17&&root>=0&&now>=200000000&&!phase_anchor&&
     n[root].gm5.issued_until>=local((unsigned)root,now)+R2_GM5_GRANT_US-2500){
   static const uint64_t phases[]={0,25000,1975000,2025000,2975000,3025000};
   phase_anchor=now;phase_failure=now+phases[failure_phase];
   assert(n[root].hot_ready&&n[standby].standby_authorized);
  }
  if(scenario==17&&phase_failure&&now==phase_failure){online[root]=false;join_at[root]=UINT64_MAX;}
  if(now==200000000&&scenario==16)for(unsigned p=0;p<nodes;p++)renewal_deadline[p]=n[p].management_until;
  if(now==200000000&&scenario==1)pa[standby]=false;
  if(now==200000000&&scenario==2){source[standby]=false;++epoch[standby];}
  if(now==200000000&&scenario==3){online[root]=false;join_at[root]=UINT64_MAX;}
  if(now==200000000&&scenario==4)drop_commit=true;
  if(now==200000000&&scenario>=1&&scenario<=4)replacement_deadline=inventory_expiry()+model_replacement_budget();
  if(now==200000000&&scenario==5){source[root]=source[standby]=false;++epoch[root];++epoch[standby];}
  if(now==200000000&&(scenario==6||scenario==14||(scenario>=8&&scenario<=10))){source[standby]=false;++epoch[standby];}
  if(now==200000000&&scenario==7)pa[standby]=false;
  /* Repeated failed acquisition is the population under test: suppress every
   * spare nominee, rather than relying on one inventory traversal order. */
  if(now==200000000&&scenario==14)for(unsigned p=0;p<nodes;p++)if((int)p!=root&&(int)p!=standby)pa[p]=false;
  if(now==300000000&&scenario==14)for(unsigned p=0;p<nodes;p++)if((int)p!=root&&(int)p!=standby)pa[p]=true;
  if(now==200000000&&scenario==11)split=true;
  if(now==370000000&&scenario==11){split=false;merge_deadline=now+30000000+nodes*5000000+R2_GM5_GRANT_US+R2_GM5_RETIRE_GUARD_US;}
  if(now==195000000&&(scenario==11||scenario==12))n[root].gm5.generation=100;
  if(now==200000000&&scenario==12){online[root]=online[standby]=false;join_at[root]=350000000;join_at[standby]=UINT64_MAX;}
  if(now==349000000&&scenario==12){fallback=active();assert(fallback>=0&&fallback!=root);}
  if(now==350000000&&scenario==12){phase_failed=true;return_at=now;return_hot_deadline=now+R2_ELECTION_STABLE_US+30000000+R2_GM5_CONTACT_US+R2_GM5_ACQUIRE_US+R2_GM5_STABLE_US+100000;}
  if(now>200000000&&scenario>=7&&scenario<=10&&!phase_failed){
   struct r2_gm5 *g=&n[root].gm5;
   bool phase=scenario==7?(g->attempt_until&&g->selected==identity((unsigned)standby)):
    scenario==8?(!g->selected&&n[root].now<g->retire_until):
    scenario==9?(g->selected&&g->selected!=identity((unsigned)standby)&&g->attempt_until&&!g->grant_pending):
    (g->selected&&g->selected!=identity((unsigned)standby)&&g->grant_pending);
   if(phase){online[root]=false;join_at[root]=UINT64_MAX;phase_failed=true;}
  }
  if(now==215000000&&scenario==6){online[root]=false;join_at[root]=UINT64_MAX;}
  if(root>=0&&!online[root]&&!failure_at){
   failure_at=now;contact_origin=now;unsigned physical_survivors=0;
   for(unsigned p=0;p<nodes;p++)if(online[p]&&source[p]){
    ++physical_survivors;
    uint64_t until=n[p].gm5.active_until;
    if(until>local(p,0)&&until-local(p,0)>contact_origin)contact_origin=until-local(p,0);
   }
   /* Silent COLD fallback: measured surviving original contact hold, then
    *max32s discovery+3s output fence. HOT additionally allows one serialized
    *30s inventory opportunity, fixed30s contact, original30s acquisition,5s stable dwell
    *and100ms for the modeled25ms control flights/owner opportunities. */
   active_deadline=contact_origin+32000000+3000000+2500;
   uint64_t inventory=inventory_expiry();
   hot_deadline=physical_survivors>=2?(inventory>active_deadline?inventory:active_deadline)+model_replacement_budget():0;
  }
  if(now==240000000&&scenario==2)source[standby]=true;
  if(now==240000000&&scenario==4)drop_commit=false;
  step();
  if(root>=0&&now>190000000&&(scenario==0||now<200000000)){assert(active()==root&&hot()==standby);assert(n[root].publication==original_publication);}
  if(root>=0&&now>201000000&&(scenario==0||scenario==1||scenario==2||scenario==4||scenario==13||scenario==14))assert(active()==root);
  if((scenario==15||scenario==16)&&now>=200000000&&now<240000000)assert(active()==root&&hot()==standby&&n[root].hot_ready);
  if(scenario==16&&now>=200000000&&now<240000000)for(unsigned p=0;p<nodes;p++){
   if(n[p].management_until>renewal_deadline[p]){
    uint64_t gap=n[p].management_until-renewal_deadline[p];
    if(renewal_deadline[p]&&gap>renewal_gap[p])renewal_gap[p]=gap;
    renewal_deadline[p]=n[p].management_until;
   }
  }
  if(scenario==16&&now==296000000)assert(!n[root].hot_ready&&!n[standby].hot_ready);
  if(now>=200000000){++observed;int a=active();source_gaps+=a<0||(a>=0&&!n[a].ready);hot_gaps+=hot()<0;}
  if(scenario==12&&now>350000000)assert(active()==fallback);
  if(scenario==11&&merge_deadline&&now>=merge_deadline){assert(active()==root&&hot()==standby);}
  if(failure_at){int a=active();if(!first_active&&a>=0&&n[a].ready)first_active=now;
   if(!first_hot&&a>=0&&n[a].ready&&n[a].hot_ready&&hot()>=0)first_hot=now;}
  if(active_deadline&&now>=active_deadline){int a=active();assert(a>=0&&n[a].ready);}
  if(scenario==12&&now>=return_hot_deadline&&return_hot_deadline){int a=active();assert(hot()>=0&&a>=0&&n[a].hot_ready);}
  if(hot_deadline&&now>=hot_deadline){int a=active();assert(hot()>=0&&a>=0&&n[a].hot_ready);}
  if(scenario>=7&&scenario<=10&&now>hot_deadline&&hot_deadline)assert(phase_failed);
  if(now==200000000+R2_GM5_COORDINATION_US+3100000&&scenario==3)assert(active()>=0);
  if(scenario==17&&phase_failure&&now==phase_failure+R2_GM5_COORDINATION_US+3100000){
   assert(n[standby].local_gm&&active()==standby); /* Original grant must fund45s+3s. */
  }
  if(replacement_deadline&&now>=replacement_deadline)assert(hot()>=0);

  if(now>580000000){if(active()<0||((scenario!=5&&!(scenario==6&&nodes==3)&&!(scenario>=8&&scenario<=10&&nodes==3))&&hot()<0)){fprintf(stderr,"late scenario%u t%llu active%d hot%d root%d standby%d\n",scenario,(unsigned long long)now,active(),hot(),root,standby);for(unsigned p=0;p<N;p++)fprintf(stderr,"p%u role%u active%llu selected%llu eligible%u ready%u hot%u attempt%llu last%u\n",p,n[p].gm5.role,(unsigned long long)n[p].gm5.active,(unsigned long long)n[p].gm5.selected,n[p].admission.eligible,n[p].ready,n[p].hot_ready,(unsigned long long)n[p].gm5.attempt_until,n[p].gm5.last_reason);}
   assert(active()>=0);if(scenario!=5&&!(scenario==6&&nodes==3)&&!(scenario>=8&&scenario<=10&&nodes==3))assert(hot()>=0);}
 }
 if(scenario==16){assert(renewal_gap[root]>2000000&&renewal_gap[root]<5000000);assert(renewal_gap[standby]<5000000);printf("RENEWAL_GAP active_us=%llu standby_us=%llu capacity_us=100000\n",(unsigned long long)renewal_gap[root],(unsigned long long)renewal_gap[standby]);}
 if(scenario==17){assert(phase_failure&&first_active);printf("SILENT_PHASE phase=%u failure_us=%llu takeover_us=%llu\n",failure_phase,(unsigned long long)phase_failure,(unsigned long long)(first_active-phase_failure));}
 printf("RETURN at_us=%llu confirmed_hot_by_us=%llu replacement_by_us=%llu\n",(unsigned long long)return_at,(unsigned long long)return_hot_deadline,(unsigned long long)replacement_deadline);
 if(scenario==12&&nodes==3)assert(n[root].gm5.generation==100&&n[root].management_generation<100&&n[root].hot_ready);
 if(scenario==13)assert(rx_refused>0);
 /* Retain the outstanding90s obligation, then replace; do not require retry churn. */
 if(scenario==14)assert(n[root].gm5.failures>=1&&n[root].gm5.attempts>=2);
 for(unsigned p=0;p<nodes;p++)if(online[p]){
  struct r2_gm5_diagnostic d={.state=n[p].gm5,.at=n[p].now};char json[2200];
  for(unsigned page=0;page<5;page++){int length=r2_gm_diagnostic_format(json,sizeof json,&d,page,"synthetic",1);assert(length>0&&length<2200);}
 }
 printf("BOUNDS failure_us=%llu original_contact_until_us=%llu active_by_us=%llu hot_by_us=%llu merge_by_us=%llu first_active_offset_us=%llu first_confirmed_hot_offset_us=%llu\n",(unsigned long long)failure_at,(unsigned long long)contact_origin,(unsigned long long)active_deadline,(unsigned long long)hot_deadline,(unsigned long long)merge_deadline,(unsigned long long)(first_active?first_active-failure_at:0),(unsigned long long)(first_hot?first_hot-failure_at:0));
 printf("PASS G5 scenario=%u nodes=%u active=%d hot=%d active_unavailable_us=%llu no_hot_us=%llu observation_us=%llu rx_budget_refused=%u status_tx=%u/%u/%u/%u\n",scenario,nodes,active(),hot(),(unsigned long long)source_gaps*2500,(unsigned long long)hot_gaps*2500,(unsigned long long)observed*2500,rx_refused,n[0].gm5.status_tx,n[1].gm5.status_tx,n[2].gm5.status_tx,n[3].gm5.status_tx);
 return 0;
}
