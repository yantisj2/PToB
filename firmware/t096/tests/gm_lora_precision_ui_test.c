/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#define main historical_watch_ui_main
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "lora_watch_ui_test.c"
#pragma clang diagnostic pop
#undef main
int main(int argc,char **argv){
 assert(argc==2);struct r2_ui_model m=example();struct r2_ui_status s={.page=2};
 m.profile_rc=false;m.gm7_owner=true;m.ble_rx_requested=true;
 m.lora.choice=RC8_SELECT_LORA;m.lora.state=RC8_AVAILABLE;
 m.lora.time_valid=m.lora.quality_known=true;m.lora.until_us=2000000;m.lora.display_acquiring=false;
 r2_ui_render(fb,&m,&s,1000);text_at(13,4,"PRECISION",C_WHITE);save(argv[1],"GM-native-precision-with-live-LORA");
 m.lora.choice=RC8_SELECT_NONE;m.lora.state=RC8_EXPIRED;m.lora.display_held=true;
 m.lora.time_valid=m.lora.quality_known=false;m.lora.rx_age_ms=90000;
 r2_ui_render(fb,&m,&s,1000);text_at(13,4,"PRECISION",C_WHITE);text_at(4,33,"UNAVAILABLE",C_AMBER);save(argv[1],"GM-native-precision-with-LORA-HOLD");
 memcpy(digital,fb,sizeof digital);struct r2_ui_model empty=m;empty.lora=(struct r2_lora_view){0};
 r2_ui_render(fb,&empty,&s,1000);assert(!memcmp(fb,digital,sizeof fb));
 m.profile_rc=true;r2_ui_render(fb,&m,&s,1000);text_at(10,4,"LOR TIMING",C_WHITE);save(argv[1],"RC-LORA-HOLD-precision-unchanged");
 puts("PASS actual native GM precision remains BLE/GM with live or held LoRa; RC LoRa precision unchanged");
 return 0;
}
