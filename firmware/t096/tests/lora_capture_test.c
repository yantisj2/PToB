/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Production shared capture owner with existing USB/PPS/BLE fakes. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/capture.c"
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);
 struct gpio_dt_spec pps={&gpio1,11},dio={&gpio0,21};struct r2_capture event,current;
 assert(!r2_capture_lora(&event));fake_now=1000;assert(!r2_capture_start(&pps,fake_now));
 raw_now=UINT32_C(0xfffff000);fake_now=1100;r2_capture_poll(fake_now);
 assert(accepting&&usb_route&&radio_open&&allocated==((1u<<5)|(1u<<6)|(1u<<7)));
 if(mode>=2&&mode<=5){
  failure=mode==2?30:mode==3?31:mode==4?32:33;
  assert(r2_capture_lora_attach(&dio,9876)<0&&!lora_owned&&!lora_fork&&!lora_route);
  assert(accepting&&usb_route&&radio_open&&r2_capture_now(&current)&&!r2_capture_error());
 }else{
  if(mode==8){
   struct gpio_dt_spec wrong={&gpio1,21};
   assert(r2_capture_lora_attach(&wrong,9876)==-EINVAL);
   assert(r2_capture_lora_attach(&dio,9877)==-EINVAL);
  }
  assert(!r2_capture_lora_attach(&dio,9876)&&lora_owned&&lora_route&&lora_fork==9876);
  unsigned mask=allocated;assert(r2_capture_lora_attach(&dio,9876)==-EALREADY&&allocated==mask);
  raw_lora=UINT32_C(0xfffffff0);raw_now=32;fake_now=1400;
  assert(r2_capture_lora(&event)&&event.tick==UINT64_C(0xfffffff0)&&event.kind==R2_CAPTURE_LORA&&event.epoch==1);
  if(mode==9){
   raw_now+=8u*R2_CAPTURE_HZ+1;fake_now+=1000;
   assert(!r2_capture_lora(&event)&&r2_capture_now(&current)&&!r2_capture_error());
   raw_pps=raw_now;assert(r2_capture_pps(&event,fake_now));
  }
  if(mode==10){
   raw_now+=16000;fake_now+=1000;assert(r2_capture_lora(&event)&&event.tick==UINT64_C(0xfffffff0));
  }
  if(mode==11){
   /* BLE/common capture continues while only the LoRa callback is delayed;
    * the original shared-clock one-second continuity guard is unchanged. */
   for(unsigned i=0;i<12;i++){
    raw_now+=R2_CAPTURE_HZ/2;fake_now+=500000;r2_capture_poll(fake_now);
   }
   assert(r2_capture_lora(&event)&&event.tick==UINT64_C(0xfffffff0)&&!r2_capture_error());
  }
  /* Common timer/HFXO teardown must refuse while LoRa owns its route. */
  assert(r2_capture_stop()==-EBUSY&&accepting&&requested&&usb_route&&radio_open);
  if(mode==6||mode==7){
   failure=mode==6?34:35;assert(r2_capture_lora_detach()==-EIO&&lora_owned);
   assert(r2_capture_stop()==-EBUSY&&accepting&&usb_route&&radio_open);
   failure=0;
  }
  assert(!r2_capture_lora_detach()&&!lora_owned&&!lora_fork&&!lora_route);
 }
 failure=0;assert(allocated==((1u<<5)|(1u<<6)|(1u<<7))&&usb_route&&radio_open);
 assert(!r2_capture_lora(&event)&&!r2_capture_error());
 usb_raw=raw_now;struct r2_usb_sof_snapshot sample;assert(r2_capture_usb_sample(&sample)&&sample.cc_before==usb_raw);
 assert(!r2_capture_stop()&&!allocated&&!requested&&!usb_route&&!radio_open);
 puts("PASS CC4 LoRa/CC3 USB/PPS/BLE ownership and fork retirement");
}
