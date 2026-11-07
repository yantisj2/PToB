/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "../protocol/lora_rc8.h"
#define LP_BLE_RETURN_US UINT64_C(30000000)
/* Local BLE observation, never a LoRa wire context or timing authority. */
struct lp_ble_scope {
 uint64_t parent,parent_boot,root,root_boot,capture_epoch;
 uint32_t source_epoch,train,parent_capture_epoch;
};
static inline bool lp_ble_scope_equal(const struct lp_ble_scope *a,const struct lp_ble_scope *b){
 return a->parent==b->parent&&a->parent_boot==b->parent_boot&&a->root==b->root&&
  a->root_boot==b->root_boot&&a->source_epoch==b->source_epoch&&a->train==b->train&&
  a->capture_epoch==b->capture_epoch&&a->parent_capture_epoch==b->parent_capture_epoch;
}
struct lp_ble_return {
 struct lp_ble_scope scope;
 uint64_t since_us,until_us;
 uint32_t refused;
 bool qualified;
};
/* This observes the already authenticated/qualified BLE clock. It grants no
 * time, extends no original lease and never consumes a LoRa/PPS sample. */
static inline void lp_ble_observe(struct lp_ble_return *s,const struct lp_ble_scope *scope,
 uint64_t until,uint32_t refused,uint64_t now){
 if(!scope||!scope->parent||!scope->parent_boot||!scope->root||!scope->root_boot||
    !scope->source_epoch||!scope->train||!scope->capture_epoch||!scope->parent_capture_epoch||until<=now){
  s->qualified=false;s->since_us=s->until_us=0;return;
 }
 if(!s->qualified||now<s->since_us||now>=s->until_us||
    refused!=s->refused||!lp_ble_scope_equal(&s->scope,scope))s->since_us=now;
 s->scope=*scope;s->refused=refused;s->until_us=until;s->qualified=true;
}
static inline bool lp_ble_stable(const struct lp_ble_return *s,uint64_t now){
 return s->qualified&&now>=s->since_us&&now<s->until_us&&now-s->since_us>=LP_BLE_RETURN_US;
}
