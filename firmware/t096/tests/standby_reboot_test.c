/* Historical Config3 fixture, retained for source archaeology. Active Config4
 * coverage is authority_network_test.c; do not compile this against network.h. */
/* Actual authenticated network: an unrelated standby reboot must not touch GM scope. */
#include "network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct r2_network net[3];
static struct r2_clock clocks[3];
static bool online[3];
static uint64_t boots[3];
struct packet {uint8_t data[110],from,to;uint64_t due,boot;};
static struct packet q[512];static unsigned head,tail;
static uint64_t local(unsigned p,uint64_t t){return 1000000+t+p*173000;}
static void init(unsigned p,uint64_t t){
 assert(!r2_network_init(&net[p],p,3,p+1,++boots[p],10000+boots[p]*17,40000,local(p,t)));
 for(unsigned j=0;j<3;j++)if(j!=p){net[p].peer[j].identity=j+1;
  for(unsigned b=0;b<16;b++){net[p].peer[j].tx_key[b]=1+b*13+p*31+j*7;net[p].peer[j].rx_key[b]=1+b*13+j*31+p*7;}}
 online[p]=true;
}
int main(void){
 for(unsigned gm=0;gm<3;gm++)for(unsigned lost=0;lost<3;lost++)if(lost!=gm){
  memset(net,0,sizeof net);memset(clocks,0,sizeof clocks);head=tail=0;
  for(unsigned p=0;p<3;p++){boots[p]=100+p*100;init(p,0);}
  uint32_t topology[3]={0};uint64_t root_boot=0;unsigned survivor=3-gm-lost;unsigned successor=gm==0?1:0;
  for(uint64_t t=0;t<37000000;t+=2000){
   if(t==10000000){online[lost]=false;for(unsigned p=0;p<3;p++)topology[p]=net[p].topologies;root_boot=net[gm].boot;}
   if(t==13000000)init(lost,t);
   if(t==23000000)online[gm]=false;
   if(t==30000000)init(gm,t);
   for(unsigned p=0;p<3;p++)if(online[p])for(unsigned j=0;j<3;j++)if(j!=p)
    r2_network_link(&net[p],j,online[j],local(p,t));
   while(head!=tail&&q[head%512].due<=t){struct packet *f=&q[head++%512];
    if(online[f->to]&&online[f->from]&&f->boot==net[f->from].boot)
      (void)r2_network_receive(&net[f->to],f->from,f->data,110,local(f->to,t),local(f->to,t));}
   for(unsigned p=0;p<3;p++)if(online[p]){
    struct r2_clock *c=&clocks[p];c->view.source_available=p==gm||t>=7000000;c->view.source_epoch=1;
    c->last_pair_edge=local(p,t/1000000*1000000);c->last_rmc=c->last_pair_edge;
    c->last_pair_utc=1791417600000000LL+t/1000000*1000000;net[p].relative_source=true;
    r2_network_tick(&net[p],c,local(p,t));
    for(unsigned k=0;k<8;k++){uint8_t peer,data[110];bool hello;if(!r2_network_next(&net[p],&peer,data,&hello))break;
      assert(tail-head<512);struct packet *f=&q[tail++%512];memcpy(f->data,data,110);f->from=p;f->to=peer;f->due=t+16000;f->boot=net[p].boot;r2_network_sent(&net[p],hello,peer,data);}
    if(t>=6000000&&t<23000000){struct v12_dc_snapshot s;v12_dc_snapshot(&net[p].core,&s);
      if(t<10000000||p==gm||p==survivor||t>=18000000){
       assert(net[p].ready&&net[p].coordinator==gm&&s.elected_root==gm);
       assert(s.relative_precision_permitted&&s.coarse.generation==1);
       assert(p==gm?s.local_gm_ready:net[p].follower_valid);
      }
      if(t>=10000000&&(p==gm||p==survivor)){
       assert(net[p].topologies==topology[p]&&s.root_boot==root_boot);
      }
    }
    if((t>=28000000&&t<30000000)||t>=35000000){
     struct v12_dc_snapshot s;v12_dc_snapshot(&net[p].core,&s);
     assert(net[p].ready&&net[p].coordinator==successor&&s.elected_root==successor);
     assert(s.relative_precision_permitted&&(p==successor?s.local_gm_ready:net[p].follower_valid));
     assert(s.root_boot!=root_boot);
    }
   }
  }
  printf("PASS GM=%u standby-reboot=%u survivor=%u: continuous scope, late lower-ID sources cannot preempt, fresh reboot rejoins; real GM loss re-elects, recovered old GM follows\n",gm,lost,survivor);
 }
}
