/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "lora_rc8_policy.h"
#include <stddef.h>
/* This is deliberately independent of normal source/precision/USB owners. */
struct r2_lora_view {uint64_t utc_us,until_us,sampled_us;uint32_t age_ms;uint16_t root_tag,emitter_tag;uint8_t state,assurance,choice,requested,active,transmitter,fault;};
void r2_lora_init(uint64_t factory,uint64_t boot,uint64_t transmitter,const struct r2_group_config *);
void r2_lora_control(bool profile_rc,bool allowed,enum rc8_selected normal_source);
void r2_lora_request(bool on);
void r2_lora_toggle(void);
void r2_lora_stop(void);
int r2_lora_close_result(void);
bool r2_lora_source_needed(void);
void r2_lora_source_publish(const struct rc8_export *);
void r2_lora_source_withdraw(void);
void r2_lora_view(struct r2_lora_view *);
int r2_lora_json(char *,size_t,int result);
