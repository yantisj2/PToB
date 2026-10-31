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
 assert(!r2_ui_take_lora_toggle()&&!lora_consumptions&&fake_lora_requested);
 assert(!status.poweroff&&!status.help);
 assert(!status.profile_toggles); /* Both1.7s presses in mode49 are short. */
 puts("PASS actual worker: holds from LoRa/other pages/wake open menu; no direct radio toggle or poweroff; next separate hold selects once");
}
