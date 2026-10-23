/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "radio_precision.h"
#include "link_quality.h"
struct r2_enrollment;
struct r2_relay_precision;
struct r2_role_record;
struct r2_gnss_record;
struct r2_group_config;
/* Diagnostic extended-AD data on the existing GM advertiser only.
 * Group points to immutable enrollment lasting the service lifetime. */
void r2_radio_service_role(const struct r2_role_record *,const struct r2_group_config *);
/* Optional source snapshot, synchronous CONTROL call only; never retained. */
void r2_radio_service_tick_reference(struct r2_radio_scope,const struct r2_source_clock *,int64_t,uint64_t,const struct r2_gnss_record *);
uint32_t r2_radio_service_gnss_sequence(void);
/* Preserve captured same-GM servo history across an owned RC publication
 * handover. No RF tuples, source grant, map or different-parent history moves. */
void r2_radio_service_handoff_request(uint64_t root,uint64_t boot,uint32_t epoch);
bool r2_radio_service_handoff_ready(uint64_t root,uint64_t boot,uint32_t epoch);
bool r2_radio_service_handoff_take(struct r2_relay_precision *,uint64_t root,uint64_t boot,uint32_t epoch,uint64_t counter_epoch);
int r2_radio_service_init(unsigned self, const struct r2_enrollment *roster);
/* Serialized BLE owner only. Immutable source copy from main clock owner. */
void r2_radio_service_tick(struct r2_radio_scope scope,
                           const struct r2_source_clock *source, int64_t utc_ns,
                           uint64_t now_us);
void r2_radio_service_stop(uint64_t now_us);
bool r2_radio_service_closed(void);
int r2_radio_service_error(void);
void r2_radio_service_view(struct r2_radio_precision_view *out,
                           uint64_t now_us);

void r2_radio_service_roster(const struct r2_enrollment *);

struct r2_group_client;
void r2_radio_service_client(const struct r2_group_client *,const struct r2_enrollment *,uint64_t now);

void r2_radio_service_client_state(struct r2_group_client *,uint64_t now);
bool r2_radio_service_client_clock(uint64_t *utc,uint64_t now);

void r2_radio_service_context(struct r2_group_client *,uint64_t now);

bool r2_radio_service_direct_opportunity(uint32_t *accepted);
void r2_radio_service_link_quality(struct r2_link_quality_view *);

/* Main clock owner: independent source withdrawal, never waits for CONTROL. */
void r2_radio_service_source(const struct r2_source_clock *,bool ready,uint32_t source_epoch);
void r2_radio_service_withdraw(void);
void r2_radio_service_time(struct r2_group_client *,uint64_t *source_frame,uint64_t now);

/* Scalar-only ISR observation; deadline failure never fabricates RF release. */
int r2_radio_service_supervise(uint64_t now);

void r2_radio_service_activate(void);

bool r2_radio_service_serving(uint64_t now);

uint32_t r2_radio_service_stack_unused(void);
/* Existing periodic/role advertiser state only; never starts/stops RF. */
void r2_radio_service_gm_diagnostics(uint32_t out[6]);

/* One coherent PT publication for UI/status consumers. */
void r2_radio_service_snapshot(struct r2_radio_precision_view *,struct r2_group_client *time,struct r2_group_client *coarse,uint64_t *frame,uint64_t now);
void r2_radio_service_continuous(bool rc);
int r2_radio_service_reset(uint32_t generation); /* PT-owner acknowledgement after proven drain. */
uint32_t r2_radio_service_profile_flushes(void);
uint32_t r2_radio_service_idle_discards(void);
