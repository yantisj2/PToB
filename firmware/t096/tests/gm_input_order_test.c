/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual G5 owner and extracted CONTROL/PT reader bodies. Fake clock/capture
 * and mailbox ABI only; no radio, scheduling-duration or power-loss proof. */
#define main previous_owner_main
#include "gm_standby_owner_test.c"
#undef main
#include "gm_input_order_client.inc"
#include "radio_precision.h"
static struct r2_network net;
static bool relay_selected;
static uint64_t fake_now;
static uint64_t now_us(void){return fake_now;}
static uint64_t service_now(void){return fake_now;}
static int pt_lock;static unsigned mailbox_reads;
#define K_FOREVER 0
static int k_mutex_lock(int *p,int wait){(void)p;(void)wait;++mailbox_reads;return 0;}
static int k_mutex_unlock(int *p){(void)p;return 0;}
static struct r2_capture fake_capture;
static bool capture_ok=true;
static bool r2_capture_now(struct r2_capture *c){*c=fake_capture;return capture_ok;}
/* Only accessed mailbox fields are represented; real reader bodies below. */
static struct {bool stopped,source_ready;uint32_t source_epoch;} pt_input;
static struct pt_output {
 struct r2_radio_scope scope;struct r2_radio_precision_view view;
 struct r2_group_client context,bootstrap;
 uint64_t utc,sampled,valid_until,source_frame,capture_epoch,capture_tick,capture_until;
 bool acquisition,utc_valid;
} pt_output;
#include "gm_input_order_reader.inc"
#include "gm_input_order_control.inc"
static void fixture(void)
{
 net=base(false);net.now=71000000;net.timing_at=net.now-250001;
 fake_now=net.now;relay_selected=false;capture_ok=true;
 fake_capture=(struct r2_capture){.epoch=1,.tick=200};pt_input.stopped=false;
 pt_output=(struct pt_output){.scope={.boot=20,.source_epoch=1,.publication_epoch=1,.root=1,.until_us=fake_now+1000000,.follower=true},
  .sampled=fake_now-1000,.valid_until=fake_now+1000000,.capture_epoch=1,.capture_tick=100,.capture_until=1000,
  .view={.authorized=true,.map_valid=true,.locked=true,.train=1},
  .context={.source=true,.root=200,.boot=20,.source_epoch=1,.publication=1,.at=fake_now-1000,.until=fake_now+1000000,.map_until=fake_now+1000000}};
}
int main(void)
{
 for(unsigned hot=0;hot<2;hot++){
  fixture();if(hot)net.gm5.role=R2_GM5_HOT;
  assert(!synced(&net));assert(refresh_direct_gm5_timing());
  assert(synced(&net)&&net.timing_at==fake_now-1000);
 }
 for(unsigned bad=0;bad<14;bad++){
  fixture();
  switch(bad){
   case 0:pt_output.valid_until=fake_now;break;
   case 1:pt_output.view.map_valid=false;break;
   case 2:pt_output.context.boot++;break;
   case 3:pt_output.context.source_epoch++;break;
   case 4:pt_output.view.train++;break;
   case 5:pt_output.context.publication++;break;
   case 6:pt_output.context.root++;break;
   case 7:pt_output.scope.until_us=fake_now;break;
   case 8:fake_capture.epoch++;break;
   case 9:pt_output.context.at=pt_output.sampled=fake_now-250001;break;
   case 10:pt_input.stopped=true;break;
   case 11:pt_output.view.authorized=false;break;
   case 12:pt_output.context.source=false;break;
   case 13:fake_capture.tick=pt_output.capture_until;break;
  }
  assert(refresh_direct_gm5_timing());assert(!synced(&net));
 }
 /* Unselected roles never read the mailbox or mutate network timing. The
  * returned false keeps their original post-tick update in cycle(). */
 for(unsigned path=0;path<4;path++){
  fixture();if(path==0)net.gm5.role=R2_GM5_ACTIVE;
  if(path==1)net.gm5.role=R2_GM5_COLD;
  if(path==2)net.gm5.enabled=false;
  if(path==3)relay_selected=true;
  struct r2_network before=net;unsigned reads=mailbox_reads;assert(!refresh_direct_gm5_timing());
  assert(mailbox_reads==reads);
  assert(!memcmp(&before,&net,sizeof(net)));
 }
 puts("PASS HOT/ACQUIRING fresh PT over stale CONTROL; original timestamp/expiry and tuple negatives; ACTIVE/COLD/RC/relay untouched");
 return 0;
}
