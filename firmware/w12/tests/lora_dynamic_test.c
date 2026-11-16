// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual authenticated codec/core; synthetic observations only.
#include "lora_core.h"
#include "lora_passive.h"
#include <assert.h>
#include <stdio.h>
static const struct r2_group_config group={.id=11,.epoch=1,.psk={1,2,3}};
static const uint64_t Base=UINT64_C(1700000000000000000);
static uint32_t gen;
static struct w12_lora_view view;
static struct lp_scope scope(uint64_t emitter){return (struct lp_scope){.emitter=emitter,.boot=22,.root=emitter,.root_boot=22,.epoch=5,.stream=1,.seed=7,.kind=1};}
static void sample(uint64_t at){w12_rc8_clock(at*16,7,at);w12_rc8_snapshot(&view,at);assert(!view.authority&&!view.assurance);}
static void reset(uint64_t hint){assert(w12_rc8_init(99,hint,group.id,group.epoch,group.psk));gen=w12_rc8_switch(true);}
static bool packet(struct lp_scope s,unsigned kind,uint32_t serial,uint32_t cursor,uint64_t utc,uint64_t at,bool valid,bool corrupt){
 uint8_t context[16],wire[112];assert(!lp_context(context,&s,8,&group));
 struct lp_packet p={.kind=kind,.schedule=8,.scope=s,.serial=serial,.cursor=cursor,.utc_ns=utc,.time_valid=valid,.tz={1,0}};
 assert(!lp_encode(wire,sizeof wire,&p,context,&group));const unsigned size=kind==LP_META?112:kind==LP_SYNC?24:32;
 if(corrupt)wire[size-1]^=1;
 const bool result=w12_rc8_receive(wire,size,at*16,7,at,at,gen);sample(at);return result;
}
static bool meta(struct lp_scope s,uint32_t serial,uint32_t cursor,uint64_t at,bool valid){
 sample(at);const uint32_t previous=view.metadata;
 assert(!packet(s,LP_META,serial,cursor,Base+at*1000,at,valid,false));return view.metadata==previous+1;
}
static void sync(struct lp_scope s,uint32_t cursor,uint64_t at){assert(!packet(s,LP_SYNC,0,cursor,0,at,false,false));}
static bool follow(struct lp_scope s,uint32_t cursor,uint64_t edge){return packet(s,LP_FOLLOW,0,cursor,Base+edge*1000-99328000,edge+2500000,false,false);}
static void pair(struct lp_scope s,uint32_t cursor,uint64_t edge){sync(s,cursor,edge);assert(follow(s,cursor,edge));}
int main(void){
 const struct lp_scope a=scope(1),b=scope(2),c=scope(3),d=scope(4),e=scope(5),self=scope(99);
 reset(0);assert(meta(a,1,0,1000000,true));
 for(unsigned i=0;i<8;++i)pair(a,2*i,2000000+UINT64_C(30000000)*i);
 assert(view.trained&&view.model_valid&&view.utc_us);
 assert(meta(a,2,16,216000000,true));assert(view.trained&&view.model_valid); // Same-context refresh.
 sync(a,16,217000000);assert(meta(b,1,0,218000000,true));
 assert(view.emitter==2&&view.metadata_known&&!view.trained&&!view.model_valid&&!view.utc_us&&!view.link_samples);
 assert(!follow(a,16,217000000));assert(!follow(b,0,219000000)); // Pending A cannot become B.
 pair(b,0,222000000);assert(!view.trained&&!view.model_valid);
 assert(!meta(a,3,14,226000000,true));assert(meta(a,3,16,227000000,true));pair(a,16,228000000);
 assert(!meta(b,2,0,232000000,true));assert(meta(b,2,2,233000000,true));pair(b,2,234000000);
 gen=w12_rc8_switch(false);gen=w12_rc8_switch(true);
 assert(!meta(a,3,16,239000000,true));assert(!meta(a,4,16,240000000,true));assert(meta(a,4,18,241000000,true));pair(a,18,242000000);
 struct lp_scope reboot=a;reboot.boot=23;reboot.root_boot=23;
 assert(meta(reboot,1,0,246000000,true));assert(!view.model_valid&&!view.trained&&!view.utc_us);
 assert(!meta(a,5,20,247000000,true));pair(reboot,0,248000000);
 assert(meta(reboot,2,2,252000000,false));assert(!view.time_valid&&!view.utc_us);
 assert(meta(reboot,3,2,253000000,true));assert(!view.model_valid);pair(reboot,2,254000000);
 reset(1);assert(meta(b,1,0,1000000,true));pair(b,0,2000000);
 reset(99);assert(meta(a,1,0,1000000,true)); // Zero and self legacy hints do not pin admission.
 reset(0);assert(!meta(self,1,0,1000000,true));
 assert(!packet(e,LP_META,1,0,Base+UINT64_C(2000000000),2000000,true,true));assert(!view.metadata);
 assert(meta(a,1,0,3000000,true));assert(meta(b,1,0,4000000,true));assert(meta(c,1,0,5000000,true));assert(meta(d,1,0,6000000,true));
 assert(!meta(e,1,0,7000000,true)&&view.emitter==4);
 gen=w12_rc8_switch(false);gen=w12_rc8_switch(true);assert(!meta(e,2,0,8000000,true));assert(!meta(a,1,0,9000000,true));
 assert(meta(a,2,0,10000000,true));assert(meta(b,2,0,11000000,true));assert(meta(c,2,0,12000000,true));assert(meta(d,2,0,13000000,true));
 puts("PASS W12 dynamic authenticated emitters,4 non-evicting floors,matching consumed-cursor/serial/retired-boot replay across source/OFFON,self/MAC/full-table refusal,fresh physical admission,model/pending fences,same-context continuity; synthetic only");
}
