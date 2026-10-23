/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "service_backend_alias.h"
#define R2_FIELD_CLIENT_WINDOW_US 450000u /* explicit FIELD450_QUIET500_V1 treatment */
#include "app.h"
#include "ui.h"
#include "network.h"
#include "ble_transport.h"
#include "radio_precision.h"
#include "relay_service.h"
#include "../protocol/relay_admission.h"
#include "rc_boundary.h"
struct r2_enrollment {
 struct r2_group_config group;uint8_t local_capabilities;
 bool rc_dynamic,rc7_leaf,rc7_exclude_direct_gm,rc7_boot_rc;
 uint64_t rc7_boot_parent; /* Optional diagnostic selector; GM entry clears RAM copy. */
 struct rc5_routes auto_routes; /* Immutable optional two-RC field topology. */
 bool boundary_field; /* Explicit RC5 field image; autonomous path unchanged. */
 uint8_t boundary_profile; /* Reviewed identity-bound test roster; no live switch. */
 bool field_profile; /* FIELD_TC01_TC02_TC03_V1: explicit image opt-in. */
 uint64_t identity[4];
 uint8_t address[4][6],key[4][4][16];
 unsigned count;
 uint8_t gm_mask,gnss_mask;
 uint32_t map_bound_us;
};
/* Bounded RAM diagnostics, not reset-retained. All access under service lock.
 * state0 idle,1 queued,2 executing; fault site1 busy-age,2 drain,3 enqueue. */
struct r2_supervision {
 uint64_t submitted,started,fault_at,fault_submitted,fault_started;
 uint32_t queue_max_us,late_queues;
 uint8_t state,fault_site,fault_state;
 uint64_t stage_at,fault_stage_at;
 uint32_t stage_max_us[7],owner_max_us;
 uint8_t stage,fault_stage;
};
struct r2_service_status {
#if defined(R2_RC_DYNAMIC_FIXTURE)
 /* Model-only owner publication of actual reservation, no native RAM/wire. */
 uint64_t fixture_pair_root_identity,fixture_pair_standby_identity,fixture_pair_root_boot,fixture_pair_standby_boot;
#endif
 uint64_t direct_path[4]; /* poor, last corrected progress us, leased parent, lease expiry us */
 bool profile_rc,profile_pending,profile_target_rc;uint8_t profile_phase;uint32_t profile_changes;uint64_t profile_incarnation;
 struct r2_supervision supervision;
 struct r2_transport_fault transport_fault;
 uint64_t identities[4],group_id;uint32_t key_epoch;uint8_t authenticated_mask;char name[25],peer_names[4][25];
 bool group_member,public_client;
 uint8_t membership_mask;bool standby_authorized,takeover_pending;
 uint64_t root_health_at,takeover_started,takeover_not_before,authority_until;
 struct r2_radio_precision_view precision;
 struct r2_relay_status relay;
 uint64_t relay_session,activation_until;bool boundary_field; /* Explicit RC5 field image; autonomous path unchanged. */
 uint8_t boundary_profile; /* Reviewed identity-bound test roster; no live switch. */
 bool field_profile;
 bool relay_pending;
 enum r2_relay_pending_reason relay_pending_reason;
 uint64_t relay_request_until;
 uint64_t at,deadline,close_deadline,closed_at,follower_utc,service_until,root_boot;
 uint64_t source_frame_ns,time_sampled_us;
 uint64_t display_coarse_utc,display_coarse_at;bool display_coarse_valid;
 uint64_t hot_withdrawal_at;uint32_t hot_withdrawals,hot_withdrawal_reason;
 uint64_t role_until,standby_boot,pair_until,management_until,source_evidence;
 uint32_t pair_epoch,hot_refusal,record_age_us,record_age_max_us,rx_latency_max_us,backlog,rx_queue_peak;
 struct r2_clock_quality quality;
 uint32_t admission_ms;
 bool source_eligible,gm_capable,hot_ready,gm_cold;
 uint32_t lease_expired,core_refusal;
 uint32_t hello_map_refusals,hello_width_us;
 uint32_t source_epoch,local_source_epoch,service_receipts,cycles,rx,authenticated,rejected,drops,errors,worker_max_us;
 unsigned self,coordinator,root,standby,members,phase;
 int error;
 uint8_t lamps[8];
 struct r2_link_quality_view reception; /* Existing selected timing-parent delivery. */
 bool normal;
 bool configured,active,closing,closed,serving,follower,follower_clock_valid;
 bool peer_delivery_confirmed,storage_window;
};
/* Before bt_enable. Provisioned identity and keys are not discovery inputs. */
int r2_service_configure(const struct r2_identity *id,const struct r2_enrollment *enrollment);
/* Main clock owner publishes an immutable copy. Worker never runs GNSS/parser. */
void r2_service_clock(const struct r2_clock *clock);
/* One finite bench treatment per boot; zero requests stop. Normal continuous
 * service is a separate profile, never implied by a finite test. */
int r2_service_start(uint32_t seconds,uint64_t arrival_ms);
int r2_service_run(uint64_t arrival_ms);
void r2_service_stop(void);
int r2_service_close_result(void);
void r2_service_snapshot(struct r2_service_status *out);
void r2_service_ui(struct r2_ui_model *out);
void r2_service_supervise(void);

int r2_service_preflight(uint32_t seconds,uint64_t arrival_ms);
int r2_service_relay(uint64_t session,uint32_t seconds,uint64_t arrival_ms);

int r2_service_name(const char *name);

/* Pending-only cancellation; EBUSY once exclusive transition is admitted. */
int r2_service_relay_cancel(uint64_t session);

uint32_t r2_service_stack_unused(void);

int r2_service_profile(unsigned mode,uint64_t arrival_ms);
void r2_service_profile_status(uint32_t out[5]);
#ifdef R2_RC_PROFILE
int r2_service_test_parent(uint64_t parent,uint64_t arrival_ms);
void r2_service_test_parent_status(uint64_t *parent,bool *relay_only);
void r2_service_test_transport_status(uint32_t out[13]);
void r2_service_test_acquisition_status(uint32_t out[20]);
void r2_service_test_gm_admission_status(uint32_t out[7]);
void r2_service_test_connection_status(uint32_t out[20]);
#endif

#if defined(R2_SWITCHABLE_FIELD)
bool r2_switch_rc_active(void);
#endif
#if defined(R2_RC_PROFILE)
struct bt_conn;
int r2_rc7_gm_receive(struct bt_conn *,const void *,uint16_t,uint16_t);
#endif
