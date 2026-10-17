/* SPDX-License-Identifier: Apache-2.0
 * Same network state machine, each node self=0 and independent local slot order. */
#include "network.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct r2_network nodes[3];
static uint64_t local(unsigned p,uint64_t t){return t+1000000+p*173000;}
static unsigned slot(unsigned owner,unsigned peer){return(peer+3-owner)%3;}
struct flight{uint8_t bytes[R2_GROUP_CONTROL_MAX];unsigned from,to;uint64_t due;};
static struct flight queue[128];static unsigned head,tail;
static unsigned accepted,replayed;
int main(int argc,char **argv){
 unsigned delay=2000;
 if(argc==2){assert(sscanf(argv[1],"%u",&delay)==1&&delay>=2000&&delay<=100000);}
 for(unsigned p=0;p<3;p++){
  struct r2_network *n=&nodes[p];assert(!r2_network_init(n,0,3,100+p,1000+p,9000+p,40000,local(p,0)));
  n->group=(struct r2_group_config){.psk={1,2,3},.id=0x12345678,.epoch=1};n->capabilities=3;strcpy(n->name,p==0?"Kitchen":"Same name");n->relative_source=true;
  r2_network_capabilities(n,1,1);
  for(unsigned q=0;q<3;q++){unsigned s=slot(p,q);n->peer[s].identity=100+q;r2_group_pair_key(n->peer[s].tx_key,&n->group,100+p,100+q);r2_group_pair_key(n->peer[s].rx_key,&n->group,100+q,100+p);}
 }
 for(uint64_t t=0;t<95000000;t+=2000){
  while(head!=tail&&queue[head%128].due<=t){struct flight *f=&queue[head++%128];unsigned s=slot(f->to,f->from);struct r2_network *n=&nodes[f->to];
   int rc=r2_network_receive(n,s,f->bytes,r2_control_wire_size(f->bytes),local(f->to,t),local(f->to,t));
   if(!rc&&n->peer[s].authenticated){++accepted;
    if(t>80000000){assert(r2_network_receive(n,s,f->bytes,r2_control_wire_size(f->bytes),local(f->to,t),local(f->to,t))!=0);++replayed;}
   }
  }
  if(t==80000000)strcpy(nodes[0].name,"Renamed Kitchen");
  for(unsigned p=0;p<3;p++){
   struct r2_network *n=&nodes[p];uint64_t now=local(p,t);
   for(unsigned q=0;q<3;q++)if(q!=p)r2_network_link(n,slot(p,q),true,now);
   struct r2_clock c={0};c.view.source_available=true;c.view.source_epoch=1;c.last_pair_edge=c.last_rmc=local(p,t/1000000*1000000);c.last_pair_utc=1791417600000000LL+t/1000000*1000000;
   r2_network_timing(n,n->root_boot,n->view.generation,n->publication,n->local_gm,n->ready,now);r2_network_tick(n,&c,now);
   for(unsigned k=0;k<8;k++){uint8_t peer,wire[R2_GROUP_CONTROL_MAX];bool hello;if(!r2_network_next(n,&peer,wire,&hello))break;
    assert(tail-head<128);struct flight *f=&queue[tail++%128];memcpy(f->bytes,wire,r2_control_wire_size(wire));f->from=p;f->to=(p+peer)%3;f->due=t+delay;r2_network_sent(n,hello,peer,wire);
   }
   if(t<60000000)assert(!n->local_gm);
   if(t>75000000){if(!n->ready||n->choice!=slot(p,0)||n->coordinator!=slot(p,1)){fprintf(stderr,"FAIL t=%llu p=%u ready=%u choice=%u coordinator=%u rejected=%u\n",(unsigned long long)t,p,n->ready,n->choice,n->coordinator,n->rejected);return 1;}}
   if(t>75000000&&p==1)assert(n->hot_ready);
  }
 }
 /* A newly discovered receiver may not yet know the other pair member's
  * capability. Its authenticated HELLO must still establish sender identity;
  * the unresolved pair hint must not establish reservation authority. */
 struct r2_network *sender=&nodes[0],*receiver=&nodes[2];
 unsigned destination=slot(0,2),origin=slot(2,0);sender->peer[destination].next_hello=0;
 uint8_t target,hello_wire[R2_GROUP_CONTROL_MAX];bool is_hello=false;
 for(unsigned k=0;k<8&&!is_hello;k++){
  assert(r2_network_next_ready(sender,1u<<destination,&target,hello_wire,&is_hello));
  r2_network_sent(sender,is_hello,target,hello_wire);
 }
 assert(is_hello);receiver->gm_mask=receiver->gnss_mask=1;
 receiver->pair=(struct r2_pair){.root=255,.standby=255};
 assert(!r2_network_receive(receiver,origin,hello_wire,r2_control_wire_size(hello_wire),receiver->now,receiver->now));
 assert(receiver->gm_mask&(1u<<origin));assert(receiver->peer[origin].authenticated);
 assert(!receiver->pair.until);
 assert(accepted>100&&replayed>100);
 assert(!strcmp(nodes[1].peer[slot(1,0)].name,"Renamed Kitchen"));
 assert(nodes[0].local_gm&&!nodes[1].local_gm&&!nodes[2].local_gm);
 printf("PASS group join, different local slot orders, duplicate names, authenticated rename, replay refusal, source dwell, autonomous identity-based roles, continuous modeled HOT after settling; delay_us=%u\n",delay);
}
