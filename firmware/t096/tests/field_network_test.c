/* Actual G1 network state machine: repeated fresh boot incarnations, dynamic
 * local slot orders, delayed prior-boot replay, root absence/no promotion. */
#include "network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct r2_network n[2];static uint64_t ids[3]={0x1000000000000001ULL,0x1000000000000002ULL,0x1000000000000003ULL};
static unsigned slot(unsigned p,unsigned q){return(p+q)%2;}
static uint64_t boot[2]={100,101},offset[2]={1000000,1173000};
static void init(unsigned p,uint64_t t){
 assert(!r2_network_init(&n[p],0,3,ids[p],boot[p],boot[p]*100,40000,t+offset[p]));
 n[p].group=(struct r2_group_config){.psk={1,2,3},.id=0x12345678,.epoch=1};n[p].capabilities=3;n[p].relative_source=true;snprintf(n[p].name,25,"TC-%02u",p+1);r2_network_capabilities(&n[p],3,3);
 for(unsigned q=0;q<2;q++)n[p].peer[slot(p,q)].identity=ids[q];n[p].peer[2].identity=ids[2];
 assert(!r2_network_profile_root(&n[p],slot(p,0),2,t+offset[p]));
}
struct flight{uint8_t bytes[R2_GROUP_CONTROL_MAX];unsigned from;uint64_t boot,due;};static struct flight queue[64];static unsigned head,tail;
int main(void){
 init(0,0);init(1,0);uint8_t old[R2_GROUP_CONTROL_MAX]={0};bool saved=false;unsigned stale=0,healthy=0;
 for(uint64_t t=0;t<460000000;t+=2000){
  if(t==100000000||t==200000000){
   r2_network_link(&n[0],1,false,t+offset[0]);assert(!n[0].peer[1].authenticated&&r2_network_member(&n[0],1)&&n[0].peer[1].name[0]);
   ++boot[1];init(1,t);head=tail=0;
  }
  while(head!=tail&&queue[head%64].due<=t){struct flight *f=&queue[head++%64];unsigned to=1-f->from;
   (void)r2_network_receive(&n[to],1,f->bytes,r2_control_wire_size(f->bytes),t+offset[to],t+offset[to]);
  }
  for(unsigned p=0;p<2;p++){
   uint64_t now=t+offset[p];r2_network_link(&n[p],1,true,now);
   struct r2_clock c={0};c.view.source_available=!(p==0&&t>=320000000&&t<340000000);c.view.source_epoch=t>=340000000?2:1;c.last_pair_edge=c.last_rmc=t/1000000*1000000+offset[p];c.last_pair_utc=1791504000000000LL+t/1000000*1000000;
   r2_network_relay_timing(&n[p],slot(p,0),n[p].root_boot,n[p].view.generation,7,p==0,n[p].ready,now);r2_network_tick(&n[p],&c,now);
   if(p==1&&n[p].freshness.utc_us){
    uint64_t upper;assert(r2_group_coarse_upper(&n[p].freshness,n[p].freshness_at,now,&upper));
    uint64_t truth=1791504000000000ULL+t;
    if(!(upper>=truth&&upper-truth<250000)){fprintf(stderr,"ANCHOR t=%llu upper=%llu truth=%llu at=%llu uncertainty=%u\n",(unsigned long long)t,(unsigned long long)upper,(unsigned long long)truth,(unsigned long long)n[p].freshness_at,n[p].freshness.uncertainty_us);return 1;}
    assert(n[p].freshness.uncertainty_us>=n[p].map_bound_us);
    if(t==90000000){
     struct r2_network disconnected=n[p];r2_network_link(&disconnected,1,false,now);
     assert(!disconnected.ready&&!disconnected.peer[1].authenticated);
     assert(disconnected.freshness.utc_us==n[p].freshness.utc_us);
     assert(r2_group_coarse_upper(&disconnected.freshness,disconnected.freshness_at,now+1000000,&upper));
     assert(upper>=truth+1000000&&disconnected.freshness.until==n[p].freshness.until);
    }
   }
   if(p==1)assert(!n[p].local_gm);assert(!(n[p].members_mask&4));
   if((t>80000000&&t<95000000)||(t>180000000&&t<195000000)||(t>280000000&&t<315000000)||t>430000000){
    if(!n[p].ready||n[p].choice!=slot(p,0)||!n[p].hot_ready){fprintf(stderr,"field failed t=%llu p=%u ready=%u root=%u hot=%u refusal=%u\n",(unsigned long long)t,p,n[p].ready,n[p].choice,n[p].hot_ready,r2_network_hot_refusal(&n[p]));fprintf(stderr,"p0 auth=%u boot=%llu seen=%llu cand=%llu nonce=%llu own=%llu rej=%u p1 auth=%u boot=%llu seen=%llu cand=%llu nonce=%llu own=%llu rej=%u\n",n[0].peer[1].authenticated,(unsigned long long)n[0].peer[1].boot,(unsigned long long)n[0].peer[1].seen,(unsigned long long)n[0].peer[1].candidate_until,(unsigned long long)n[0].peer[1].nonce,(unsigned long long)n[0].peer[1].own_nonce,n[0].rejected,n[1].peer[1].authenticated,(unsigned long long)n[1].peer[1].boot,(unsigned long long)n[1].peer[1].seen,(unsigned long long)n[1].peer[1].candidate_until,(unsigned long long)n[1].peer[1].nonce,(unsigned long long)n[1].peer[1].own_nonce,n[1].rejected);return 1;}++healthy;
   }
   if(t>325000000&&t<339000000)assert(!n[p].ready&&!n[p].local_gm);
   for(unsigned k=0;k<8;k++){uint8_t peer,wire[R2_GROUP_CONTROL_MAX];bool hello;if(!r2_network_next(&n[p],&peer,wire,&hello))break;
    assert(peer==1);assert(tail-head<64);struct flight *f=&queue[tail++%64];memcpy(f->bytes,wire,r2_control_wire_size(wire));f->from=p;f->boot=boot[p];f->due=t+(p?8000:2000);r2_network_sent(&n[p],hello,peer,wire);
    if(p==1&&hello&&t>90000000&&t<91000000){memcpy(old,wire,sizeof old);saved=true;}
   }
  }
  if(saved&&t>110000000&&t%1000000==0){assert(r2_network_receive(&n[0],1,old,r2_control_wire_size(old),t+offset[0],t+offset[0])!=0||!n[0].peer[1].authenticated);assert(!n[0].peer[1].authenticated||n[0].peer[1].boot==boot[1]);++stale;}
 }
 assert(healthy&&stale>100);puts("PASS actual G1 network repeated relay boots, permuted slots, fenced old names/HELLO, root loss/recovery, promotion/client exclusion");
}
