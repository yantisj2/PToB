/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <stdio.h>
#include "ui_service_fake.h"
#include "../src/ui_service.c"
int main(int argc,char **argv){
 assert(argc==2);mode=atoi(argv[1]);assert(mode>=42&&mode<=51);
 struct r2_ui_model m={.unit="TEST",.platform_ready=true,.profile_supported=true};r2_ui_publish(&m);assert(!r2_ui_page(R2_UI_LORA_PAGE));
 if(mode==47)assert(!r2_ui_page(3));
 r2_ui_start();assert(entry);entry(NULL,NULL,NULL);
 if(mode==42||mode==43){assert(r2_ui_take_lora_toggle());assert(!r2_ui_take_lora_toggle());assert(!status.poweroff&&!status.help&&status.page==R2_UI_LORA_PAGE);}
 else{
  assert(!r2_ui_take_lora_toggle());
  if(mode==47)assert(status.poweroff);else assert(!status.poweroff);
  if(mode==48)assert(lora_consumptions==1&&!fake_lora_requested&&lora_consumed_at>=2000&&lora_consumed_at<=2200&&status.drawn_page==R2_UI_LORA_PAGE);
  if(mode==49)assert(lora_consumptions==2&&fake_lora_requested&&status.drawn_page==R2_UI_LORA_PAGE);
 }
 assert(!status.profile_toggles);puts("PASS actual RC8 page worker: one-shot1.5s /extendedholdsafe /4s off otherpage /page cancel /wake-only /profile cancel /two toggles");
}
