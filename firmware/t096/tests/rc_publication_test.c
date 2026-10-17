/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis. Production commit/USB publication test. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
#include "radio_capture.h"
#include "timing_events.h"
#include "../src/usb_source.c"
static struct rc5_state timing;
static struct r2_radio_capture_stats delivery_capture_baseline;
static struct rc5_clock local_clock;
static struct rc5_origin current_origin;
static struct rc7_path upstream_path;
static struct rc5_routes routes;
static struct r2_group_config group;
static struct r2_capture sampled={.tick=100000,.epoch=1};
static uint64_t discipline_at,discipline_gap_max,phase_rx,last_context_progress,usable_tx_from;
static uint64_t last_rx,delivery_packet_tuple,delivery_correction_tuple[2],delivery_origin_issued,delivery_origin_expires,delivery_discipline_tuple;
static uint32_t rx,wire_rejected,capture_epoch=1,holdover_expiries,holdover_recoveries,delivery_packet_count,delivery_discipline_rx;
static unsigned sent_count,parent_index;
static bool holdover_expired,transmitter=true,sampling=true;
static bool delivery_source_seen,delivery_source_lost;
#define RC_FIELD_HOLD_TICKS UINT64_C(8000000)
bool r2_capture_now(struct r2_capture *out){*out=sampled;return sampling;}
bool r2_usb_sof_scope(uint32_t *epoch){*epoch=7;return true;}
uint8_t r2_usb_reference_number(void){return 2;}
bool rc7_path_same(const struct rc7_path *a,const struct rc7_path *b){return !memcmp(a,b,sizeof *a);}
bool rc7_path_valid(const struct rc7_path *a){(void)a;return false;}
int rc7_path_mac(uint8_t out[16],const struct rc7_path *p,const struct r2_group_config *g){(void)out;(void)p;(void)g;return -1;}
bool rc5_map_project(const struct rc5_map *m,uint64_t tick,uint64_t *ns,uint64_t *elapsed){
 if(tick<m->anchor_tick)return false;*elapsed=tick-m->anchor_tick;*ns=m->anchor_ns+*elapsed*125/2;return true;
}
void r2_radio_capture_stats(struct r2_radio_capture_stats *s){memset(s,0,sizeof *s);}
#define r2_timing_event record_event
static unsigned source_lost_events,source_recovered_events;
static void record_event(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b){
 (void)p;(void)d;(void)a;(void)b;
 if(k==R2_TIMING_RC_DELIVERY&&d==1){source_lost_events+=r==3;source_recovered_events+=r==4;}
}
#include "rc_publication_under_test.h"
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static uint8_t *read_source(uint64_t event,uint64_t now){
 test_raw=(struct r2_usb_sof_snapshot){.current_tick=now,.cc_before=(uint32_t)event,.cc_after=(uint32_t)event,.capture_epoch=1,.frame_before=3,.frame_after=3};
 static struct usb_setup_packet s={0xc1,0x53,0,2,256};int32_t len;uint8_t *p;assert(!r2_usb_source_request(&s,&len,&p));return p;
}
static void next_map(uint64_t completed,uint32_t generation){
 timing.reference=(struct rc5_reference){.completed_at=completed,.rx=completed-1000000,.valid=true};
 local_clock.map[(local_clock.head+3)%4]=(struct rc5_map){.effective=completed,.anchor_tick=completed,.anchor_ns=1000000000,.generation=generation};
 sampled.tick=completed+1000; /* actual publication62.5us after completion */
 r2_usb_source_begin(completed+500,1);discipline_commit(completed);
}
int main(void){
 struct r2_identity id={.silicon="1000000000000004",.boot=99};r2_usb_source_init(&id);
 local_clock.count=1;local_clock.head=1;timing.bound=true;timing.context_until=UINT64_MAX;
 current_origin=(struct rc5_origin){.root=17,.boot=18,.epoch=19,.issued=1,.expires=1000000000000,.utc_offset=100000000000};
 local_clock.root_boot=current_origin.boot;local_clock.root_epoch=current_origin.epoch;
 timing.parent_boot=local_clock.parent_boot=20;timing.parent_epoch=local_clock.parent_epoch=21;
 delivery_packet_tuple=55;rx=7;
 next_map(100000,1);
 assert(discipline_at==100000&&!holdover_expired&&delivery_discipline_rx==7&&delivery_discipline_tuple==55);
 /* A newly committed map must be exposed BEFORE any following control work.
  * The former loop-end publication leaves this request busy/unavailable. */
 uint8_t *p=read_source(sampled.tick+16000,sampled.tick+16001);
 assert(get(p+36,4)&&get(p+100,4)==1&&get(p+144,8)==101000);
 assert(get(p+104,8)==8100000);
 /* Causal reproduction of65's77s boundary: new discipline125us before old
  * expiry. Publication happens62.5us later, then unrelated control work takes
  *3.125ms. The already-published new map covers subsequent SOFs. */
 next_map(8098000,2);sampled.tick+=50000;
 p=read_source(8100000,8100100);assert(get(p+36,4)&&get(p+100,4)==2);
 assert(get(p+144,8)==8099000&&get(p+104,8)==16098000);
 struct r2_uc_clock c=clock_offer(sampled.tick);c.until_tick+=1000000;r2_usb_source_publish(&c);
 assert(maps[0].clock.until_tick==16098000); /* same generation never extends */
 /* No retrospective authorization across the real begin/publication gap. */
 p=read_source(8098750,8100100);assert(!get(p+36,4)&&get(p+220,4)==7);
 /* A real correction gap remains a gap; commit does not backdate lo/expiry. */
 next_map(16100000,3);p=read_source(16099000,16102000);assert(!get(p+36,4));
 /* Original context is the tighter deadline. No new500ms lease is minted. */
 timing.context_until=16200000;next_map(16110000,4);
 p=read_source(16150000,16150010);assert(get(p+36,4)&&get(p+104,8)==16200000);
 p=read_source(16200000,16200001);assert(!get(p+36,4));
 /* Source expiry is independently enforced, even inside map/context hold. */
 timing.context_until=UINT64_MAX;current_origin.expires=1000100000;next_map(20000000,5);
 p=read_source(20002000,20002001);assert(!get(p+36,4));
 c=clock_offer(20002001);delivery_source_poll(&c,20002001);
 assert(source_lost_events==1&&delivery_source_lost);
 delivery_source_poll(&c,20002002);assert(source_lost_events==1); /* no periodic spam */
 current_origin.expires=1000000000000;next_map(20100000,6);
 assert(source_recovered_events==1&&!delivery_source_lost);
 holdover_expired=true;c=clock_offer(sampled.tick);assert(!c.valid);r2_usb_source_publish(&c);assert(!permission);
 holdover_expired=false;++local_clock.root_epoch;c=clock_offer(sampled.tick);
 assert(!c.valid&&!c.estimate); /* Retained maps cannot cross a source scope. */
 --local_clock.root_epoch;++local_clock.parent_epoch;c=clock_offer(sampled.tick);
 assert(!c.valid&&!c.estimate); /* Nor an authenticated parent incarnation. */
 --local_clock.parent_epoch;
 holdover_expired=false;sampling=false;usb_offer_now();assert(!permission&&!maps[0].set);
 puts("PASS production immediateUSB commit before control delay; real500ms/source/context expiry, no backdating, no same-generation extension, capture failure withdrawal");
}
