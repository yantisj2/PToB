// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Real authenticated codec and N30 core; synthetic observations only.
#include "lora_core.h"
#include "lora_passive.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const struct r2_group_config group={.id=11,.epoch=1,.psk={1,2,3}};
static const uint64_t Base=UINT64_C(1700000000000000000);
static uint32_t gen;
static struct n30_lora_view view;
static struct lp_scope scope(uint64_t emitter){return (struct lp_scope){.emitter=emitter,.boot=22,.root=emitter,.root_boot=22,.epoch=5,.stream=1,.seed=7,.kind=1};}
static void sample(uint64_t at){n30_rc8_clock(at*16,7,at);n30_rc8_snapshot(&view,at);assert(!view.authority&&!view.assurance);}
static void reset(uint64_t hint){assert(n30_rc8_init(99,hint,group.id,group.epoch,group.psk));gen=n30_rc8_switch(true);}
static bool packet(struct lp_scope s,unsigned kind,uint32_t serial,uint32_t cursor,uint64_t utc,uint64_t at,bool valid,bool corrupt){
 uint8_t context[16],wire[112];assert(!lp_context(context,&s,8,&group));
 struct lp_packet p={.kind=kind,.schedule=8,.scope=s,.serial=serial,.cursor=cursor,.utc_ns=utc,.time_valid=valid,.tz={1,0}};
 assert(!lp_encode(wire,sizeof wire,&p,context,&group));const unsigned size=kind==LP_META?112:kind==LP_SYNC?24:32;
 if(corrupt)wire[size-1]^=1;
 const bool result=n30_rc8_receive(wire,size,at*16,7,at,at,gen);sample(at);return result;
}
static bool meta(struct lp_scope s,uint32_t serial,uint32_t cursor,uint64_t at,bool valid){
 sample(at);const uint32_t previous=view.metadata;
 assert(!packet(s,LP_META,serial,cursor,Base+at*1000,at,valid,false));return view.metadata==previous+1;
}
static void sync(struct lp_scope s,uint32_t cursor,uint64_t at){assert(!packet(s,LP_SYNC,0,cursor,0,at,false,false));}
static bool follow(struct lp_scope s,uint32_t cursor,uint64_t edge){return packet(s,LP_FOLLOW,0,cursor,Base+edge*1000-99328000,edge+2500000,false,false);}
static void pair(struct lp_scope s,uint32_t cursor,uint64_t edge){sync(s,cursor,edge);assert(follow(s,cursor,edge));assert(view.watch_valid);}
int main(void){
 const struct lp_scope a=scope(1),b=scope(2),c=scope(3),d=scope(4),e=scope(5),self=scope(99);
 reset(0);assert(meta(a,1,0,1000000,true));
 for(unsigned i=0;i<8;++i)pair(a,2*i,2000000+UINT64_C(30000000)*i);
 assert(view.model_valid&&view.trained&&view.watch_pairs==8&&view.watch_prediction_n==7);
 // A fresh META on the same context preserves its actual model/history.
 assert(meta(a,2,16,216000000,true));assert(view.model_valid&&view.watch_valid&&view.watch_pairs==8);
 sync(a,16,217000000); // Pending A original must not pair after selecting B.
 assert(meta(b,1,0,218000000,true));
 assert(view.emitter==2&&view.metadata_known&&!view.model_valid&&!view.trained&&!view.watch_valid&&!view.watch_pairs&&!view.watch_prediction_n&&!view.link_samples);
 assert(!follow(a,16,217000000)&&!view.watch_valid);
 assert(!follow(b,0,219000000)&&!view.watch_valid); // A's pending RX is not B's RX.
 pair(b,0,222000000);assert(view.watch_pairs==1&&!view.watch_prediction_n&&!view.model_valid);
 // A's replay floor retains consumed cursor14 independently of B's floor.
 assert(!meta(a,3,14,226000000,true));assert(view.emitter==2);
 assert(meta(a,3,16,227000000,true));assert(!view.watch_valid&&!view.watch_pairs&&!view.model_valid);
 pair(a,16,228000000);
 // B also rejects its consumed cursor0 after returning from A. This catches
 // accidentally advancing only replay slot0 instead of the selected emitter.
 assert(!meta(b,2,0,232000000,true));assert(meta(b,2,2,233000000,true));pair(b,2,234000000);
 gen=n30_rc8_switch(false);sample(238000000);assert(!view.watch_valid&&!view.metadata_known);
 gen=n30_rc8_switch(true);
 assert(!meta(a,3,16,239000000,true)); // Exact serial replay survived OFF/ON.
 assert(!meta(a,4,16,240000000,true)); // Consumed pair cursor survived OFF/ON.
 assert(meta(a,4,18,241000000,true));assert(!view.watch_valid);pair(a,18,242000000);
 // Boot/source/context changes require new physical evidence, not an old
 // paired model or pending original. Retired boots cannot return later.
 struct lp_scope reboot=a;reboot.boot=23;reboot.root_boot=23;
 assert(meta(reboot,1,0,246000000,true));assert(!view.watch_valid&&!view.model_valid&&!view.watch_pairs);
 assert(!meta(a,5,20,247000000,true));pair(reboot,0,248000000);
 assert(meta(reboot,2,2,252000000,false));assert(!view.time_valid&&!view.watch_valid);
 assert(meta(reboot,3,2,253000000,true));assert(!view.watch_valid);pair(reboot,2,254000000);
 // No legacy factory hint pins selection, including a zero or self hint.
 reset(1);assert(meta(b,1,0,1000000,true));pair(b,0,2000000);
 reset(99);assert(meta(a,1,0,1000000,true));
 // Invalid MAC and self emitter do not consume a slot.
 reset(0);assert(!meta(self,1,0,1000000,true));
 assert(!packet(e,LP_META,1,0,Base+UINT64_C(2000000000),2000000,true,true));assert(!view.metadata);
 assert(meta(a,1,0,3000000,true));assert(meta(b,1,0,4000000,true));assert(meta(c,1,0,5000000,true));assert(meta(d,1,0,6000000,true));
 assert(!meta(e,1,0,7000000,true)&&view.emitter==4); // Fifth cannot evict any floor.
 gen=n30_rc8_switch(false);gen=n30_rc8_switch(true);
 assert(!meta(e,2,0,8000000,true));assert(!meta(a,1,0,9000000,true));assert(!meta(b,1,0,10000000,true));
 assert(meta(a,2,0,11000000,true));assert(meta(b,2,0,12000000,true));assert(meta(c,2,0,13000000,true));assert(meta(d,2,0,14000000,true));
 // Authenticated observer pairs after META expiry also advance the floor.
 reset(0);assert(meta(a,1,0,1000000,true));pair(a,0,2000000);
 sync(a,2,440000000);assert(!follow(a,2,440000000));assert(view.watch_pairs==2&&view.watch_valid&&!view.metadata_known);
 assert(meta(b,1,0,444000000,true));assert(!meta(a,2,2,445000000,true));assert(meta(a,2,4,446000000,true));
 puts("PASS N30 dynamic LoRa: authenticated emitters,4 non-evicting floors,matching consumed-cursor/serial/retired-boot replay across source/OFFON,legacy0/self hints,self/MAC/full-table refusal,new physical admission,strict/observer model/pending fences,samecontext continuity; synthetic only");
}
