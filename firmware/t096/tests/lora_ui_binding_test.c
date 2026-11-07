/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t emitter=UINT64_C(0x1111111100004990);
static const char *received="";
void r2_lora_view_named(struct r2_lora_view *v,uint64_t *id,char name[25]){(void)v;*id=emitter;snprintf(name,25,"%s",received);}
#include "lora_ui_binding_under_test.h"
int main(void){
 struct r2_ui_model m={.ble_rx_supported=true,.lora={.requested=true}};
 lora_ui_view(&m);assert(!strcmp(m.upstream_name,"TC-03"));
 emitter=UINT64_C(0x2222222200004990);lora_ui_view(&m);assert(!strcmp(m.upstream_name,"TC-09"));
 emitter=UINT64_C(0x3333333300004990);lora_ui_view(&m);assert(!strcmp(m.upstream_name,"4990"));
 emitter=0;lora_ui_view(&m);assert(!strcmp(m.upstream_name,"--"));
 /* A held observer cannot overwrite the BLE source name during acquisition. */
 emitter=UINT64_C(0x1111111100004990);m=(struct r2_ui_model){.platform_ready=true,.sampled_ms=1000,
  .ble_rx_supported=true,.ble_rx_requested=true,.ble_active=true,.sync_attempt=true,.upstream_name="TC-04",
  .lora={.requested=true,.active=true,.display_held=true,.sampled_us=1000000,.utc_us=1000000,.display_until_us=UINT64_MAX}};
 lora_ui_view(&m);assert(!strcmp(m.upstream_name,"TC-04"));
 puts("PASS actual main LoRa UI binding: full-factory registry aliases precede hex fallback, including equal low16 identities");
}
