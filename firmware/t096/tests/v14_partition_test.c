/* Actual authenticated codec/network, finite connected-domain lifecycle. */
#include "network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct r2_network n[3];
struct flight {uint8_t bytes[R2_AUTH_BYTES],from,to;uint64_t due;};
static struct flight q[1024];static unsigned head,tail;static bool cut,asymmetric,timing;
static uint64_t local(unsigned p,uint64_t t){return t+1000000+p*173000;}
static bool link(unsigned a,unsigned b){return !cut||(a!=0&&b!=0);}
static void init(unsigned p,uint64_t t,unsigned incarnation){
 assert(!r2_network_init(&n[p],p,3,p+1,100+p+100*incarnation,10001+p+100*incarnation,40000,local(p,t)));
 r2_network_capabilities(&n[p],7,7);n[p].relative_source=true;
 for(unsigned j=0;j<3;j++){n[p].peer[j].identity=j+1;for(unsigned b=0;b<16;b++){
 n[p].peer[j].tx_key[b]=1+b*13+p*31+j*7;n[p].peer[j].rx_key[b]=1+b*13+j*31+p*7;}}
}
static void step(uint64_t t){
 while(head!=tail&&q[head%1024].due<=t){struct flight *f=&q[head++%1024];
 if(link(f->from,f->to)&&!(asymmetric&&f->from==0))r2_network_receive(&n[f->to],f->from,f->bytes,r2_auth_size(f->bytes[1]),local(f->to,t),local(f->to,t));}
 for(unsigned p=0;p<3;p++){
 uint64_t now=local(p,t);for(unsigned j=0;j<3;j++)if(j!=p)r2_network_link(&n[p],j,link(p,j),now);
 struct r2_clock c={0};c.view.source_available=true;c.view.source_epoch=1;
 c.last_pair_edge=c.last_rmc=local(p,t/1000000*1000000);c.last_pair_utc=1791417600000000LL+t/1000000*1000000;
 r2_network_timing(&n[p],n[p].root_boot,n[p].view.generation,n[p].publication,n[p].local_gm,timing&&n[p].ready,now);
 r2_network_tick(&n[p],&c,now);
 for(unsigned k=0;k<8;k++){uint8_t peer,wire[R2_AUTH_BYTES];bool hello;if(!r2_network_next(&n[p],&peer,wire,&hello))break;
 assert(tail-head<1024);struct flight *f=&q[tail++%1024];memcpy(f->bytes,wire,sizeof wire);f->from=p;f->to=peer;f->due=t+6000;r2_network_sent(&n[p],hello,peer,wire);}
 }
}
int main(void){
 for(unsigned p=0;p<3;p++)init(p,0,0);
 unsigned no_map_commits=0,partition_dual=0;uint64_t promoted=0;uint32_t settled_topologies[3]={0};
 for(uint64_t t=0;t<300000000;t+=2000){
 if(t==80000000)timing=true;
 if(t==100000000)cut=true;
 if(t==140000000)cut=false;
 if(t==180000000)init(0,t,1);
 if(t==260000000)asymmetric=true;
 if(t==270000000)asymmetric=false;
 step(t);
 if(t>70000000&&t<80000000){assert(n[0].local_gm&&n[1].management_committed&&!n[1].hot_ready);no_map_commits++;}
 if(t>85000000&&t<100000000)assert(n[1].hot_ready);
 if(t>100000000&&n[1].local_gm&&!promoted)promoted=t;
 if(t>115000000&&t<140000000){assert(n[0].local_gm&&n[1].local_gm);partition_dual++;}
 if(t==170000000)for(unsigned p=0;p<3;p++)settled_topologies[p]=n[p].topologies;
 if(t>170000000&&t<180000000)for(unsigned p=0;p<3;p++){assert(n[p].choice==n[0].choice);assert(n[p].topologies==settled_topologies[p]);}
 if(t>=180000000&&t<240000000)assert(!n[0].local_gm);
 if(t>250000000&&t<260000000)for(unsigned p=0;p<3;p++)assert(n[p].choice==n[0].choice);
 if(t>285000000)for(unsigned p=0;p<3;p++)assert(n[p].choice==n[0].choice);
 }
 assert(no_map_commits&&partition_dual&&promoted>=104000000&&promoted<107000000);
 printf("V14 source/control independent of map; HOT loss takeover at%llu; partition/merge, reboot60s and asymmetric return PASS\n",(unsigned long long)promoted);
}
