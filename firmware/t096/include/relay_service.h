/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "relay_precision.h"
#include "link_quality.h"
struct r2_enrollment;
struct r2_relay_status {
 uint8_t slot;uint32_t slot_scans,slot_changes,slot_rollbacks,slot_busy,slot_requests,client_quiet_faults;
 uint32_t slot_scan_verified,slot_scan_failures;uint64_t slot_scan_owned_us;
 uint32_t upstream_recoveries;bool sync_creating,sync_active,phase_acquiring;
 uint64_t sampled_us;
 uint64_t root_identity,parent_identity,root_boot,source_revision,utc_us,valid_until_us,source_until_us;
 uint64_t lifecycle,rx_lifecycle,tx_lifecycle;
 uint64_t source_created_ns,source_expires_ns,source_frame_ns;
 uint32_t source_epoch,upstream_train,upstream_publication,train,map_generation,accepted,tx_corrections,rejected,duplicates;
 uint32_t host_notifications;
 uint64_t recovery_lost_at,recovery_until;
 bool recovery_grace_active,held_time,holdover_bound_known;
 uint32_t capture_completed,capture_invalid,capture_overflow,flushed,cost_refusals,retries;
 uint32_t update_age_ms,source_age_ms,stale_tx;
 uint64_t start_delay_ticks,publish_ticks,discipline_gap_max_ticks;
 uint32_t holdover_expiries,holdover_recoveries;
 uint32_t correction_age_ms,bound_ns,reservation_max_us,prepare_max_us,rx_window_max_us;
 int64_t phase_ns,target_ns,error_ns;
 unsigned root,parent,role;uint8_t time_relay_hops;
 bool selected,active,map_valid,phase_admitted,quiet;
 uint32_t window_faults,window_recoveries,window_fault_site,fault_line;
 uint64_t window_fault_at,window_fault_deadline;
 bool window_recovering;
 int error;
};
int r2_relay_service_init(unsigned self,const struct r2_enrollment *roster,uint64_t boot,void (*wake)(void));
/* One explicit volatile profile per boot; owner must have drained P1 first. */
int r2_relay_service_start(uint64_t now_us);
void r2_relay_service_activation(uint64_t until_us);
void r2_relay_service_passive(bool passive);
void r2_relay_service_continuous(bool continuous);
void r2_relay_service_tick(struct r2_radio_scope authority,const struct r2_source_clock *source,
                         int64_t trusted_coarse_utc_ns,bool coarse_fresh,uint64_t now_us);
void r2_relay_service_stop(uint64_t now_us);
bool r2_relay_service_closed(void);
bool r2_relay_service_serving(uint64_t now_us);
void r2_relay_service_link_quality(struct r2_link_quality_view *);
int r2_relay_service_error(void);
void r2_relay_service_snapshot(struct r2_relay_status *out,
                             struct r2_radio_precision_view *precision,uint64_t now_us);

void r2_relay_service_roster(const struct r2_enrollment *);

/* Private timing-path selection; never changes control membership. */
int r2_relay_service_path(uint64_t root_id,uint64_t relay_id,uint64_t local_id,uint64_t root_boot,uint32_t source_epoch);

int r2_relay_service_release_path(void);
int r2_relay_service_profile_reset(uint64_t boot); /* No callback/thread recreation; requires closure. */
