/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "time_zone.h"
#include "gm7_codec.h"
#include "gm7_air.h"
#include "group_join.h"
#include "rc_dynamic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct r2_group_config group(void){
 struct r2_group_config g={.id=0x123456789abcdef0ULL,.epoch=7};
 for(unsigned i=0;i<32;i++)g.psk[i]=(uint8_t)(i+1);return g;
}
static void leases(void){
 uint8_t w[4];r2_tz_put(w,r2_tz_manual());assert(!memcmp(w,"\x02\x00\xd4\xfe",4));
 for(unsigned i=0;i<65536;i++){
  int offset=i<=INT16_MAX?(int)i:-1-(int)(UINT16_MAX-i);
  struct r2_tz t={42,(int16_t)offset};r2_tz_put(w,t);struct r2_tz q=r2_tz_get(w);
  assert(r2_tz_valid(q)==(offset>=-840&&offset<=840));
  if(r2_tz_valid(q))assert(q.id==42&&q.offset_min==offset);
 }
 struct r2_tz_lease a={0},b={0};uint64_t at=1000000;
 assert(r2_tz_observe(&a,r2_tz_manual(),at,at,R2_TZ_GM_PERIOD_US));
 assert(a.until_us==1750000&&r2_tz_current(&a,1749999).id==2);
 assert(!r2_tz_current(&a,1750000).id);
 /* A precision map can expire at 1,010,000 without touching coarse state. */
 assert(r2_tz_current(&a,1200000).id==2);
 assert(r2_tz_observe(&a,(struct r2_tz){2,-360},at+250000,at+250000,R2_TZ_GM_PERIOD_US));
 assert(a.value.offset_min==-360&&a.until_us==2000000);
 assert(r2_tz_observe(&a,r2_tz_manual(),1800000,1800000,R2_TZ_GM_PERIOD_US));
 assert(a.until_us==2550000); /* matching coarse advertisement renews */
 assert(r2_tz_observe(&a,r2_tz_unknown(),1850000,1850000,R2_TZ_GM_PERIOD_US));
 r2_tz_renew(&a,1900000,R2_TZ_GM_PERIOD_US);assert(!r2_tz_current(&a,1900000).id);
 assert(r2_tz_observe(&a,r2_tz_manual(),1950000,1950000,R2_TZ_GM_PERIOD_US));
 assert(!r2_tz_observe(&a,r2_tz_manual(),1000000,1800000,R2_TZ_GM_PERIOD_US));
 assert(r2_tz_observe(&b,r2_tz_current(&a,1800000),1800000,1800000,R2_TZ_RC_PERIOD_US));
 assert(b.until_us==2100000&&b.value.offset_min==-300);
 a=(struct r2_tz_lease){0}; /* Explicit new mesh/configuration. */
 assert(r2_tz_observe(&a,(struct r2_tz){2,-360},2000000,2000000,R2_TZ_GM_PERIOD_US));
 assert(a.value.offset_min==-360);
 /* Existing authenticated parent broadcasts keep coarse configuration alive
  * even when no precision fit/correction is available. */
 r2_tz_renew(&a,3000000,R2_TZ_RC_PERIOD_US);
 assert(r2_tz_current(&a,3299999).id==2 && !r2_tz_current(&a,3300000).id);
 assert(sizeof(struct r2_tz_lease)==16);
 int64_t out=123;assert(r2_tz_local_us(r2_tz_manual(),18000000000LL,&out)&&out==0);
 assert(!r2_tz_local_us(r2_tz_manual(),INT64_MIN,&out));
 assert(!r2_tz_observe(&a,r2_tz_manual(),UINT64_MAX,UINT64_MAX,R2_TZ_GM_PERIOD_US));
}
static void vector(const char *name,const uint8_t *w,size_t n){
 printf("VECTOR %s ",name);for(size_t i=0;i<n;i++)printf("%02x",w[i]);putchar('\n');
}
static void wires(void){
 struct r2_group_config g=group();struct gm7_packet p={.group=g.id,.key_epoch=g.epoch,.kind=GM7_ANNOUNCE,.flags=GM7_ELIGIBLE};
 p.body.control=(struct gm7_control){.sender=11,.boot=22,.sequence=1,.sent_utc_us=1700000000000000ULL,
  .max_age_us=2750000,.source_valid_us=1000000,.source_gen=1,.capture_gen=1,.train_gen=1,
  .birth_us=1700000000000000ULL,.founder=11,.term=1,.active=11,.active_boot=22,.timing_epoch=9,.tz={2,-300}};
 uint8_t ad[GM7_TZ_MAX_AD_BYTES];struct gm7_packet q;
 assert(!gm7_ad_encode(ad,sizeof ad,&p,&g));assert(gm7_wire_size(p.kind)==182);
 assert(!memcmp(ad+166,"\x02\x00\xd4\xfe",4));
 assert(!gm7_ad_decode(&q,ad,sizeof ad,&g));assert(q.body.control.tz.offset_min==-300);
 vector("gm-announce-cdt",ad,sizeof ad);
 struct gm7_job job={.state=GM7_JOB_QUEUED,.length=sizeof ad};
 memcpy(job.ad,ad,sizeof ad);uint8_t primary[15],address[6]={1,2,3,4,5,0xc0},actual_address[6];
 const uint8_t *on_air=NULL;size_t on_air_bytes=0;
 assert(!gm7_air_build(primary,&job,address,5));
 assert(!gm7_air_received(job.pdu_prefix,12+sizeof ad,&on_air,&on_air_bytes,actual_address));
 assert(on_air_bytes==sizeof ad && !memcmp(on_air,ad,sizeof ad) && !memcmp(address,actual_address,6));
 assert(!gm7_ad_decode(&q,on_air,on_air_bytes,&g));
 job.ad[7]='3';assert(gm7_air_received(job.pdu_prefix,12+sizeof ad,&on_air,&on_air_bytes,actual_address));

 for(unsigned i=0;i<sizeof ad;i++){
  ad[i]^=1;assert(gm7_ad_decode(&q,ad,sizeof ad,&g));ad[i]^=1;
 }
 ad[8]=3;assert(gm7_ad_decode(&q,ad,sizeof ad,&g));ad[8]=4;
 struct r2_join_record j={.kind=R2_JOIN_REQUEST,.capabilities=4,.sender=11,.receiver=12,.sender_boot=22,
  .client_nonce=33,.sequence=1,.created_us=100,.name="Synthetic",.tz={2,-300}},k;
 uint8_t join[R2_GROUP_JOIN_BYTES];assert(sizeof join==168);
 assert(!r2_group_join_encode(join,&j,&g));assert(join[2]==5&&!memcmp(join+148,"\x02\x00\xd4\xfe",4));
 assert(!r2_group_join_decode(&k,join,sizeof join,&g));assert(k.tz.offset_min==-300);
 vector("join-request-cdt",join,sizeof join);
 for(unsigned i=0;i<sizeof join;i++){join[i]^=1;assert(r2_group_join_decode(&k,join,sizeof join,&g));join[i]^=1;}
 struct rc7_control c={.type=RC7_CAPABILITY,.sender=11,.sender_boot=22,.next_challenge=33,.tz={2,-300}},d;
 uint8_t cap[RC_DYNAMIC_CAPABILITY_BYTES];assert(sizeof cap==148);
 assert(!rc7_capability_encode(cap,&c,&g));assert(cap[6]==8&&!memcmp(cap+128,"\x02\x00\xd4\xfe",4));
 assert(!rc7_capability_decode(&d,cap,sizeof cap,&g));assert(d.tz.offset_min==-300);
 vector("rc-capability-cdt",cap,sizeof cap);
 for(unsigned i=0;i<sizeof cap;i++){cap[i]^=1;assert(rc7_capability_decode(&d,cap,sizeof cap,&g));cap[i]^=1;}
 uint8_t control[RC_DYNAMIC_CONTROL_BYTES];assert(sizeof control==216);
 assert(!rc7_control_encode(control,&c,&g));
 assert(control[6]==8&&!memcmp(control+196,"\x02\x00\xd4\xfe",4));
 assert(!rc7_control_decode(&d,control,sizeof control,&g)&&d.tz.offset_min==-300);
 vector("rc-control-cdt",control,sizeof control);
 for(unsigned i=0;i<sizeof control;i++){control[i]^=1;assert(rc7_control_decode(&d,control,sizeof control,&g));control[i]^=1;}
 /* Unknown future ID forwards, malformed TZ normalizes without changing base validity. */
 c.tz=(struct r2_tz){49152,345};assert(!rc7_capability_encode(cap,&c,&g));
 assert(!rc7_capability_decode(&d,cap,sizeof cap,&g)&&d.tz.id==49152&&d.tz.offset_min==345);
 c.tz=(struct r2_tz){1,300};assert(!rc7_capability_encode(cap,&c,&g));
 assert(!rc7_capability_decode(&d,cap,sizeof cap,&g)&&!d.tz.id);
}
int main(void){leases();wires();puts("PASS TZ coarse leases 3x cadence, dynamic updates/unknown withdrawal, renewal, two-hop values, signed offsets, overflow and authenticated new-wire mutation checks");}
