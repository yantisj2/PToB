/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_passive_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct r2_group_config group={.psk={1,2,3},.id=9,.epoch=1};
static struct lp_checkpoint saved;static bool storage_ok=true;static unsigned writes;
static bool save(void *user,const struct lp_checkpoint *s){assert(user==&saved);++writes;if(!storage_ok)return false;saved=*s;return true;}
static struct lp_packet meta(uint32_t serial,uint64_t ns){
 return (struct lp_packet){.scope={.emitter=1,.boot=2,.root=1,.root_boot=2,.epoch=3,.stream=1,.seed=0x51a7c003},
 .cursor=0,.serial=serial,.utc_ns=ns,.kind=LP_META,.schedule=LP_FAST,.has_pair=true,.time_valid=true,
 .delay_10ms=85,.tz={.id=2,.offset_min=-300}};
}
static void codec(void){
 uint8_t w[112],context[16],wrong[16];struct lp_packet p=meta(1,UINT64_C(1000000000000)),q;
 assert(!lp_context(context,&p.scope,LP_FAST,&group));memcpy(wrong,context,16);wrong[0]^=1;
 for(unsigned k=LP_DISC;k<=LP_FOLLOW;k++){
  p.kind=(uint8_t)k;
  if(k>=LP_SYNC){p.has_pair=p.time_valid=false;p.delay_10ms=0;}
  assert(!lp_encode(w,sizeof w,&p,context,&group));
  assert(!lp_decode(&q,w,lp_size(k),context,&group));assert(q.kind==k&&q.cursor==p.cursor);
  if(k!=LP_META)assert(lp_decode(&q,w,lp_size(k),wrong,&group));
  for(size_t i=0;i<lp_size(k);i++){w[i]^=1;assert(lp_decode(&q,w,lp_size(k),context,&group));w[i]^=1;}
  assert(lp_decode(&q,w,lp_size(k)-1,context,&group));
 }
 p=meta(1,1);p.scope.parent=3;assert(lp_context(context,&p.scope,LP_FAST,&group));
 p=meta(1,1);p.schedule=2;assert(lp_encode(w,sizeof w,&p,context,&group));
 uint8_t order[64],seen[64]={0};assert(!lp_hop_order(order,0x51a7c003));
 for(unsigned i=0;i<64;i++){assert(order[i]<64&&!seen[order[i]]);seen[order[i]]=1;}
 for(unsigned i=0;i<64;i++)assert(lp_channel(order,i)!=lp_channel(order,i+1));
 printf("context ");for(unsigned i=0;i<16;i++)printf("%02x",wrong[i]^(i==0));puts("");
 printf("hop ");for(unsigned i=0;i<64;i++)printf("%02x",order[i]);puts("");
 p=meta(1,UINT64_C(1000000000000));assert(!lp_context(context,&p.scope,LP_FAST,&group));
 for(unsigned k=1;k<=4;k++){
  p.kind=(uint8_t)k;if(k>=3){p.has_pair=p.time_valid=false;p.delay_10ms=0;}
  assert(!lp_encode(w,sizeof w,&p,context,&group));printf("wire%u ",k);
  for(size_t i=0;i<lp_size(k);i++)printf("%02x",w[i]);puts("");
 }
}
static void ledger(uint64_t phase){
 struct lp_tx t;struct lp_checkpoint initial={.seed=0x51a7c003,.next_slot_us=phase};struct lp_reservation p;
 assert(!lp_tx_restore(&t,&initial,save,&saved,false));assert(!lp_tx_reserve(&t,0,&p));
 assert(lp_tx_restore(&t,&initial,save,&saved,true));
 uint64_t admitted=0;unsigned denied=0,control_only=0;
 for(unsigned i=0;i<1800;i++){
  uint64_t slot=phase+(uint64_t)i*LP_BEAT_US;
  if(!lp_tx_reserve(&t,slot,&p)){++denied;continue;}
  admitted+=lp_duration(p.control);
  printf("RF %llu %llu %u -1\n",(unsigned long long)phase,(unsigned long long)slot,lp_duration(p.control));
  if(p.has_pair){
   admitted+=lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW);
   printf("RF %llu %llu %u %u\n",(unsigned long long)phase,(unsigned long long)(slot+2000000),lp_duration(LP_SYNC),lp_channel(t.order,p.cursor));
   printf("RF %llu %llu %u %u\n",(unsigned long long)phase,(unsigned long long)(slot+2481951),lp_duration(LP_FOLLOW),lp_channel(t.order,p.cursor+1));
   assert(lp_tx_confirm(&t,LP_SYNC,slot+2231951));
   assert(lp_tx_confirm(&t,LP_FOLLOW,slot+2750765));
  }else ++control_only;
  assert(lp_charged(&t.durable,slot)<=LP_CAP_US);
 }
 assert(t.durable.cursor>3000);assert(admitted>LP_CAP_US);assert(writes>3000);
 printf("ledger admitted_us=%llu refused=%u control_only=%u state_bytes=%zu\n",(unsigned long long)admitted,denied,control_only,sizeof initial);
 /* Clean restart preserves physical cursor, phase, META position and debt. */
 struct lp_tx restart;assert(lp_tx_restore(&restart,&saved,save,&saved,true));
 assert(!memcmp(&restart.durable,&t.durable,sizeof saved));
 assert(lp_tx_restore(&restart,&restart.durable,save,&saved,true));
 assert(!memcmp(&restart.durable,&t.durable,sizeof saved));
 assert(!lp_tx_reserve(&restart,t.durable.next_slot_us-1,&p));
 assert(lp_tx_reserve(&restart,t.durable.next_slot_us,&p));
 struct lp_checkpoint interrupted=saved;
 assert(lp_tx_restore(&restart,&interrupted,save,&saved,true));assert(restart.durable.fh_disabled);
 assert(lp_tx_reserve(&restart,restart.durable.next_slot_us,&p)&&!p.has_pair);
 /* Proven-no-RF cancellation cannot mint airtime or consume a channel. */
 initial=(struct lp_checkpoint){.seed=0x51a7c003};
 assert(lp_tx_restore(&t,&initial,save,&saved,true));assert(lp_tx_reserve(&t,0,&p));
 uint64_t before=lp_charged(&t.durable,0);lp_tx_cancel(&t,true);
 assert(t.durable.cursor==0&&!t.durable.pending&&lp_charged(&t.durable,0)==before);
 assert(lp_tx_reserve(&t,4000000,&p));assert(lp_tx_confirm(&t,LP_SYNC,6231951));lp_tx_cancel(&t,true);
 assert(t.durable.fh_disabled&&t.durable.cursor==1);
 assert(lp_tx_restore(&t,&initial,save,&saved,true));storage_ok=false;assert(!lp_tx_reserve(&t,0,&p)&&!t.ready);storage_ok=true;
 /* Minute bucket retained past the oldest possible RF and across the hour. */
 initial.buckets[0]=100;initial.minute=0;
 assert(lp_charged(&initial,LP_HOUR_US)==100);
 assert(lp_charged(&initial,LP_HOUR_US+LP_MINUTE_US)==0);
}
static void receiver(void){
 struct lp_rx r;lp_rx_clear(&r);uint64_t base=UINT64_C(1000000000000),age;
 struct lp_packet m=meta(1,base);struct lp_age a={.lower_ns=base+1000000,.upper_ns=base+2000000,.valid_until_us=UINT64_C(1000000000),.source_bound_known=true};
 assert(lp_age_admit(&a,base,0,&age)&&age==2000);
 assert(!lp_age_admit(&a,base+3000000,0,&age));
 assert(!lp_age_admit(&a,base-UINT64_C(11000000000),0,&age));
 a.source_bound_known=false;assert(!lp_age_admit(&a,base,0,&age));a.source_bound_known=true;
 assert(lp_rx_meta(&r,&m,&group,NULL,0,1000)&&!r.bounded&&!r.meta_deadline_us);
 assert(!lp_rx_meta(&r,&m,&group,&a,0,2000)); /* same serial never renews */
 lp_rx_clear(&r);assert(lp_rx_meta(&r,&m,&group,&a,0,2000));assert(r.meta_deadline_us==LP_LEASE_US);
 /* Original age shortens a late delivery instead of issuing a new18s grant. */
 lp_rx_clear(&r);a.lower_ns=base+1000000000;a.upper_ns=base+2000000000;
 assert(lp_rx_meta(&r,&m,&group,&a,0,1000));assert(r.meta_deadline_us==16001000);
 lp_rx_clear(&r);
 for(unsigned i=0;i<=50;i++){
  uint64_t event=(uint64_t)i*LP_BEAT_US+2000000;
  if(i%3==0){m=meta(i/3+1,base+(uint64_t)i*LP_BEAT_US*1000);m.cursor=i*2;
   a.lower_ns=m.utc_ns+1000000;a.upper_ns=m.utc_ns+2000000;
   assert(lp_rx_meta(&r,&m,&group,&a,(uint64_t)i*LP_BEAT_US,(uint64_t)i*LP_BEAT_US+2000));}
  struct lp_packet hint={.kind=LP_DISC,.schedule=LP_FAST,.cursor=i*2,.has_pair=true,.time_valid=true,.delay_10ms=150};
  assert(lp_rx_hint(&r,&hint,event-1500000,event-1499000));
  uint64_t tick=event*16;
  assert(lp_rx_sync(&r,i*2,tick,7,event,event+100000));
  struct lp_packet f={.kind=LP_FOLLOW,.schedule=LP_FAST,.cursor=i*2,.utc_ns=base+event*1000+event/10000};
  a.lower_ns=f.utc_ns+400000000;a.upper_ns=f.utc_ns+500000000;
  assert(lp_rx_follow(&r,&f,&a,7,event+800000));
  assert(r.physical_deadline_us==event+LP_HOLD_US);
  if(i<45)assert(!r.trained);
 }
 assert(r.trained&&r.fit_count<=16&&r.rate_ppb>=99&&r.rate_ppb<=101);
 uint64_t ns;assert(lp_rx_estimate(&r,r.last_tick+16000000,7,r.last_us+1000000,&ns));
 assert(ns>=r.last_tx_ns+1000000099&&ns<=r.last_tx_ns+1000000101);
 assert(!lp_rx_estimate(&r,r.last_tick,8,r.last_us,&ns));
 assert(lp_rx_envelope_current(&r,&a,r.last_us+1000000));
 assert(!lp_rx_envelope_current(&r,&a,r.physical_deadline_us));
 /* One prediction only; an absent/late pair drops back to DTS. */
 assert(r.prediction&&r.hint);lp_rx_expire(&r,r.hint_us+250001);assert(!r.hint&&!r.prediction);
 assert(!lp_rx_sync(&r,r.highest+2,r.last_tick+64000000,7,r.last_us+LP_BEAT_US,r.last_us+LP_BEAT_US));
 /* Receipt of newer metadata cannot renew the original physical deadline. */
 uint64_t original=r.physical_deadline_us;
 m=meta(r.serial+1,r.last_meta_ns+1000000000);m.cursor=r.highest;
 a.lower_ns=m.utc_ns+1000000;a.upper_ns=m.utc_ns+2000000;
 assert(lp_rx_meta(&r,&m,&group,&a,r.last_us+1000000,r.last_us+1002000));
 assert(r.physical_deadline_us==original);
 /* An age-valid late FOLLOW still cannot repair an expired association. */
 uint64_t next=r.last_us+LP_BEAT_US;
 struct lp_packet hint={.kind=LP_DISC,.schedule=LP_FAST,.cursor=r.highest+2,.has_pair=true,.time_valid=true,.delay_10ms=150};
 assert(lp_rx_hint(&r,&hint,next-1500000,next-1499000));
 assert(lp_rx_sync(&r,hint.cursor,next*16,7,next,next+100000));
 struct lp_packet late={.kind=LP_FOLLOW,.schedule=LP_FAST,.cursor=hint.cursor,.utc_ns=r.last_tx_ns+LP_BEAT_US*1000};
 a.lower_ns=late.utc_ns+1000000000;a.upper_ns=late.utc_ns+1100000000;
 assert(!lp_rx_follow(&r,&late,&a,7,next+1000000)&&!r.pending&&!r.hint);
 assert(r.physical_deadline_us==original);
 /* A new authenticated scope retires the fit; an older stream cannot revive it. */
 m=meta(1,r.last_meta_ns+1000000000);m.scope.stream=2;m.cursor=r.highest;
 a.lower_ns=m.utc_ns+1000000;a.upper_ns=m.utc_ns+2000000;
 assert(lp_rx_meta(&r,&m,&group,&a,next+1100000,next+1102000));
 assert(!r.trained&&!r.have_pair&&!r.physical_deadline_us);
 m.scope.stream=1;++m.serial;m.utc_ns+=1000000000;
 assert(!lp_rx_meta(&r,&m,&group,&a,next+1200000,next+1201000));
}
static void runtime_root(void){
 struct lp_rx r={0};struct lp_packet m=meta(1,UINT64_C(1000000000000)),decoded;uint8_t wire[112];
 m.scope.root_boot=9;m.scope.epoch=7;
 assert(!lp_encode(wire,sizeof wire,&m,NULL,&group));assert(!lp_decode(&decoded,wire,sizeof wire,NULL,&group));
 assert(decoded.scope.boot==2&&decoded.scope.root_boot==9&&lp_rx_meta(&r,&decoded,&group,NULL,0,1000));
 m.scope.root_boot=10;m.scope.epoch=3;m.scope.stream=2;++m.serial;m.utc_ns+=1000000000;
 assert(lp_rx_meta(&r,&m,&group,NULL,1000000,1001000));
 m.scope.epoch=2;m.scope.stream=3;++m.serial;m.utc_ns+=1000000000;
 assert(!lp_rx_meta(&r,&m,&group,NULL,2000000,2001000)); /* Same incarnation cannot roll its epoch back. */
 m.scope.root_boot=11;assert(lp_rx_meta(&r,&m,&group,NULL,2000000,2001000));
 m.scope.root_boot=9;m.scope.stream=1;++m.serial;m.utc_ns+=1000000000;
 assert(!lp_rx_meta(&r,&m,&group,NULL,3000000,3001000)); /* Retired stream cannot return. */
}
int main(int argc,char **argv){(void)argc;(void)argv;codec();ledger(0);ledger(59900000);receiver();runtime_root();puts("PASS passive codec, persistent admission and physical-event receiver");return 0;}
