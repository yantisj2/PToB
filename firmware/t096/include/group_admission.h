/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "../protocol/group_join.h"
#include "ble_service.h"
#include "link_quality.h"
#define R2_FIELD_DIAGNOSTIC_US UINT64_C(600000000)
#define R2_LOCAL_DEMAND_RENEW_US UINT64_C(300000000)
#define R2_LOCAL_DEMAND_LEASE_US UINT64_C(360000000)
struct r2_ble_event;
struct r2_group_client {
  bool passive, source;
  uint64_t root, boot, peer, peer_boot, utc_us, at, until, map_until;
  uint32_t source_epoch, publication, uncertainty_us;
};
void r2_group_admission_gm5_policy(struct r2_network *,uint64_t now);
void r2_group_admission_tick(struct r2_network *, uint64_t now);
void r2_group_admission_receive(struct r2_network *,
                                const struct r2_ble_event *, uint64_t now);
void r2_group_admission_require_anchor(void);
void r2_group_admission_refresh_anchor(void);
void r2_group_admission_client(struct r2_group_client *);
bool r2_group_admission_coordination_direct(const struct r2_gm_coordination *,uint64_t now);

/* Single coalesced local demand; fresh exchanges may extend the service lease,
 * never a source expiry. Peer/root identifiers do not allocate control slots. */
struct r2_local_demand {
 uint64_t root,root_boot,peer,peer_boot,at,until;
 uint32_t source_epoch;
 bool received;
};
void r2_group_admission_timing(const struct r2_group_client *);
void r2_group_admission_demand(struct r2_local_demand *out);
/* Failed parent selection retires only our route, not the server's aggregate
 * demand/grace or any source deadline. Serialized, actual RX owner only. */
void r2_group_admission_parent_unavailable(uint64_t parent,uint64_t now);
uint64_t r2_group_admission_pending_until(uint64_t now);

void r2_group_admission_direct_opportunity(uint32_t accepted,uint64_t now);
/* Actual authenticated unique PA delivery; clock-map validity is independent. */
void r2_group_admission_direct_quality(const struct r2_link_quality_view *,uint64_t now);
void r2_group_admission_path_view(uint64_t out[4]); /* Serialized owner only. */
void r2_group_admission_reset(void); /* Serialized, only after proved transport closure. */
void r2_group_admission_relay_allowed(bool); /* Depth1 receiver cannot regenerate depth2. */
/* Actual downstream PA/captured-correction readiness, distinct from an upstream map. */
void r2_group_admission_relay_ready(bool);

/* Serialized owner only; RX policy, independent of fine timing validity. */
bool r2_group_admission_mesh_contact(uint64_t now);

/* GM7 server facade. The existing JOIN nonce/exchange/codec, original transient
 * expiry and transport callback drain are shared unchanged with RC clients.
 * No incoming proof or cached JOIN time can become GM7 HOT/election input. */
struct r2_gm7_admission_source {
#ifdef R2_TZ_AUTO
 struct r2_tz tz;
#endif
 const struct r2_group_config *group;
 uint64_t identity,boot,utc_us,sampled_us,until_us;
 uint32_t timing_epoch,train,map_bound_us;
 const char *name;
 bool serving;
};
/* Existing JOIN source envelope, not GM membership or a follow permission. */
static inline bool r2_gm7_admission_live(const struct r2_gm7_admission_source *s,uint64_t now){
 return s&&s->group&&s->serving&&s->identity&&s->boot&&s->timing_epoch&&s->train&&
  now>=s->sampled_us&&now<s->until_us&&s->until_us-now<=1500000&&
  s->utc_us<=UINT64_MAX-(now-s->sampled_us);
}
void r2_group_admission_gm7_tick(const struct r2_gm7_admission_source *,uint64_t now);
void r2_group_admission_gm7_receive(const struct r2_gm7_admission_source *,const struct r2_ble_event *,uint64_t now);
