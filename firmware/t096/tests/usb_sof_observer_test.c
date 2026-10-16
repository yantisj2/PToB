/* SPDX-License-Identifier: Apache-2.0 -- synthetic register tuples, no USB. */
#include "usb_sof_observer.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>
static struct r2_usb_sof_snapshot at(uint64_t tick,unsigned frame){
 return (struct r2_usb_sof_snapshot){.current_tick=tick+100,.cc_before=(uint32_t)tick,
  .cc_after=(uint32_t)tick,.capture_epoch=1,.usb_epoch=7,.frame_before=frame,.frame_after=frame};
}
int main(void){
 struct r2_usb_sof_observer s={0};struct r2_usb_sof_record out;
 struct r2_usb_sof_snapshot r=at(100000,2047);
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_INACTIVE);
 r2_usb_sof_begin(&s,1,7);
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_ACCEPTED);
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_DUPLICATE);
 assert(s.count==1&&s.duplicates==1&&s.loss_total==0);
 r=at(116000,0);assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_ACCEPTED); /* 11-bit wrap */
 r=at(164000,3);assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_ACCEPTED);
 assert(s.missed==2&&s.loss_total==2&&s.ordinal==5);
 assert(r2_usb_sof_pop(&s,&out)&&out.frame==2047&&out.ordinal==1);
 assert(r2_usb_sof_pop(&s,&out)&&out.frame==0&&out.tick==116000);
 assert(r2_usb_sof_pop(&s,&out)&&out.loss_total==2&&out.sequence==3);
 assert(!r2_usb_sof_pop(&s,&out));
 r=at(180000,4);r.frame_after=5;
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_RACE&&s.races==1);
 r=at(180000,4);r.cc_after++;
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_RACE&&s.races==2);
 r=at(180000,4);r.current_tick+=16000;
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_RACE);
 r=at(180000,4);assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_ACCEPTED);
 assert(r2_usb_sof_pop(&s,&out)&&out.loss_total==2);
 // Queue saturation drops new entries; later sequence/loss counters retain it.
 for(unsigned i=0;i<10;i++){
  r=at(196000+i*16000,5+i);
  assert(r2_usb_sof_observe(&s,&r)==(i<8?R2_USB_SOF_ACCEPTED:R2_USB_SOF_FULL));
 }
 assert(s.count==8&&s.overflow==2&&s.missed==2);
 for(unsigned i=0;i<8;i++)assert(r2_usb_sof_pop(&s,&out));
 r=at(356000,15);assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_ACCEPTED);
 assert(r2_usb_sof_pop(&s,&out)&&out.sequence==15&&out.loss_total==4);
 // Capture extension may cross 32-bit wrap without losing hardware origin.
 r2_usb_sof_begin(&s,1,7);r=at(UINT64_C(0xfffffff0),30);
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_ACCEPTED);
 r=at(UINT64_C(0xfffffff0)+16000,31);
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_ACCEPTED);
 assert(r2_usb_sof_pop(&s,&out)&&out.tick==UINT64_C(0xfffffff0));
 assert(r2_usb_sof_pop(&s,&out)&&out.tick==UINT64_C(0xfffffff0)+16000);
 // A >1s stall or equal raw frame after wrap cannot silently choose an offset.
 r=at(UINT64_C(0xfffffff0)+16000+2048*16000,31);
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_DISCONTINUITY&&!s.active&&!s.count);
 // Changed frame with an unchanged capture, or a grossly mismatched delta.
 for(unsigned mode=0;mode<2;mode++){
  r2_usb_sof_begin(&s,1,7);r=at(500000,10);assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_ACCEPTED);
  r=at(mode?540000:500000,11);
  assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_DISCONTINUITY&&!s.count);
 }
 // USB reset/suspend/reconnect and capture restart are separate fences.
 for(unsigned mode=0;mode<2;mode++){
  r2_usb_sof_begin(&s,1,7);r=at(500000,10);assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_ACCEPTED);
  r=at(516000,11);if(mode)r.capture_epoch++;else r.usb_epoch++;
  assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_DISCONTINUITY&&!s.count);
  assert(!r2_usb_sof_pop(&s,&out));
 }
 r2_usb_sof_begin(&s,0,7);r=at(16000,1);assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_INACTIVE);
 r2_usb_sof_begin(&s,1,7);s.sequence=UINT32_MAX;
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_DISCONTINUITY);
 r2_usb_sof_begin(&s,1,7);s.ordinal=UINT64_MAX;
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_DISCONTINUITY);
 r2_usb_sof_begin(&s,1,7);s.loss_total=UINT32_MAX;r.frame_after++;
 assert(r2_usb_sof_observe(&s,&r)==R2_USB_SOF_RACE&&s.loss_total==UINT32_MAX);
 printf("PASS USB SOF observer: raw wraps/races/missed frames/stalls/queue/epochs/overflow; RAM=%zu\n",sizeof(s));
}
