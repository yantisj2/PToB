/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "../protocol/lora_rc8.h"
#define LP_BLE_RETURN_US UINT64_C(30000000)
struct lp_ble_return {
 struct rc8_scope scope;
 uint64_t since_us,until_us;
 uint32_t refused;
 bool qualified;
};
/* This observes already authenticated/qualified BLE exports. It grants no
 * time, extends no original lease and never consumes a LoRa/PPS sample. */
static inline void lp_ble_observe(struct lp_ble_return *s,const struct rc8_scope *scope,
 uint64_t until,uint32_t refused,uint64_t now){
 if(!scope||scope->kind!=RC8_SELECTED_BLE||until<=now){
  s->qualified=false;s->since_us=s->until_us=0;return;
 }
 if(!s->qualified||now<s->since_us||now>=s->until_us||
    refused!=s->refused||!rc8_scope_equal(&s->scope,scope))s->since_us=now;
 s->scope=*scope;s->refused=refused;s->until_us=until;s->qualified=true;
}
static inline bool lp_ble_stable(const struct lp_ble_return *s,uint64_t now){
 return s->qualified&&now>=s->since_us&&now<s->until_us&&now-s->since_us>=LP_BLE_RETURN_US;
}
