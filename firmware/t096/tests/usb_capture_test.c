/* SPDX-License-Identifier: Apache-2.0 -- actual capture owner, fake drivers. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/capture.c"
int main(int argc,char **argv){
 assert(argc==2);failure=(unsigned)strtoul(argv[1],NULL,10);
 struct gpio_dt_spec p={&gpio1,11};struct r2_usb_sof_snapshot sample={0};
 assert(!r2_capture_usb_sample(&sample));
 fake_now=1000;assert(!r2_capture_start(&p,fake_now));raw_now=UINT32_C(0xfffff000);fake_now=1100;r2_capture_poll(fake_now);
 if(failure>=15&&failure<=17){
  assert(r2_capture_error()&&!accepting&&!usb_owned&&!requested&&!timer_owned&&!allocated);
 }else{
  assert(accepting&&usb_owned&&usb_route&&allocated==((1u<<5)|(1u<<6)|(1u<<7)));
  usb_raw=UINT32_C(0xfffffff0);raw_now=32;fake_now=1400;
  assert(r2_capture_usb_sample(&sample)&&sample.current_tick==UINT64_C(0x100000020));
  assert(sample.cc_before==UINT32_C(0xfffffff0)&&sample.cc_after==sample.cc_before&&sample.frame_before==77&&sample.frame_after==77&&sample.capture_epoch==1);
  struct r2_usb_sof_observer obs; r2_usb_sof_begin(&obs,1,7);sample.usb_epoch=7;
  assert(r2_usb_sof_observe(&obs,&sample)==R2_USB_SOF_ACCEPTED);
  usb_race=true;assert(r2_capture_usb_sample(&sample));sample.usb_epoch=7;
  assert(r2_usb_sof_observe(&obs,&sample)==R2_USB_SOF_RACE);usb_race=false;
  usb_registers.LOWPOWER=1;assert(!r2_capture_usb_sample(&sample));usb_registers.LOWPOWER=0;
  usb_registers.ENABLE=0;assert(!r2_capture_usb_sample(&sample));usb_registers.ENABLE=1;
  int rc=r2_capture_stop();
  if(failure==18||failure==19){assert(rc==-EIO&&timer_owned&&requested&&usb_owned);failure=0;assert(!r2_capture_stop());}
  else assert(!rc);
  assert(!requested&&!usb_owned&&!usb_route&&!allocated&&!timer_owned&&!route_enabled);
 }
 assert(!r2_capture_usb_sample(&sample));
 puts("PASS allocated SOF PPI/CC3, hardware extension/wrap/races/power state, failure cleanup and retained ownership");
}
