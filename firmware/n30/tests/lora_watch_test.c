// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Real MAC/codec and actual N30 observer; no hardware or UTC qualification.
#include "lora_core.h"
#include "lora_passive.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const struct r2_group_config group={.id=11,.epoch=1,.psk={1,2,3}};
static struct lp_scope scope;
static const uint64_t base=UINT64_C(1700000000000000000);
static uint8_t context[16],wire[112];static uint32_t gen;static uint64_t now;static struct n30_lora_view v;
static uint64_t utc(uint64_t us){return base+us*1000-us*2700/1000000;}
static void encode(struct lp_packet p){assert(!lp_encode(wire,sizeof wire,&p,context,&group));}
static void snapshot(uint64_t at,uint64_t epoch){now=at;n30_rc8_clock(at*16,epoch,at);n30_rc8_snapshot(&v,at);}
static void meta(unsigned serial,unsigned cursor,bool valid,uint64_t at,uint64_t epoch){
 struct lp_packet p={.kind=LP_META,.schedule=8,.scope=scope,.serial=serial,.cursor=cursor,.utc_ns=utc(at),.tz={2,-360},.time_valid=valid};encode(p);assert(!n30_rc8_receive(wire,112,at*16,epoch,at,at,gen));snapshot(at,epoch);
}
static void setup(void){scope=(struct lp_scope){.emitter=1,.boot=22,.root=1,.root_boot=22,.epoch=5,.stream=1,.seed=7,.kind=1};assert(!lp_context(context,&scope,8,&group));assert(n30_rc8_init(2,1,11,1,group.psk));gen=n30_rc8_switch(true);meta(1,0,true,1000000,7);}
static void pair(unsigned cursor,uint64_t edge,uint64_t epoch,bool qualified_accept){
 n30_rc8_clock(edge*16,epoch,edge);struct lp_packet p={.kind=LP_SYNC,.schedule=8,.cursor=cursor};encode(p);assert(!n30_rc8_receive(wire,24,edge*16,epoch,edge,edge,gen));
 p.kind=LP_FOLLOW;p.utc_ns=utc(edge)-99328000;encode(p);now=edge+2500000;assert(n30_rc8_receive(wire,32,now*16,epoch,now,now,gen)==qualified_accept);snapshot(now,epoch);
}
int main(void){
 setup();pair(0,2000000,7,true);assert(v.watch_valid&&v.watch_utc_us&&!v.utc_us&&!v.until_us&&!v.model_valid&&v.watch_pairs==1&&v.watch_prediction_n==0&&v.watch_rms_ns==UINT32_MAX&&v.authority==0&&v.assurance==0);
 pair(2,32000000,7,true);assert(v.watch_valid&&v.watch_prediction_n==1&&v.watch_last_ns>=80000&&v.watch_last_ns<=82000&&v.watch_rms_ns>=80000&&!v.trained);
 for(unsigned i=2;i<17;i++)pair(2*i,2000000+UINT64_C(30000000)*i,7,i<14);
 assert(v.accepted==14&&v.watch_pairs==17&&v.watch_prediction_n==16&&v.watch_valid&&v.metadata_cached&&!v.metadata_known&&!v.time_valid&&!v.utc_us&&!v.until_us&&!v.model_valid&&v.authority==0);
 assert(v.tz_id==2&&v.tz_offset_min==-360&&v.watch_max_ns>=80000); // no hidden first-pair forecast
 const uint32_t pairs=v.watch_pairs;struct lp_packet bad={.kind=LP_SYNC,.schedule=8,.cursor=34};encode(bad);wire[20]^=1;assert(!n30_rc8_receive(wire,24,(now+1)*16,7,now+1,now+1,gen));snapshot(now+2,7);assert(v.watch_pairs==pairs&&v.watch_valid);
 gen=n30_rc8_switch(false);snapshot(now+1,7);assert(!v.watch_valid&&v.metadata_cached&&v.tz_id==2&&v.tz_offset_min==-360);
 gen=n30_rc8_switch(true);pair(34,486000000,7,false);assert(!v.watch_valid&&v.metadata_cached); // retained context alone cannot readmit
 meta(2,34,true,489000000,7);pair(34,490000000,7,true);assert(v.watch_valid&&v.watch_pairs==1&&!v.watch_prediction_n&&v.watch_rms_ns==UINT32_MAX);
 snapshot(now+1,8);assert(!v.watch_valid&&v.metadata_cached&&v.watch_reason==4);
 meta(3,36,true,494000000,8);pair(36,495000000,8,true);assert(v.watch_valid&&v.watch_pairs==1);
 meta(4,38,false,499000000,8);assert(!v.watch_valid&&v.metadata_cached&&v.watch_reason==3&&!v.time_valid);
 scope.stream=2;assert(!lp_context(context,&scope,8,&group));meta(5,40,true,500000000,8);assert(v.stream==2&&!v.watch_valid&&!v.watch_prediction_n); // no old clock in a new scope
 setup();for(unsigned i=0;i<8;i++)pair(2*i,2000000+UINT64_C(30000000)*i,7,true);
 const uint32_t count=v.watch_prediction_n,last_rms=v.watch_rms_ns;const uint64_t source_until=v.until_us;
 snapshot(302000000,7);assert(v.watch_valid&&v.watch_state==3&&v.watch_age_ms==90000&&v.watch_prediction_n==count&&v.watch_rms_ns==last_rms&&v.until_us<=source_until);
 snapshot(1000000000,7);assert(v.watch_valid&&v.watch_state==3&&v.metadata_cached&&!v.metadata_known&&!v.utc_us&&!v.until_us&&v.watch_age_ms==788000&&v.watch_error_age_ms==788000&&v.watch_prediction_n==count&&v.watch_rms_ns==last_rms);
 meta(2,16,true,1001000000,7);assert(v.watch_valid&&v.watch_pairs==8&&v.watch_prediction_n==count); // same-context refresh preserves history
 pair(16,1002000000,7,true);assert(v.watch_valid&&v.watch_pairs==9&&v.watch_prediction_n==count+1&&!v.utc_us&&!v.model_valid); // only original fresh qualification may resume
 n30_rc8_radio_fault();snapshot(now+1,7);assert(!v.watch_valid&&v.metadata_cached);
 puts("PASS actual authenticated observer: first-pair unqualified time/pair2 causalerror, expiredMETA continuingMACpairs, tamper, OFF/freshadmission, counter/source/contextfences, 2lostpairs/HOLD, longabsence andsameMETAreacquisition; grantsneverrenewed");
}
