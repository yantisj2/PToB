/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "ui.h"
#include "capture.h"
#include "rf_clock.h"
#include <assert.h>
#include <stdio.h>
static bool visible=true,capture_ok=true;
static unsigned captures,ble_reads,lora_reads;
static const struct r2_capture common={.kind=R2_CAPTURE_NOW,.tick=UINT64_C(0x123456789),.epoch=7};
static const uint64_t common_us=1000000;
static struct r2_rf_clock_sample ble,lora;
bool r2_ui_rf_requested(void){return visible;}
bool r2_capture_now_at(struct r2_capture *out,uint64_t *now){++captures;*out=common;*now=common_us;return capture_ok;}
void r2_service_rf_clock(struct r2_rf_clock_sample *out,struct r2_capture at,uint64_t now){assert(at.tick==common.tick&&at.epoch==common.epoch&&now==common_us);++ble_reads;*out=ble;}
void r2_lora_rf_clock(struct r2_rf_clock_sample *out,struct r2_capture at,uint64_t now){assert(at.tick==common.tick&&at.epoch==common.epoch&&now==common_us);++lora_reads;*out=lora;}
#include "rf_ui_under_test.h"
int main(void){
 struct r2_ui_model m={0};
 ble=(struct r2_rf_clock_sample){.time_ns=UINT64_C(1790000000000000675),.root=1,.boot=2,.source_epoch=3,.train=4,.rate_ppb=120,.available=true,.fresh=true,.utc=true,.rate_valid=true,.fresh_until_us=2000000};
 lora=ble;lora.time_ns-=675;lora.rate_ppb=162;lora.fresh_until_us=1500000;
 rf_ui_view(&m);assert(captures==1&&ble_reads==1&&lora_reads==1);assert(m.rf_comparison.flags==(R2_UI_RF_DELTA|R2_UI_RF_RATE)&&m.rf_comparison.delta_ns==675&&m.rf_comparison.drift_ppb==-42&&m.rf_comparison.until_us==1500000);
 visible=false;rf_ui_view(&m);assert(captures==1&&!m.rf_comparison.flags);visible=true;
 m.lora.transmitter=true;rf_ui_view(&m);assert(captures==1&&!m.rf_comparison.flags);m.lora.transmitter=false;
 capture_ok=false;rf_ui_view(&m);assert(ble_reads==1&&lora_reads==1&&!m.rf_comparison.flags);capture_ok=true;
 lora.provisional=true;lora.rate_valid=false;rf_ui_view(&m);assert(m.rf_comparison.flags==(R2_UI_RF_DELTA|R2_UI_RF_ESTIMATE));
 lora.rate_valid=true;lora.time_ns=ble.time_ns+712;ble.rate_ppb=INT32_MAX;lora.rate_ppb=INT32_MIN;
 rf_ui_view(&m);assert(m.rf_comparison.delta_ns==-712&&m.rf_comparison.drift_ppb==INT64_C(4294967295));
 lora.fresh=false;rf_ui_view(&m);assert(!m.rf_comparison.flags);lora.fresh=true;
 lora.fresh_until_us=common_us;rf_ui_view(&m);assert(!m.rf_comparison.flags);lora.fresh_until_us=2000000;
 lora.time_ns=UINT64_MAX;rf_ui_view(&m);assert(!m.rf_comparison.flags);lora.time_ns=ble.time_ns;
 lora.root=0;rf_ui_view(&m);assert(!m.rf_comparison.flags);
 puts("PASS actual main RF binding: one common capture, hidden-page idle, ns/sign/PPB width, provisional unknownrate, expiry/HOLD/identity/overflow");
}
