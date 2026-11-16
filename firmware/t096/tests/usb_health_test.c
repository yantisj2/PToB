/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define R2_USB_SOURCE_STREAM 1
#include <stdint.h>
#include <string.h>
static uint32_t usb_sample[4];
void r2_usb_health_snapshot(uint32_t out[4]){memcpy(out,usb_sample,sizeof usb_sample);}
void r2_usb_debug_snapshot(uint32_t out[8]){memset(out,0,32);memcpy(out,usb_sample,sizeof usb_sample);}
#define main historical_watchdog_main
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "watchdog_test.c"
#pragma clang diagnostic pop
#undef main
int main(void)
{
 struct r2_usb_health h={0};uint32_t s[4]={55,7,11,19};
 assert(!r2_usb_health_stalled(&h,s,100));
 assert(!r2_usb_health_stalled(&h,s,30099));
 /* New setup arrivals do not hide a worker that completes no work. */
 ++s[2];assert(r2_usb_health_stalled(&h,s,30100));
 ++s[1];assert(!r2_usb_health_stalled(&h,s,30101));
 for(unsigned flags=0;flags<1024;flags++)if((flags&5)!=5||!(flags&992)){
  s[0]=flags;assert(!r2_usb_health_stalled(&h,s,100000));
  assert(!r2_usb_health_stalled(&h,s,200000));
 }
 for(unsigned bit=32;bit<=512;bit*=2){
  h=(struct r2_usb_health){0};s[0]=5|bit;
  assert(!r2_usb_health_stalled(&h,s,0));assert(r2_usb_health_stalled(&h,s,30000));
 }
 /* Suspended-but-pending control and held DMA are failures, not idle sleep. */
 for(unsigned bit=32;bit<=512;bit*=2){
  h=(struct r2_usb_health){0};s[0]=13|bit;
  assert(!r2_usb_health_stalled(&h,s,0));assert(r2_usb_health_stalled(&h,s,30000));
 }
 /* Switching from a worker wait to a newly acquired DMA starts its own budget. */
 h=(struct r2_usb_health){0};s[0]=37;assert(!r2_usb_health_stalled(&h,s,0));
 s[0]=261;assert(!r2_usb_health_stalled(&h,s,29999));
 assert(!r2_usb_health_stalled(&h,s,30000));assert(r2_usb_health_stalled(&h,s,59999));
 h=(struct r2_usb_health){0};s[0]=55;assert(!r2_usb_health_stalled(&h,s,UINT32_MAX-10000));
 assert(!r2_usb_health_stalled(&h,s,19998));
 assert(r2_usb_health_stalled(&h,s,19999));
 /* Actual watchdog first-cause, CRC and field mapping. */
 clear_runtime();assert(!r2_watchdog_boot(&id));usb_health=(struct r2_usb_health){0};
 memcpy(usb_sample,s,sizeof s);fake_ms=100;main_at=fake_ms;
 assert(watchdog_step());fake_ms+=30000;main_at=fake_ms;
 assert(!watchdog_step());assert(reset_reason==WD_USB_FAULT);
 assert(watchdog_bound(&id)&&r2_watchdog_capsule[8]==(uint32_t)-ETIMEDOUT);
 assert(r2_watchdog_capsule[9]==s[0]&&r2_watchdog_capsule[10]==s[1]&&r2_watchdog_capsule[11]==s[2]&&r2_watchdog_capsule[32]==s[3]);
 r2_watchdog_lora(-EIO,0,0,0,0);assert(r2_watchdog_capsule[4]==WD_USB_FAULT);
 usb_sample[0]=0;assert(!watchdog_step()); /* terminal decision cannot resume feeding */
 /* Actual watchdog capture also covers an awake idle PHY with no queued
  * control work or DMA. Physical frame progress is independent of software. */
 clear_runtime();assert(!r2_watchdog_boot(&id));usb_health=(struct r2_usb_health){0};
 usb_sample[0]=0x3c17u;usb_sample[2]=73;usb_sample[3]=912;
 for(uint32_t elapsed=0;elapsed<=30000;elapsed+=125){
  fake_ms=100+elapsed;main_at=fake_ms;++usb_sample[1];
  assert(watchdog_step()==(elapsed<30000));
 }
 assert(reset_reason==WD_USB_FAULT&&watchdog_bound(&id));
 assert(r2_watchdog_capsule[9]==usb_sample[0]&&r2_watchdog_capsule[11]==73&&r2_watchdog_capsule[32]==912);
 /* Real lifecycle timeouts remain terminal after VBUS removal/explicit
  * detach. A late completion or unrelated progress cannot erase the cause. */
 for(unsigned reason=1;reason<=4;reason++){
  clear_runtime();assert(!r2_watchdog_boot(&id));usb_health=(struct r2_usb_health){0};
  usb_sample[0]=reason<<19;usb_sample[1]=41;usb_sample[2]=19;usb_sample[3]=0;
  fake_ms=100;main_at=fake_ms;assert(!watchdog_step());
  assert(reset_reason==WD_USB_FAULT&&watchdog_bound(&id));
  assert(r2_watchdog_capsule[9]==(reason<<19)&&r2_watchdog_capsule[10]==41&&r2_watchdog_capsule[11]==19);
  usb_sample[0]=0;++usb_sample[1];assert(!watchdog_step());
 }
 puts("PASS USB idle/suspend/unplug/startup, progress/arrival, wrap, threshold and actual retained watchdog first cause");
}
