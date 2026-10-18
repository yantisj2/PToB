/* Copyright (c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
#include "rc_transport_hold.h"
#define RC_FIELD_HOLD_TICKS UINT64_C(8000000)
#define BT_DATA_MANUFACTURER_DATA 255
static bool transmitter=true,parent_known=true,parent_synced=true,holdover_expired;
static struct rc5_rate_fit fit={.valid=true};
static struct rc5_clock local_clock;
static struct rc5_state timing;
static struct rc5_routes routes;
static struct rc7_path upstream_path,publication_path;
static struct rc5_origin current_origin;
static struct rc7_packet published;
static uint8_t current_proof[16],wire[190];
static struct r2_group_config group={.id=7,.epoch=9,.psk={1,2,3}};
static uint64_t self_identity=5,local_boot=6,fake_tick,discipline_at,publish_ticks;
static uint32_t train=1,publication=1,capture_epoch=7;
static unsigned node=1,sent_count;
static struct rc5_correction sent[2];
static void *pa=(void*)1;
struct r2_capture {uint64_t tick;};
static bool r2_capture_now(struct r2_capture *c){c->tick=fake_tick;return true;}
static void holdover_poll(uint64_t t){if(t-discipline_at>=RC_FIELD_HOLD_TICKS)holdover_expired=true;}
static void check(bool ok,const char *why){(void)why;assert(ok);}
struct bt_data {unsigned type,data_len;const uint8_t *data;};
static int bt_le_per_adv_set_data(void *p,const struct bt_data *d,unsigned n){
 assert(p&&n==1&&d->type==255&&d->data_len==188);
 wire[0]=189;wire[1]=255;memcpy(wire+2,d->data,188);return 0;
}
#include "rc_transport_publication_under_test.h"
static void map_at(uint64_t tick,uint64_t ns){
 local_clock=(struct rc5_clock){.count=1,.head=1,.hold_ticks=RC_FIELD_HOLD_TICKS,
  .root_boot=2,.root_epoch=3,.parent_boot=2,.parent_epoch=4,
  .map={{.effective=tick,.anchor_tick=tick,.anchor_ns=ns,.generation=1,
   .bound=RC5_UNKNOWN,.capture_bound=RC5_UNKNOWN,.rate_bound=RC5_UNKNOWN,.continuous=true}}};
 discipline_at=tick;holdover_expired=false;
}
static struct rc7_packet decode(void){
 struct rc7_packet p;assert(!rc7_decode(&p,wire,sizeof wire,&publication_path,&group));return p;
}
static void count0(struct rc7_receiver *child,uint16_t event,uint64_t until){
 publish();struct rc7_packet p=decode();assert(!p.count);
 // The canonical count0 packet retains the ORIGINAL signed provenance and
 // path/train. Both correction slots are actually zero on the wire.
 for(unsigned i=118;i<172;i++)assert(!wire[i]);
 assert(p.origin.root==current_origin.root&&p.origin.boot==current_origin.boot&&
  p.origin.epoch==current_origin.epoch&&p.origin.issued==current_origin.issued&&
  p.origin.expires==current_origin.expires&&p.origin.utc_offset==current_origin.utc_offset&&
  p.origin.frame==current_origin.frame&&p.origin.hold_us==current_origin.hold_us&&p.origin.hold_ns==current_origin.hold_ns);
 assert(!memcmp(p.proof,current_proof,16));
 assert(p.train==train&&p.epoch==capture_epoch&&p.sender==self_identity);
 uint32_t accepted=child->clock.accepted;
 assert(!rc7_receive(child,&p,fake_tick,event,2,fake_tick+1));
 assert(child->clock.accepted==accepted&&child->clock.context_until==until);
 // Canonical zeros and MAC are checked by the production decoder.
 wire[118]=1;assert(rc7_decode(&p,wire,sizeof wire,&publication_path,&group));
}
int main(void){
 fake_tick=16000000;map_at(fake_tick,1000000000);
 upstream_path=(struct rc7_path){.source_epoch=3,.node={{.id=1,.boot=2,.train=1,.epoch=4}}};
 routes=(struct rc5_routes){.count=2,.node={{.id=1,.transmitter=true},
  {.id=5,.parent=0,.slot=1,.depth=1,.transmitter=true}}};
 timing=(struct rc5_state){.bound=true,.root_boot=2,.root_epoch=3,.parent_boot=2,
  .parent_epoch=4,.parent_train=1,.context_until=UINT64_MAX};
 current_origin=(struct rc5_origin){.root=1,.boot=2,.epoch=3,.issued=900000000,.expires=2550000000};
 assert(!rc7_origin_sign(current_proof,&current_origin,&group));
 assert(make_publication_path());
 struct rc5_admission admission={.parent_boot=6,.root_boot=2,.root_epoch=3,.parent_train=1,
  .parent_epoch=7,.now_tick=fake_tick,.root_lo_ns=999900000,.root_hi_ns=1000100000,.root_expires_ns=current_origin.expires};
 struct rc7_receiver child={0};assert(rc7_bind(&child,&publication_path,8,false,255,&admission));
 const uint64_t until=child.clock.context_until;
 // Actual production PA publication, codec and child matcher form one test.
 // Save original and correct it with a subsequent publication.
 sent_count=0;publish();struct rc7_packet p=decode();assert(p.publication==2&&!p.count);
 assert(!rc7_receive(&child,&p,fake_tick,10,2,fake_tick+1));
 sent[0]=(struct rc5_correction){.publication=2,.event=10,.channel=2,.generation=1,
  .tx_ns=1000000000,.bound=RC5_UNKNOWN};sent_count=1;
 fake_tick+=1000000;publish();p=decode();assert(p.count==1);
 assert(rc7_receive(&child,&p,fake_tick,11,2,fake_tick+1)&&child.clock.accepted==1);
 assert(child.clock.reference.bound==RC5_UNKNOWN);
 // PA may remain present during these independent refusals. Even a stale
 // populated history cannot publish a correction or extend child's lease.
 parent_synced=false;count0(&child,12,until);parent_synced=true;
 fit.valid=false;count0(&child,13,until);fit.valid=true;
 timing.context_until=fake_tick;count0(&child,14,until);timing.context_until=UINT64_MAX;
 timing.bound=false;count0(&child,15,until);timing.bound=true;
 holdover_expired=true;count0(&child,16,until);holdover_expired=false;
 local_clock.map[0].continuous=false;count0(&child,17,until);local_clock.map[0].continuous=true;
 fake_tick=discipline_at+RC_FIELD_HOLD_TICKS;count0(&child,18,until);
 map_at(fake_tick,current_origin.expires);count0(&child,19,until);
 // A fresh valid map does not reuse history in the actual owner: loss clears
 // sent_count and usable_tx_from. Only newly captured TX can correct again.
 map_at(fake_tick,1500000000);sent_count=0;publish();p=decode();assert(!p.count);
 assert(!rc7_receive(&child,&p,fake_tick,20,2,fake_tick+1));
 uint32_t original=p.publication;fake_tick+=1000000;
 sent_count=1;sent[0]=(struct rc5_correction){.publication=original,.event=20,.channel=2,
  .generation=2,.tx_ns=1500000000,.bound=RC5_UNKNOWN};publish();p=decode();
 assert(p.count==1&&rc7_receive(&child,&p,fake_tick,21,2,fake_tick+1)&&child.clock.accepted==2);
 assert(child.clock.context_until==until&&child.clock.reference.bound==RC5_UNKNOWN);
 // Neither child context expiry nor an old path becomes valid from transport.
 assert(!rc7_receive(&child,&p,until,22,2,until)&&child.clock.accepted==2);
 struct rc7_path other=publication_path;++other.node[1].train;
 assert(rc7_decode(&p,wire,sizeof wire,&other,&group));
 puts("PASS actual PA publisher + RC7 codec/receiver: canonical count0, preserved provenance/train, precision/source/auth/hold refusal, recovery and original child deadline; no hardware");
}
