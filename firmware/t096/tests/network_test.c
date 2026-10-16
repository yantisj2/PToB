/* Historical Config3 fixture, retained for source archaeology. Active Config4
 * coverage is authority_network_test.c; do not compile this against network.h. */
/* SPDX-License-Identifier: Apache-2.0 */
#include "network.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct r2_network n[3];static struct r2_clock gps[3];
static unsigned count,serving[3],following[3],gap[3],max_gap[3];
static uint64_t local(unsigned p,uint64_t t){return t+1000000+p*173000;}
struct flight{uint8_t wire[110],from,to;uint64_t sent,due;};
static struct flight queue[512];static unsigned head,tail;
static void setup(void)
{
 for(unsigned i=0;i<3;i++){
  assert(!r2_network_init(&n[i],i,3,i+1,100+i,10001+i,40000,local(i,0)));
  for(unsigned j=0;j<3;j++)if(i!=j){n[i].peer[j].identity=j+1;
   for(unsigned b=0;b<16;b++){n[i].peer[j].tx_key[b]=1+b*13+i*31+j*7;n[i].peer[j].rx_key[b]=1+b*13+j*31+i*7;}
   r2_network_link(&n[i],j,true,local(i,0));
  }
 }
}
static void conservative_map_intersection(void)
{
 struct r2_network x;assert(!r2_network_init(&x,0,2,1,100,10000,40000,1000000));
 r2_network_link(&x,1,true,1000000);struct r2_net_peer *m=&x.peer[1];
 m->boot=101;m->nonce=88;m->authenticated=true;m->seen=m->alive=m->map_anchor=1000000;
 m->offset_low=-100;m->offset_high=100;m->sent[0]=1100000;
 struct v12_control_record r={.kind=V12_DC_HELLO,.sender=1,.target=0,.root=255,
  .boot=101,.coordinator_boot=101,.challenge=88,.sequence=1,.decision=m->own_nonce,
  .root_boot=100,.config_epoch=R2_NETWORK_CONFIG,.source_second=1100000,.evidence_us=1130000,
  .created_us=1200000,.expires_us=1450000};
 uint8_t wire[110];assert(!v12_control_encode(wire,&r,m->rx_key));
 assert(!r2_network_receive(&x,1,wire,sizeof wire,1230000,1230000));
 assert(m->offset_low==-600&&m->offset_high==600&&m->seen==1230000);
 /* An interval inconsistent with the expanded prior evidence cannot renew
  * calibration. Reconnection/boot changes separately invalidate old bounds. */
 m->sent[1]=1330000;r.sequence=2;r.source_second=1330000;r.evidence_us=1320000;
 r.created_us=1400000;r.expires_us=1650000;
 assert(!v12_control_encode(wire,&r,m->rx_key));
 assert(!r2_network_receive(&x,1,wire,sizeof wire,1435000,1435000));
 assert(m->seen==1230000&&m->offset_low==-600&&m->offset_high==600);
 r2_network_link(&x,1,false,1435000);assert(!m->authenticated);
}
int main(int argc,char **argv)
{
 unsigned delay=argc>=2?(unsigned)strtoul(argv[1],NULL,10):6000;assert(delay>=2000&&delay<=30000);
 bool self_root=argc>=3&&!strcmp(argv[2],"self-root");
 unsigned source=self_root?0:1;
 conservative_map_intersection();
 setup();
 for(uint64_t t=0;t<15000000;t+=2000){
  while(head!=tail&&queue[head%512].due<=t){struct flight *f=&queue[head++%512];
   (void)r2_network_receive(&n[f->to],f->from,f->wire,110,local(f->to,t),local(f->to,t));++count;}
  for(unsigned i=0;i<3;i++){
   gps[i].view.source_available=self_root||i!=0;gps[i].view.source_epoch=1;
   gps[i].last_pair_edge=local(i,t/1000000*1000000);gps[i].last_rmc=gps[i].last_pair_edge+35000;
   if(gps[i].last_rmc>(int64_t)local(i,t))gps[i].last_rmc-=1000000;
   gps[i].last_pair_utc=1791417600000000LL+t/1000000*1000000;
   n[i].relative_source=i==source&&t<10000000;
   r2_network_tick(&n[i],&gps[i],local(i,t));
   for(unsigned k=0;k<8;k++){uint8_t peer,wire[110];bool hello;
    if(!r2_network_next(&n[i],&peer,wire,&hello))break;
    assert(tail-head<512);struct flight *f=&queue[tail++%512];memcpy(f->wire,wire,110);f->from=i;f->to=peer;f->sent=t;f->due=t+delay;
    r2_network_sent(&n[i],hello,peer,wire);
   }
   if(n[i].ready){struct v12_dc_snapshot s;v12_dc_snapshot(&n[i].core,&s);if(t>6000000&&t<9000000&&(s.local_gm_ready||s.functional_service))assert(s.relative_precision_permitted);
    if(t>11000000)assert(!s.relative_precision_permitted);
    assert(!s.precision_permitted);
    serving[i]+=s.serving;following[i]+=n[i].follower_valid;
    if(t>=4000000){bool valid=i==source?s.serving:n[i].follower_valid;
     gap[i]=valid?0:gap[i]+2000;if(gap[i]>max_gap[i])max_gap[i]=gap[i];}
   }
  }
 }
 for(unsigned i=0;i<3;i++){printf("node=%u coord=%u auth=%u reject=%u serving=%u follower=%u phase=%u lastref=%u\n",i,n[i].coordinator,n[i].authenticated_rx,n[i].rejected,serving[i],following[i],n[i].core.phase,n[i].core.last_refusal);assert(n[i].coordinator==source&&n[i].authenticated_rx>100);}
 printf("observed maximum fresh-service gaps us=%u/%u\n",max_gap[0],max_gap[2]);
 assert(serving[source]>5000&&following[self_root?1:0]>5000&&following[2]>0&&max_gap[0]<500000&&max_gap[2]<500000);
 /* Physical pair exposed a missing case: coordinator is also the source.
  * At30ms each way, old SERVICE and REPORT expiry caps caused repeated gaps
  * on a lossless link. Score every2ms after warmup, not eventual election. */
 if(self_root)for(unsigned i=0;i<3;i++)assert(max_gap[i]==0);
 /* A busy first peer must not prevent another peer's bootstrap refresh. */
 n[0].peer[1].next_hello=n[0].peer[2].next_hello=0;
 uint8_t selected,buffer[110];bool bootstrap;
 assert(r2_network_next_ready(&n[0],1u<<2,&selected,buffer,&bootstrap)&&selected==2&&bootstrap);
 /* Valid traffic refreshes liveness without manufacturing a new map sample. */
 if(self_root){
 struct r2_network mapped_copy=n[0];struct r2_net_peer *mp=&mapped_copy.peer[1];
 uint64_t old_map=mp->seen;mp->alive=mapped_copy.now-400000;
 struct v12_control_record report={.kind=V12_DC_REPORT,.sender=1,.target=0,.root=1,
 .boot=n[1].boot,.coordinator_boot=n[0].boot,.challenge=mp->own_nonce,.sequence=mp->sequence+1,
 .root_boot=n[1].boot,.config_epoch=R2_NETWORK_CONFIG,.created_us=n[1].now,.evidence_us=n[1].now,.expires_us=n[1].now+250000};
 uint8_t live_wire[110];assert(!v12_control_encode(live_wire,&report,mp->rx_key));
 assert(!r2_network_receive(&mapped_copy,1,live_wire,110,mapped_copy.now,mapped_copy.now));
 assert(mp->alive==mapped_copy.now&&mp->seen==old_map);
 }
 /* Exact prior signed bootstrap cannot authenticate a new connection nonce. */
 struct r2_network *a=&n[0];uint8_t peer,wire[110];bool hello;
 a->peer[1].next_hello=0;assert(r2_network_next(a,&peer,wire,&hello)&&hello&&peer==1);
 uint64_t now=n[1].now+1;r2_network_link(&n[1],0,false,now);r2_network_link(&n[1],0,true,now);
 (void)r2_network_receive(&n[1],0,wire,110,now,now);assert(!n[1].peer[0].authenticated);
 assert(r2_network_receive(&n[1],0,wire,110,now,now+50000));
 struct r2_network copy=n[2];
 uint64_t held=copy.follower_utc;
 copy.peer[source].alive=copy.now-500000;
 r2_network_tick(&copy,&gps[2],copy.now+1);
 assert(!copy.ready&&copy.choice==source&&!copy.follower_valid);
 assert(copy.follower_utc==held+1);
 struct r2_network held_copy=copy;held_copy.fence=held_copy.now+70000000;
 r2_network_tick(&held_copy,&gps[2],held_copy.now+60000001);
 assert(!held_copy.follower_clock_valid&&held_copy.follower_utc==held+60000002);
 /* Physically lost coordinator permits bounded, fenced reselection. */
 r2_network_link(&copy,source,false,copy.now+1);
 r2_network_tick(&copy,&gps[2],copy.now+1);assert(copy.choice!=source&&!copy.ready);
 copy=n[1];
 r2_network_tick(&copy,&gps[1],copy.now-1);assert(copy.stopped&&!copy.ready&&!copy.follower_valid);
 copy=n[1];copy.next_nonce=UINT64_MAX;r2_network_link(&copy,0,false,copy.now);r2_network_link(&copy,0,true,copy.now);assert(copy.stopped&&!copy.ready);
 printf("R2 network host PASS delay_us=%u source=%u deliveries=%u max_service_gap_us=%u/%u/%u reconnect nonce and stale ingress refused\n",delay,source,count,max_gap[0],max_gap[1],max_gap[2]);
}
