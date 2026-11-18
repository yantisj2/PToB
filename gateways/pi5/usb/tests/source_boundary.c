/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Deterministic scalar snapshots against the unchanged public USB source.
 * A passing admission case documents existing behavior, not a correct physical
 * association or permission to install firmware. No hardware is accessed.
 */
#include <stdio.h>
#include "../../../../firmware/t096/src/usb_source.c"

static struct r2_capture sampled={.tick=100000,.epoch=1};
static uint32_t usb_epoch=7;
bool r2_capture_now(struct r2_capture *out){*out=sampled;return true;}
bool r2_usb_sof_scope(uint32_t *epoch){*epoch=usb_epoch;return true;}
uint8_t r2_usb_reference_number(void){return 2;}
bool rc5_map_project(const struct rc5_map *m,uint64_t tick,uint64_t *ns,uint64_t *elapsed){
 assert(!test_critical_depth);
 if(tick<m->anchor_tick)return false;
 *elapsed=tick-m->anchor_tick;*ns=m->anchor_ns+*elapsed*125/2;return true;
}
static unsigned cases,hook_reads;
static struct usb_setup_packet request={0xc1,0x53,0,2,256};
static void snapshot(uint16_t frame,uint64_t event,uint32_t age){
 test_sample_hook=NULL;hook_reads=0;test_sample_count=0;
 test_raw=(struct r2_usb_sof_snapshot){.current_tick=event+age,
  .cc_before=(uint32_t)event,.cc_after=(uint32_t)event,
  .capture_epoch=1,.frame_before=frame,.frame_after=frame};
}
static void check(bool valid,unsigned reads,uint32_t reason){
 int32_t n;uint8_t *p;
 assert(!r2_usb_source_request(&request,&n,&p)&&n==256);
 assert(!!sys_get_le32(p+36)==valid&&sys_get_le32(p+220)==reason);
 assert(test_sample_count==reads);++cases;
}
static void first_frame_change(struct r2_usb_sof_snapshot *r){if(!hook_reads++)++r->frame_after;}
static void first_cc_change(struct r2_usb_sof_snapshot *r){if(!hook_reads++)++r->cc_after;}
static void persistent_cc_change(struct r2_usb_sof_snapshot *r){++hook_reads;++r->cc_after;}
static void epoch_change(struct r2_usb_sof_snapshot *r){(void)r;++hook_reads;++usb_epoch;}
int main(void){
 struct r2_identity id={.silicon="1000000000000004",.boot=99};r2_usb_source_init(&id);
 struct r2_uc_clock c={.map={.effective=100000,.anchor_tick=100000,
   .anchor_ns=UINT64_C(1700000000000000000),.generation=1},
  .origin={.root=17,.boot=18,.epoch=19,.expires=UINT64_C(1700000010000000000)},
  .until_tick=UINT64_C(10000000),.capture_epoch=1,.valid=true};
 r2_usb_source_publish(&c);
 /* Age alone cannot distinguish a legitimate near-end-of-frame snapshot from
  * a stable new frame label paired with the previous capture. Both pass. */
 snapshot(10,200000,0);check(true,1,0);
 snapshot(10,200000,15990);check(true,1,0);
 snapshot(10,200000,15999);check(true,1,0);
 snapshot(11,200000,15990);check(true,1,0); /* stable label advanced first */
 snapshot(10,216000,3);check(true,1,0); /* stable capture advanced first */
 snapshot(10,200000,16000);check(false,2,2);
 snapshot(10,200000,16001);check(false,2,2);
 snapshot(2047,200000,15999);check(true,1,0);
 snapshot(2048,200000,3);check(false,2,2);
 /* A transition visible between scalar reads is detected and retried once. */
 snapshot(11,216000,5);test_sample_hook=first_frame_change;check(true,2,0);
 snapshot(11,216000,5);test_sample_hook=first_cc_change;check(true,2,0);
 snapshot(11,216000,5);test_sample_hook=persistent_cc_change;check(false,2,2);
 snapshot(11,216000,5);test_sample_hook=epoch_change;check(false,1,2);
 snapshot(11,216000,5);test_raw.capture_epoch=2;check(false,1,1);
 /* CC low32 wrap reconstructs the same extended event, not a new epoch. */
 snapshot(0,UINT64_C(0x100000000)+200000,15990);
 c.map.generation=2;c.map.effective=c.map.anchor_tick=UINT64_C(0x100000000);
 sampled.tick=c.map.anchor_tick;c.until_tick=c.map.anchor_tick+10000000;
 r2_usb_source_publish(&c);check(true,1,0);
 assert(sys_get_le64(wire+48)==UINT64_C(0x100000000)+200000);
 /* UFA1 uses the same scalar guard: a stable mismatch is also admitted.
  * Fetch is immutable and independently rejects USB/capture epoch changes. */
 snapshot(1,c.map.anchor_tick+216000,15990);
 struct usb_setup_packet marker={0x41,0x54,27,2,0},fetch={0xc1,0x55,27,2,64};
 int32_t n;uint8_t *p;
 assert(!r2_usb_anchor_request(&marker,&n,&p)&&n==0);
 assert(!r2_usb_anchor_request(&fetch,&n,&p)&&sys_get_le32(p+36)==1);++cases;
 ++usb_epoch;assert(!r2_usb_anchor_request(&fetch,&n,&p)&&!sys_get_le32(p+36)&&sys_get_le32(p+60)==3);++cases;
 --usb_epoch;++sampled.epoch;
 assert(!r2_usb_anchor_request(&fetch,&n,&p)&&!sys_get_le32(p+36)&&sys_get_le32(p+60)==3);++cases;
 printf("PASS %u exact-source scalar boundary cases; stable frame/CC mismatch admission reproduced; no physical race ordering inferred\n",cases);
}
