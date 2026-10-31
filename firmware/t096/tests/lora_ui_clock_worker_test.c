/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual UI worker/rendering with deterministic publisher preemption. */
#define k_spin_lock base_spin_lock
#define r2_display_frame base_display_frame
#include "ui_service_fake.h"
#undef r2_display_frame
#undef k_spin_lock
static bool preempt,inside;
static unsigned fraction,preemptions,drawn;
static k_spinlock_key_t k_spin_lock(struct k_spinlock *l){
 if(preempt&&!inside){
  inside=true;fake_now+=2;++preemptions;
  struct r2_ui_model m={.sampled_ms=fake_now,.unit="TEST",.platform_ready=true,.profile_supported=true,
   .lora={.requested=true,.active=true,.state=RC8_AVAILABLE,.choice=RC8_SELECT_LORA,
    .sampled_us=fake_now*1000+fraction,.utc_us=36642000000ULL+fake_now*1000+fraction,.until_us=12000000}};
  r2_ui_publish(&m);inside=false;
 }
 return base_spin_lock(l);
}
int r2_display_frame(const uint8_t *fb){
 unsigned dial=0;for(unsigned y=55;y<112;y++)for(unsigned x=5;x<75;x++)dial+=fb[y*80+x]==R2_UI_COLOR_LCD;
 assert(dial>1000);++drawn;
 return base_display_frame(fb);
}
#include "../src/ui_service.c"
#include <stdlib.h>
#include <stdio.h>
int main(int argc,char **argv){
 assert(argc==2);fraction=(unsigned)atoi(argv[1]);assert(fraction<1000);mode=17;
 r2_ui_start();assert(entry);preempt=true;entry(NULL,NULL,NULL);preempt=false;
 assert(preemptions>drawn&&drawn>=10&&status.analog&&status.drawn_time&&status.drawn_coarse&&!status.drawn_ble&&!status.drawn_map);
 assert(!status.error&&!status.menu.open&&!status.poweroff);
 puts("PASS actual worker/render: publisher preemption before model copy and fractional LoRa samples retain every completed dial");
}
