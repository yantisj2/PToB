/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_sof_observer.h"
#include <string.h>
#include <limits.h>
static void add(uint32_t *v,uint32_t n){*v=UINT32_MAX-*v<n?UINT32_MAX:*v+n;}
void r2_usb_sof_begin(struct r2_usb_sof_observer *s,uint32_t capture,uint32_t usb){
 memset(s,0,sizeof(*s));s->capture_epoch=capture;s->usb_epoch=usb;s->active=capture&&usb;
}
void r2_usb_sof_end(struct r2_usb_sof_observer *s){s->active=false;s->seen=false;s->count=s->head=s->tail=0;}
enum r2_usb_sof_result r2_usb_sof_observe(struct r2_usb_sof_observer *s,const struct r2_usb_sof_snapshot *r){
 if(!s->active)return R2_USB_SOF_INACTIVE;
 if(r->capture_epoch!=s->capture_epoch||r->usb_epoch!=s->usb_epoch){
  r2_usb_sof_end(s);return R2_USB_SOF_DISCONTINUITY;
 }
 /* A changing frame, overwritten capture, out-of-range frame or a CC value
  * older than one frame cannot be made consistent by a later software read. */
 uint32_t age=(uint32_t)r->current_tick-r->cc_after;
 if(r->frame_before!=r->frame_after||r->cc_before!=r->cc_after||
    r->frame_after>R2_USB_FRAME_MASK||age>=R2_USB_SOF_TICKS||r->current_tick<age){
  add(&s->races,1);return R2_USB_SOF_RACE;
 }
 uint64_t tick=r->current_tick-age;
 unsigned frames=1;
 if(s->seen){
  if(tick==s->last_tick&&r->frame_after==s->last_frame){add(&s->duplicates,1);return R2_USB_SOF_DUPLICATE;}
  frames=(r->frame_after-s->last_frame)&R2_USB_FRAME_MASK;
  /* At most one second between observations: never infer an arbitrary number
   * of 2048-frame wraps after a stall. The half-frame consistency allowance
   * detects gross counter/CC disagreement; it is NOT a timing error bound. */
  uint64_t expected=(uint64_t)frames*R2_USB_SOF_TICKS;
  uint64_t dt=tick-s->last_tick;
  uint64_t error=dt>expected?dt-expected:expected-dt;
  if(tick<=s->last_tick||!frames||frames>1000||dt>UINT64_C(16000000)||error>=R2_USB_SOF_TICKS/2){
   add(&s->loss_total,1);r2_usb_sof_end(s);return R2_USB_SOF_DISCONTINUITY;
  }
  add(&s->missed,frames-1);add(&s->loss_total,frames-1);
 }
 if(s->sequence==UINT32_MAX||s->ordinal>UINT64_MAX-frames){
  r2_usb_sof_end(s);return R2_USB_SOF_DISCONTINUITY;
 }
 s->seen=true;s->last_tick=tick;s->last_frame=r->frame_after;s->ordinal+=frames;++s->sequence;
 if(s->count==R2_USB_SOF_QUEUE){add(&s->overflow,1);add(&s->loss_total,1);return R2_USB_SOF_FULL;}
 s->queue[s->head]=(struct r2_usb_sof_record){.tick=tick,.ordinal=s->ordinal,
  .capture_epoch=s->capture_epoch,.usb_epoch=s->usb_epoch,.sequence=s->sequence,
  .loss_total=s->loss_total,.frame=r->frame_after};
 s->head=(s->head+1)%R2_USB_SOF_QUEUE;++s->count;
 return R2_USB_SOF_ACCEPTED;
}
bool r2_usb_sof_pop(struct r2_usb_sof_observer *s,struct r2_usb_sof_record *r){
 if(!s->active||!s->count)return false;
 *r=s->queue[s->tail];s->tail=(s->tail+1)%R2_USB_SOF_QUEUE;--s->count;return true;
}
