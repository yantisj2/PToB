/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <stdio.h>
#include "ui_service_fake.h"
#include "../src/ui_service.c"
int main(int argc,char **argv){
 assert(argc==2);mode=atoi(argv[1]);assert(mode>=42&&mode<=46);
 struct r2_ui_model m={.unit="TEST",.platform_ready=true,.profile_supported=true};r2_ui_publish(&m);assert(!r2_ui_page(R2_UI_LORA_PAGE));
 r2_ui_start();assert(entry);entry(NULL,NULL,NULL);
 if(mode==42){assert(r2_ui_take_lora_toggle());assert(!r2_ui_take_lora_toggle());assert(!status.poweroff&&!status.help&&status.page==R2_UI_LORA_PAGE);}
 else{assert(!r2_ui_take_lora_toggle());if(mode==43)assert(status.poweroff);if(mode==45)assert(!status.poweroff);}
 assert(!status.profile_toggles);puts("PASS actual RC8 page worker: release toggle /4s off /page cancel /wake-only /profile cancel");
}
