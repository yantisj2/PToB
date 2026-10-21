/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Selected synthetic owner replay developed during the146 reliability review.
 * Actual protocol/wire bodies, RX2/FIFO32 and separate physical callback time;
 * ideal PT and initialized authenticated context are fixture inputs, not RF proof.
 */
#include "../protocol/network.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static struct r2_network base(bool active)
{
 struct r2_network n;assert(!r2_network_init(&n,0,2,100,10,22,40000,70000000));
 n.group=(struct r2_group_config){.id=1,.epoch=1,.psk={1}};n.capabilities=3;n.gm5.enabled=true;
 n.gm_mask=n.gnss_mask=3;n.admission.eligible=true;n.admission.observing=true;n.admission.source_epoch=1;
 n.candidate=(struct v12_dc_coarse){.available=true,.generation=1,.valid_until_us=n.now+1500000};
 n.peer[1]=(struct r2_net_peer){.identity=200,.boot=20,.connected=true,.authenticated=true,.map_valid=true,.alive=n.now,.seen=n.now,.candidate_until=n.now+1500000,.candidate_epoch=1};
 n.choice=active?0:1;n.root_boot=active?10:20;n.coordinator=active?1:0;n.coordinator_boot=active?20:10;
 n.view=n.candidate;n.view.available=true;n.publication=1;n.ready=true;n.local_gm=active;n.service_until=n.now+1500000;
 n.timing_valid=true;n.timing_boot=n.root_boot;n.timing_epoch=1;n.timing_train=1;n.timing_at=n.now;
 n.gm5.role=active?R2_GM5_ACTIVE:R2_GM5_ACQUIRING;n.gm5.active=active?100:200;n.gm5.active_boot=n.root_boot;n.gm5.active_until=n.now+120000000;
 n.gm5.selected=active?200:0;n.gm5.generation=7;n.management_generation=7;return n;
}
struct event {uint64_t at;size_t length;uint8_t data[R2_GROUP_CONTROL_MAX];};
struct queue {struct event e[32];unsigned head,tail,count,peak,drops;};
static void put(struct queue*q,const struct event*e){if(q->count==32){q->drops++;return;}q->e[q->tail++%32]=*e;if(++q->count>q->peak)q->peak=q->count;}
int main(int argc,char**argv)
{
 unsigned period=argc>1?(unsigned)atoi(argv[1]):150000;unsigned burst_turn[2]={0};
 struct r2_network pair[2]={base(true),base(false)};struct r2_network*a=&pair[0],*b=&pair[1];
 b->identity=200;b->boot=20;b->peer[1].identity=100;b->peer[1].boot=10;b->root_boot=b->gm5.active_boot=b->timing_boot=10;b->gm5.active=100;b->coordinator_boot=20;
 a->peer[1].own_nonce=11;a->peer[1].nonce=22;b->peer[1].own_nonce=22;b->peer[1].nonce=11;strcpy(a->name,"Active");strcpy(b->name,"Successor");uint64_t start=a->now;
 for(unsigned k=0;k<2;k++){struct r2_network*n=&pair[k];n->admission.boot_at=n->admission.stable_at=0;n->admission.last_expiry=start+1500000;n->candidate.utc_second=n->view.utc_second=1791417600;n->candidate.evidence_us=n->view.evidence_us=start;n->peer[1].map_anchor=start;n->gm5.attempt_started=start;n->gm5.attempt_until=start+R2_GM5_ACQUIRE_US;}
 struct queue receive[2]={0};struct event flight[2]={0};uint64_t due[2]={0},cycle[2]={0,50000};
 uint64_t first_hot=0,withdraw[2]={0},latency[2]={0},hello_at[2]={0},hello_gap[2]={0},first_reject[2]={0};unsigned why[2]={0},tx[2][14]={{0}},reject[2][14]={{0}},hello_refusal[2]={0};
 for(uint64_t elapsed=0;elapsed<=45000000;elapsed+=1000){uint64_t now=start+elapsed;
  for(unsigned k=0;k<2;k++)if(due[k]&&now>=due[k]){put(&receive[1-k],&flight[k]);due[k]=0;}
  for(unsigned k=0;k<2;k++)if(elapsed>=cycle[k]){
   struct r2_network*n=&pair[k];n->now=now;
   for(unsigned j=0;j<2&&receive[k].count;j++){
    struct event*e=&receive[k].e[receive[k].head++%32];receive[k].count--;if(now-e->at>latency[k])latency[k]=now-e->at;
    unsigned kind=e->data[1];int rc=r2_network_receive(n,1,e->data,e->length,e->at,now);
    if(rc){reject[k][kind]++;if(first_hot&&!first_reject[k]){first_reject[k]=elapsed;printf("FIRST_REJECT owner%u t%llu kind%u rc%d callback_age%llu seen_age%llu candidate_remaining%lld\n",k,(unsigned long long)elapsed,kind,rc,(unsigned long long)(now-e->at),(unsigned long long)(now-n->peer[1].seen),(long long)(n->peer[1].candidate_until-now));}}
   }
   struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;uint64_t second=elapsed/1000000*1000000;source.last_pair_edge=source.last_rmc=start+second;source.last_pair_utc=INT64_C(1791417600000000)+second;
   r2_network_timing(n,n->root_boot,n->view.generation,n->publication,n->local_gm,true,now);bool was_hot=n->hot_ready;r2_network_tick(n,&source,now);
   if(first_hot&&was_hot&&!n->hot_ready&&!withdraw[k]){withdraw[k]=elapsed;why[k]=n->hot_withdrawal_reason;printf("WITHDRAW owner%u t%llu why%u candidate_remaining%lld source_remaining%lld readyage%llu management_remaining%lld queue%u latencymax%llu\n",k,(unsigned long long)elapsed,why[k],(long long)(n->peer[1].candidate_until-now),(long long)(n->service_until-now),(unsigned long long)(now-n->gm5.ready_at),(long long)(n->management_until-now),receive[k].count,(unsigned long long)latency[k]);}
   uint8_t peer,wire[R2_GROUP_CONTROL_MAX];bool hello;
   if(r2_network_next_ready(n,2,&peer,wire,&hello)){
    assert(peer==1&&!due[k]);r2_network_sent(n,hello,peer,wire);unsigned kind=wire[1];tx[k][kind]++;
    if(kind==R2_AUTH_HELLO){if(hello_at[k]&&elapsed-hello_at[k]>hello_gap[k])hello_gap[k]=elapsed-hello_at[k];hello_at[k]=elapsed;}
    flight[k].at=now+25000;flight[k].length=r2_control_wire_size(wire);memcpy(flight[k].data,wire,flight[k].length);due[k]=flight[k].at;
   }
   uint64_t interval=100000;
   if(k==1&&elapsed>=20000000&&elapsed<26000000){
    if(period)interval=period;
    else {static const uint64_t burst[]={100000,150000,250000};interval=burst[burst_turn[k]++%3];printf("BURST t%llu interval%llu\n",(unsigned long long)elapsed,(unsigned long long)interval);}
   }
   cycle[k]=elapsed+interval;
  }
  if(!first_hot&&a->hot_ready&&b->hot_ready)first_hot=elapsed;
 }
 for(unsigned k=0;k<2;k++){hello_refusal[k]=pair[k].hello_map_refusals;printf("SUMMARY owner%u period%u firsthot%llu withdraw%llu why%u finalhot%u rxpeak%u drops%u rxlatency%llu hellogap%llu maprefusal%u",k,period,(unsigned long long)first_hot,(unsigned long long)withdraw[k],why[k],pair[k].hot_ready,receive[k].peak,receive[k].drops,(unsigned long long)latency[k],(unsigned long long)hello_gap[k],hello_refusal[k]);for(unsigned kind=7;kind<14;kind++)printf(" kind%u_tx%u_reject%u",kind,tx[k][kind],reject[k][kind]);puts("");}
 return !first_hot||withdraw[0]||withdraw[1]?7:0;
}
