/* Production Config5 network/codec under deterministic delayed/lost transport.
 * GNSS and timing maps are explicit synthetic boundaries, not physical proof. */
#include "network.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct r2_network n[4];
static bool online[4],source[4];
static unsigned rng,drop,delay,mask;static bool partition;
struct flight {uint8_t data[R2_AUTH_BYTES],from,to;uint64_t due;};
static struct flight q[2048];static unsigned head,tail;
static unsigned random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static uint64_t local(unsigned p,uint64_t t){return t+1000000+p*123000;}
static void init(unsigned p,uint64_t t,unsigned incarnation)
{
 assert(!r2_network_init(&n[p],p,4,p+1,101+p+100*incarnation,1001+p+100*incarnation,40000,local(p,t)));
 r2_network_capabilities(&n[p],mask,mask);n[p].relative_source=true;
 for(unsigned j=0;j<4;j++){n[p].peer[j].identity=j+1;for(unsigned b=0;b<16;b++){n[p].peer[j].tx_key[b]=1+b*13+p*31+j*7;n[p].peer[j].rx_key[b]=1+b*13+j*31+p*7;}}
 online[p]=source[p]=true;
}
static bool link(unsigned a,unsigned b){return online[a]&&online[b]&&(!partition||(a<2)==(b<2));}
static void step(uint64_t t)
{
 bool was_hot[4];uint32_t withdrawals[4];for(unsigned p=0;p<4;p++){was_hot[p]=n[p].hot_ready;withdrawals[p]=n[p].hot_withdrawals;}
 while(head!=tail&&q[head%2048].due<=t){struct flight f=q[head++%2048];if(link(f.from,f.to))r2_network_receive(&n[f.to],f.from,f.data,r2_auth_size(f.data[1]),local(f.to,t),local(f.to,t));}
 for(unsigned p=0;p<4;p++)if(online[p]){
  uint64_t now=local(p,t);for(unsigned j=0;j<4;j++)if(j!=p)r2_network_link(&n[p],j,link(p,j),now);
  struct r2_clock c={0};c.view.source_available=source[p];c.view.source_epoch=1;c.last_pair_edge=c.last_rmc=local(p,t/1000000*1000000);c.last_pair_utc=1791417600000000LL+t/1000000*1000000;
  r2_network_timing(&n[p],n[p].root_boot,n[p].view.generation,n[p].publication,n[p].local_gm,n[p].ready,now);
  r2_network_tick(&n[p],&c,now);
  assert(!n[p].ready||n[p].service_until>now);
  if(was_hot[p]&&!n[p].hot_ready)assert(n[p].hot_withdrawals>withdrawals[p]);
  if(n[p].hot_withdrawals>withdrawals[p])assert(n[p].hot_withdrawal_reason&&n[p].hot_withdrawal_at<=now);
  if(n[p].hot_ready){assert(n[p].ready);assert(n[p].coordinator<4);assert(mask&(1u<<n[p].coordinator));}
  for(unsigned k=0;k<8;k++){uint8_t peer,bytes[R2_AUTH_BYTES];bool hello;if(!r2_network_next(&n[p],&peer,bytes,&hello))break;r2_network_sent(&n[p],hello,peer,bytes);
   if(random32()%100<drop)continue;assert(tail-head<2048);struct flight *f=&q[tail++%2048];memcpy(f->data,bytes,sizeof bytes);f->from=p;f->to=peer;f->due=t+delay;
  }
 }
}
/* Loss is allowed to revoke fresh source permission, never to preempt the
 * incumbent pair. Measure those honest availability gaps separately, then
 * require zero no-loss source/HOT gaps. Faulted followers
 * must truthfully withdraw expired permission; report that availability. */
static uint64_t checked[4],unready[4],gap[4],maxgap[4],hot_checks,hot_missing;
static void agreement(unsigned root,unsigned standby)
{
 for(unsigned p=0;p<4;p++)if(online[p]&&n[p].now-n[p].admission.boot_at>10000000){
  if(n[p].choice!=root||n[p].coordinator!=standby||(!drop&&!n[p].ready)){
   fprintf(stderr,"pair mismatch p=%u now=%llu actual=%u/%u wanted=%u/%u ready=%u reserve=%llu\n",p,(unsigned long long)n[p].now,n[p].choice,n[p].coordinator,root,standby,n[p].ready,(unsigned long long)n[p].pair.until);abort();
  }
  checked[p]++;if(!n[p].ready){unready[p]++;gap[p]++;if(gap[p]>maxgap[p])maxgap[p]=gap[p];}else gap[p]=0;
 }
}
static void hot(unsigned p){hot_checks++;if(!n[p].hot_ready)hot_missing++;assert(drop||n[p].hot_ready);}

