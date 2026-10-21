/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "lora_rc8_policy.h"
#include "../protocol/gm_coordination.h"
#include <stddef.h>
/* This is deliberately independent of normal source/precision/USB owners. */
struct r2_lora_view {
 uint64_t utc_us,until_us,sampled_us;
 uint32_t age_ms,link_rms_ns;
 uint16_t root_tag,emitter_tag,parent_tag;
 uint8_t state,assurance,choice,link_samples,kind,parent_class,parent_accuracy;
 uint8_t requested:1,active:1,transmitter:1,fault:1,quality_known:1,time_valid:1,parent_quality:1,coord_follow:1;
};
_Static_assert(sizeof(struct r2_lora_view)==48,"RC8 view reserve");
void r2_lora_init(uint64_t factory,uint64_t boot,uint64_t transmitter,const struct r2_group_config *);
void r2_lora_control(bool profile_rc,bool allowed,enum rc8_selected normal_source);
/* Computed by serialized BLE owner from original authenticated contact leases. */
void r2_lora_mesh_contact(bool present);
void r2_lora_request(bool on);
void r2_lora_toggle(void);
void r2_lora_stop(void);
int r2_lora_close_result(void);
bool r2_lora_source_needed(void);
void r2_lora_source_publish(const struct rc8_export *);
void r2_lora_source_withdraw(void);
void r2_lora_quality_publish(const struct rc8_scope *,const struct rc8_quality *);
void r2_lora_parent_quality(uint64_t factory,uint64_t boot,uint8_t clock_class,uint8_t accuracy,uint64_t parent_seen_us);
void r2_lora_view(struct r2_lora_view *);
void r2_lora_coordination(struct r2_gm_coordination *);
void r2_lora_origin(bool actually_serving,uint64_t incarnation,uint32_t epoch);
int r2_lora_json(char *,size_t,int result);
