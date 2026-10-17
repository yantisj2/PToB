/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include "../src/usb_source.c"
static struct r2_capture sampled={.tick=50000,.epoch=1};
static bool sampling=true,usb_live=true;
static uint32_t test_usb_epoch=7;
bool r2_capture_now(struct r2_capture *out){*out=sampled;return sampling;}
bool r2_usb_sof_scope(uint32_t *epoch){*epoch=test_usb_epoch;return usb_live;}
uint8_t r2_usb_reference_number(void){return 2;}
static void (*project_hook)(void);
/* Publication tests isolate the already-tested RC map arithmetic boundary. */
bool rc5_map_project(const struct rc5_map *m,uint64_t tick,uint64_t *ns,uint64_t *elapsed){
 assert(!test_critical_depth);
 if(project_hook){void(*hook)(void)=project_hook;project_hook=NULL;hook();}
 if(tick<m->anchor_tick)return false;*elapsed=tick-m->anchor_tick;
 *ns=m->anchor_ns+*elapsed*125/2;return true;
}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static void raw(uint64_t event,uint64_t now){test_raw=(struct r2_usb_sof_snapshot){.current_tick=now,.cc_before=(uint32_t)event,.cc_after=(uint32_t)event,.capture_epoch=1,.frame_before=3,.frame_after=3};}
static unsigned race_reads;
static void one_sof_race(struct r2_usb_sof_snapshot *out){if(!race_reads++)++out->frame_after;}
static void persistent_sof_race(struct r2_usb_sof_snapshot *out){++race_reads;++out->cc_after;}
static void usb_epoch_race(struct r2_usb_sof_snapshot *out){++race_reads;++out->frame_after;++test_usb_epoch;}
int main(void){
 struct r2_identity id={.silicon="1000000000000004",.boot=99};r2_usb_source_init(&id);
 struct r2_uc_clock c={.map={.effective=40000,.anchor_tick=40000,.anchor_ns=UINT64_C(1700000000000000000),.generation=1,.bound=UINT32_MAX,.rate_bound=UINT32_MAX,.capture_bound=UINT32_MAX},.origin={.root=17,.boot=18,.epoch=19,.expires=UINT64_C(1700000010000000000)},.until_tick=500000,.capture_epoch=1,.valid=true};
 struct usb_setup_packet s={0xc1,0x52,0,2,256};int32_t length;uint8_t *p;
 raw(45000,51000);assert(!r2_usb_source_request(&s,&length,&p)&&!get(p+36,4));
 r2_usb_source_publish(&c);
 assert(!r2_usb_source_request(&s,&length,&p)&&length==256&&!memcmp(p,"UTS1",4)&&get(p+36,4)==1&&get(p+100,4)==1&&get(p+24,4)==7);
 assert(get(p+8,8)==UINT64_C(0x1000000000000004)&&get(p+80,8)==17&&get(p+172,4)==UINT32_MAX);
 s.bRequest=0x53;assert(!r2_usb_source_request(&s,&length,&p)&&!get(p+36,4)); /* event predates publication */
 r2_usb_source_begin(60000,1);raw(59000,63000);assert(!r2_usb_source_request(&s,&length,&p)&&!get(p+36,4)&&get(p+220,4)==4);
 sampled.tick=62000;c.map.generation=2;c.map.effective=60000;r2_usb_source_publish(&c);
 assert(!r2_usb_source_request(&s,&length,&p)&&get(p+36,4)&&get(p+100,4)==1); /* retained old applicable map */
 raw(61000,63000);assert(!r2_usb_source_request(&s,&length,&p)&&!get(p+36,4)); /* explicit publication gap */
 assert(get(p+220,4)==7&&!get(p+64,8)&&!memcmp(p+232,"USM1",4)&&get(p+236,2)==1&&get(p+238,2)==24&&get(p+248,4)==1&&!get(p+252,4));
 assert(get(p+80,8)==17&&get(p+100,4)==2&&get(p+144,8)==62000&&get(p+48,8)==61000&&get(p+56,8)==63000);
 assert(get(p+240,8)==c.map.anchor_ns+(63000-c.map.anchor_tick)*125/2); /* SOURCE, not captured UTC */
 struct published_map saved[2];memcpy(saved,maps,sizeof saved);
 r2_usb_source_withdraw();assert(!r2_usb_source_request(&s,&length,&p)&&get(p+220,4)==1&&!get(p+232,4));
 memcpy(maps,saved,sizeof maps);permission=true;
 busy=true;assert(!r2_usb_source_request(&s,&length,&p)&&get(p+220,4)==4&&!get(p+232,4));busy=false;
 maps[0].clock.until_tick=63000;assert(!r2_usb_source_request(&s,&length,&p)&&get(p+220,4)==1&&!get(p+232,4));memcpy(maps,saved,sizeof maps);
 maps[0].clock.origin.expires=c.map.anchor_ns+(63000-c.map.anchor_tick)*125/2;
 assert(!r2_usb_source_request(&s,&length,&p)&&get(p+220,4)==1&&!get(p+232,4));memcpy(maps,saved,sizeof maps);
 maps[0].clock.map.anchor_tick=64000;assert(!r2_usb_source_request(&s,&length,&p)&&get(p+220,4)==1&&!get(p+232,4));memcpy(maps,saved,sizeof maps);
 test_raw.capture_epoch=2;assert(!r2_usb_source_request(&s,&length,&p)&&get(p+220,4)==1&&!get(p+232,4));test_raw.capture_epoch=1;
 usb_live=false;assert(!r2_usb_source_request(&s,&length,&p)&&get(p+220,4)==2&&!get(p+232,4));usb_live=true;
 s.bRequest=0x52;assert(!r2_usb_source_request(&s,&length,&p)&&get(p+36,4)&&!get(p+232,4));s.bRequest=0x53;
 assert(maps[0].lo==62000&&maps[1].hi==60000&&maps[0].clock.until_tick==500000); /* no interval widening */
 raw(63000,64000);assert(!r2_usb_source_request(&s,&length,&p)&&get(p+36,4)&&get(p+100,4)==2&&get(p+144,8)==62000);
 sampled.tick=65000;c.until_tick=900000;r2_usb_source_publish(&c);raw(70000,71000);
 assert(!r2_usb_source_request(&s,&length,&p)&&get(p+104,8)==500000); /* same generation cannot refresh permission */
 raw(490000,500000);assert(!r2_usb_source_request(&s,&length,&p)&&!get(p+36,4));
 raw(70000,71000);test_raw.capture_epoch=2;assert(!r2_usb_source_request(&s,&length,&p)&&!get(p+36,4));
 test_raw.capture_epoch=1;usb_live=false;assert(!r2_usb_source_request(&s,&length,&p)&&!get(p+36,4));usb_live=true;
 r2_usb_source_withdraw();assert(!r2_usb_source_request(&s,&length,&p)&&!get(p+36,4));
 for(unsigned i=0;i<5;i++){struct usb_setup_packet bad=s;if(i==0)bad.wLength--;else if(i==1)bad.wIndex++;else if(i==2)bad.wValue++;else if(i==3)bad.bRequest++;else bad.bmRequestType=0x41;assert(r2_usb_source_request(&bad,&length,&p)==-ENOTSUP);}
 struct usb_setup_packet marker={0x41,0x54,27,2,0},fetch={0xc1,0x55,27,2,64};
 raw(70000,71000);assert(!r2_usb_anchor_request(&marker,&length,&p)&&length==0);
 raw(80000,81000);assert(!r2_usb_anchor_request(&fetch,&length,&p)&&length==64&&!memcmp(p,"UFA1",4)&&get(p+44,8)==70000&&get(p+52,8)==71000&&get(p+42,2)==27&&get(p+36,4));
 fetch.wValue=28;assert(r2_usb_anchor_request(&fetch,&length,&p)==-ENOTSUP);fetch.wValue=27;
 ++test_usb_epoch;assert(!r2_usb_anchor_request(&fetch,&length,&p)&&!get(p+36,4)&&get(p+60,4)==3);--test_usb_epoch;
 marker.wLength=1;assert(r2_usb_anchor_request(&marker,&length,&p)==-ENOTSUP);marker.wLength=0;
 marker.wValue=0;assert(r2_usb_anchor_request(&marker,&length,&p)==-ENOTSUP);marker.wValue=29;
 usb_live=false;assert(!r2_usb_anchor_request(&marker,&length,&p));usb_live=true;fetch.wValue=29;
 assert(!r2_usb_anchor_request(&fetch,&length,&p)&&!get(p+36,4));
 /* Explicit UTE1 projection survives500/743ms and132s; legacy remains strict. */
 s=(struct usb_setup_packet){0xc1,0x57,0,2,256};sampled.tick=100000;
 c.estimate=true;c.valid=true;c.map.generation=40;c.map.effective=90000;
 c.map.anchor_tick=90000;c.until_tick=8090000;c.discipline_tick=90000;
 c.session_until=UINT64_C(5760090000);c.origin.expires=c.map.anchor_ns+UINT64_C(10000000000);
 r2_usb_source_publish(&c);
 uint64_t initial_age=0;
 for(unsigned i=0;i<4;i++){
  uint64_t gap[]={UINT64_C(100000),UINT64_C(8000000),UINT64_C(11888000),UINT64_C(2112000000)};
  uint64_t now=90000+gap[i];raw(now-1000,now);
  assert(!r2_usb_source_request(&s,&length,&p)&&!memcmp(p,"UTE1",4));
  assert((get(p+36,4)&6)==6&&get(p+64,8)>0&&get(p+232,8)==90000);
  assert((get(p+48,8)-get(p+232,8))>=initial_age);initial_age=get(p+48,8)-get(p+232,8);
  if(i){assert(!(get(p+36,4)&1)&&get(p+248,4)==2);}
  if(i==3)assert(!(get(p+36,4)&16));
 }
 raw(UINT64_C(6000000000),UINT64_C(6000001000));
 assert(!r2_usb_source_request(&s,&length,&p)&&(get(p+36,4)&6)==6&&!(get(p+36,4)&24));
 c.session_until=UINT64_MAX;c.until_tick=UINT64_MAX;sampled.tick=200000;r2_usb_source_publish(&c);
 assert(estimate.clock.session_until==UINT64_C(5760090000)); /* Same map cannot renew lease. */
 r2_usb_source_withdraw();raw(1000000,1001000);
 assert(!r2_usb_source_request(&s,&length,&p)&&(get(p+36,4)&6)==6&&!(get(p+36,4)&25));
 test_raw.capture_epoch=2;assert(!r2_usb_source_request(&s,&length,&p)&&!(get(p+36,4)&2));test_raw.capture_epoch=1;
 raw(80000,81000);assert(!r2_usb_source_request(&s,&length,&p)&&!(get(p+36,4)&2));
 raw(1000000,1001000);usb_live=false;assert(!r2_usb_source_request(&s,&length,&p)&&!get(p+36,4));usb_live=true;
 /* New source is a new segment; no post-publication map applies backwards. */
 sampled.tick=2000000;c.map.generation=41;c.map.effective=1900000;c.map.anchor_tick=1900000;
 c.origin.root=20;c.discipline_tick=1900000;r2_usb_source_publish(&c);
 raw(1999000,2001000);assert(!r2_usb_source_request(&s,&length,&p)&&!(get(p+36,4)&2));
 raw(2001000,2002000);assert(!r2_usb_source_request(&s,&length,&p)&&(get(p+36,4)&2)&&get(p+80,8)==20);
 /* Same-scope preparation is not withdrawal. The old published map remains
  * usable at its ORIGINAL deadlines; a later map never applies backwards. */
 r2_usb_source_withdraw();sampled.tick=3000000;c.map.generation=42;
 c.map.effective=c.map.anchor_tick=c.discipline_tick=2900000;
 c.until_tick=11000000;c.session_until=50000000;r2_usb_source_publish(&c);
 uint32_t before_prepare=revision;
#ifdef R2_USB_LEGACY_HANDOFF
 r2_usb_source_begin(3050000,1);
#else
 r2_usb_source_prepare(3050000,1,&c);
#endif
 assert(!busy&&revision==before_prepare);
 raw(3100000,3101000);assert(!r2_usb_source_request(&s,&length,&p)&&(get(p+36,4)&1)&&get(p+100,4)==42&&get(p+104,8)==11000000);
 c.until_tick=20000000;r2_usb_source_prepare(3101000,1,&c);
 assert(maps[0].clock.until_tick==11000000); /* preparation cannot renew */
 sampled.tick=3200000;c.map.generation=43;c.map.effective=c.map.anchor_tick=c.discipline_tick=3150000;c.until_tick=12000000;
 r2_usb_source_publish(&c);assert(maps[1].hi==3200000&&maps[0].lo==3200000);
 raw(3195000,3201000);assert(!r2_usb_source_request(&s,&length,&p)&&(get(p+36,4)&1)&&get(p+100,4)==42&&get(p+232,8)==2900000);
 raw(3201000,3202000);assert(!r2_usb_source_request(&s,&length,&p)&&(get(p+36,4)&1)&&get(p+100,4)==43);
 race_reads=0;test_sample_hook=one_sof_race;
 assert(!r2_usb_source_request(&s,&length,&p)&&(get(p+36,4)&1)&&race_reads==2);
 race_reads=0;test_sample_hook=persistent_sof_race;
 assert(!r2_usb_source_request(&s,&length,&p)&&!(get(p+36,4)&1)&&race_reads==2);
 race_reads=0;test_sample_hook=usb_epoch_race;
 assert(!r2_usb_source_request(&s,&length,&p)&&!(get(p+36,4)&1)&&race_reads==1);--test_usb_epoch;
 test_sample_hook=NULL;
 maps[1].clock.until_tick=3201000;raw(3195000,3201000);
 assert(!r2_usb_source_request(&s,&length,&p)&&!(get(p+36,4)&1));
 ++c.capture_epoch;r2_usb_source_prepare(3300000,2,&c);assert(busy);
 raw(3300000,3301000);assert(!r2_usb_source_request(&s,&length,&p)&&!(get(p+36,4)&1));
 --c.capture_epoch;++c.origin.boot;sampled.tick=3400000;c.map.generation=44;r2_usb_source_publish(&c);
 raw(3395000,3401000);assert(!r2_usb_source_request(&s,&length,&p)&&!(get(p+36,4)&1));
 sequence=UINT32_MAX;assert(!r2_usb_source_request(&s,&length,&p)&&!(get(p+36,4)&3)&&!get(p+64,8)&&!get(p+248,4));
 printf("PASS causal UTC/SOF map publication, old generation, explicit gap, expiry, capture/USB/withdraw fences, request guards; map bank %zuB\n",sizeof maps);
 return 0;
}