static void commit_recovery_scope(void)
{
 struct r2_network sender=n[2],base=n[1];uint8_t peer,bytes[R2_AUTH_BYTES];bool hello;
 sender.peer[1].next_hello=sender.now+100000;sender.peer[1].next_management=0;sender.peer[1].response_pending=false;
 assert(r2_network_next_ready(&sender,1u<<1,&peer,bytes,&hello)&&!hello&&peer==1);
 struct r2_auth_record commit;assert(!r2_auth_decode(&commit,bytes,r2_auth_size(bytes[1]),sender.peer[1].tx_key));assert(commit.kind==R2_AUTH_COMMIT);
 base.management_hash=base.management_until=0;base.management_generation=0;base.management_committed=base.hot_ready=false;
 uint64_t at=base.now+50000;
 struct r2_network copy=base;
 assert(!r2_network_receive(&copy,2,bytes,r2_auth_size(bytes[1]),at,at));
 assert(copy.hot_ready&&copy.management_hash&&copy.management_generation==commit.decision);
 assert(r2_network_receive(&copy,2,bytes,r2_auth_size(bytes[1]),at,at)); /* Replay. */
 for(unsigned mode=0;mode<10;mode++){
  struct r2_auth_record bad=commit;copy=base;
  switch(mode){
   case 0:bad.root_boot++;break;case 1:bad.coordinator_boot++;break;
   case 2:bad.source_epoch++;break;case 3:bad.source_second^=1;break;
   case 4:bad.created_us-=500000;bad.evidence_us-=500000;bad.expires_us-=500000;break;
   case 5:bad.decision=0;break;case 6:bad.flags=0;break;
   case 7:bad.permit_until_us=bad.created_us+R2_MANAGEMENT_LEASE_US+1;break;
   case 8:copy.ready=false;break;default:copy.management_generation=(uint32_t)commit.decision+1;break;
  }
  assert(!r2_auth_encode(bytes,&bad,sender.peer[1].tx_key));
  assert(r2_network_receive(&copy,2,bytes,r2_auth_size(bytes[1]),at,at));assert(!copy.hot_ready&&!copy.management_hash);
 }
 copy=base;copy.timing_valid=false;assert(!r2_auth_encode(bytes,&commit,sender.peer[1].tx_key));
 assert(!r2_network_receive(&copy,2,bytes,r2_auth_size(bytes[1]),at,at));assert(!copy.hot_ready);
 puts("PASS fresh complete COMMIT recovery: boot/epoch/descriptor/TTL/lease/sequence/readiness fences");
}

int main(int argc,char **argv)
{
 rng=argc>1?strtoul(argv[1],0,10):23;drop=argc>2?strtoul(argv[2],0,10):0;delay=argc>3?strtoul(argv[3],0,10):6000;assert(rng&&drop<=30&&delay<=30000);mask=7;
 init(1,0,0);init(2,0,0);unsigned healthy=0;
 for(uint64_t t=0;t<1800000000;t+=10000){
  /* A local transient role view can clear management just after its READY
   * reaches the coordinator. Exercise the production clear path without
   * clearing the coordinator's already committed decision. */
  if(!drop&&t==94000000)commit_recovery_scope();
  if(!drop&&t==95000000){assert(n[1].hot_ready&&n[2].hot_ready);n[1].coordinator=255;}
  if(!drop&&t==100000000){assert(n[1].hot_ready&&n[2].hot_ready);assert(n[1].management_generation==n[2].management_generation);}
  if(t==120000000){init(0,t,0);n[0].quality.priority1=1;} /* Better-ranked independent newcomer. */
  if(t==240000000)init(3,t,0); /* C1-equivalent restricted client. */
  if(t==350000000){online[2]=false;}
  if(t==360000000)init(2,t,1);
  if(t==500000000){online[1]=false;}
  if(t==530000000)init(1,t,1);
  step(t);
  if(t>90000000&&t<120000000){agreement(1,2);hot(2);if(t>260000000&&!drop){assert(n[0].hot_ready&&n[3].hot_ready);}healthy++;}
  if(t>210000000&&t<350000000){agreement(1,2);hot(2);if(t>260000000&&!drop){assert(n[0].hot_ready&&n[3].hot_ready);}healthy++;}
  if(t>460000000&&t<500000000)agreement(1,0);
  if(t>640000000){agreement(0,2);assert(!n[3].local_gm&&n[3].coordinator!=3);hot(2);healthy++;}
 }
 /* Two independently established partitions rejoin deterministically. */
 memset(n,0,sizeof n);head=tail=0;mask=15;partition=true;for(unsigned p=0;p<4;p++)init(p,0,0);
 for(uint64_t t=0;t<400000000;t+=10000){
  if(t==200000000)partition=false;
  step(t);
  if(t>120000000&&t<200000000){assert(n[0].choice==0&&n[1].choice==0&&n[2].choice==2&&n[3].choice==2);hot(1);hot(3);}
  if(t>230000000)agreement(0,1);
 }
 for(unsigned p=0;p<4;p++)printf("AVAILABILITY node=%u loss=%u checked_ms=%llu source_unready_ms=%llu max_gap_ms=%llu\n",p,drop,(unsigned long long)checked[p]*10,(unsigned long long)unready[p]*10,(unsigned long long)maxgap[p]*10);
 printf("HOT_AVAILABILITY loss=%u checked_ms=%llu unavailable_ms=%llu\n",drop,(unsigned long long)hot_checks*10,(unsigned long long)hot_missing*10);
 fflush(stdout);if(!drop){for(unsigned p=0;p<4;p++)assert(!unready[p]);assert(!hot_missing);}
 printf("PASS Config5 durable pair:30min soak,newcomer,client join,standby reboot,GM loss/return,partition merge; loss=%u delay=%u healthy_HOT_checks=%u\n",drop,delay,healthy);
}
