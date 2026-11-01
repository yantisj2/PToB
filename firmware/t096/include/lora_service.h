/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "lora_rc8_policy.h"
#include "../protocol/gm_coordination.h"
#include <stddef.h>
/* This is deliberately independent of normal source/precision/USB owners. */
struct r2_lora_view {
 uint64_t tz_until_us;
 struct r2_tz tz;
 /* Occupy existing alignment space; display estimates add no mutable RAM.
  * Conservative remaining lease in8s units, projected from original expiry. */
 /* Positive excess delay when rtt_positive; otherwise the learned floor. */
 uint32_t rtt_value_ns;
 uint64_t utc_us,until_us,sampled_us,display_until_us;
 uint32_t age_ms,link_rms_ns,rx_age_ms,activity_age_ms,fit_age_ms;
 int16_t rssi_half_dbm;int8_t snr_quarter_db;uint8_t activity_kind;
 uint16_t root_tag,emitter_tag,parent_tag;
 uint8_t state,assurance,choice,link_samples,kind,parent_class,parent_accuracy;
 uint8_t requested:1,active:1,transmitter:1,fault:1,quality_known:1,time_valid:1,parent_quality:1,coord_follow:1;
 uint8_t display_held:1,rssi_valid:1,rtt_base_count:2,rtt_positive:1;
 uint8_t rtt_life_8s;
};
_Static_assert(sizeof(struct r2_lora_view)<=88,"RC8 view with coarse TZ");
/* UI-only maximum from the original accepted physical pair; no authority. */
#ifdef R2_LORA_PASSIVE_SLOW
#define R2_LORA_DISPLAY_HOLD_US UINT64_C(62000000)
#define R2_LORA_FIT_HOLD_MS 62000u
#define R2_LORA_SIGNAL_HOLD_MS 62000u
#else
#define R2_LORA_DISPLAY_HOLD_US UINT64_C(20000000)
#define R2_LORA_FIT_HOLD_MS 24000u
#define R2_LORA_SIGNAL_HOLD_MS 12000u
#endif
/* Minimum deadband for relative propagation estimates; not an accuracy bound. */
#define R2_LORA_TOF_FLOOR_NS UINT32_C(1000)
void r2_lora_name_publish(const char *);
void r2_lora_tz_publish(struct r2_tz,uint64_t until_us,uint64_t root,uint64_t root_boot,uint32_t epoch);
void r2_lora_view_named(struct r2_lora_view *,uint64_t *factory,char name[25]);
void r2_lora_init(uint64_t factory,uint64_t boot,uint64_t transmitter,const struct r2_group_config *);
void r2_lora_control(bool profile_rc,bool allowed,enum rc8_selected normal_source);
/* Computed by serialized BLE owner from original authenticated contact leases. */
void r2_lora_mesh_contact(bool present);
void r2_lora_request(bool on);
/* Temporary five-minute lab cadence; never changes eligibility or authority. */
void r2_lora_rtt_lab(bool on);
/* Main-owner bridge into the existing causal PPS observer; no witness feedback. */
void r2_lora_shadow_publish(void);
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
#ifdef R2_LORA_PASSIVE_SLOW
void r2_lora_gm_admission(bool serving,uint64_t original_until_us,bool explicit_primary_handoff,uint32_t source_epoch,uint32_t capture_epoch);
bool r2_lora_transmitter_quiescent(void);
#endif
#if defined(R2_LORA_PASSIVE_FAST)||defined(R2_LORA_PASSIVE_SLOW)
int r2_lora_diagnostics_json(char *,size_t,int result);
#endif
int r2_lora_json(char *,size_t,int result);
