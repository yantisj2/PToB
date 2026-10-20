/* Config5: independent source authority and separate second-ranked management. */
#pragma once
#include "authority_codec.h"
#include "group_control.h"
#include "group_join.h"
#include "direct_control_state.h" /* Shared snapshot/coarse types; old core not used. */
#include "election_policy.h"
#include "clock.h"
#include "durability.h"
#include "gm_coordination.h"
#define R2_NET_PEERS 4
/* Retain every50ms issuance through its original250ms ACK lifetime. */
#define R2_ISSUED_RECORDS 8
#define R2_NETWORK_CONFIG R2_AUTH_CONFIG
#include "precision_codec.h"
/* P1 schedule revision 2, bound to the actual PA interval (old: 0x5031320000007a12). */
#define R2_FIXED_SCHEDULE (UINT64_C(0x5031320200000000) | R2_PRECISION_INTERVAL_US)
struct r2_net_peer {
 char name[R2_GROUP_NAME_MAX+1];uint8_t capabilities;
 uint64_t identity,boot,nonce,own_nonce,seen,alive,hello_sequence,sequence,next_hello;
 int64_t offset_low,offset_high;
 uint64_t hello_created,hello_received,map_anchor,sent[4];unsigned sent_head;
 uint8_t tx_key[16],rx_key[16];
 struct r2_clock_quality quality;
 uint64_t candidate_until,next_service,next_management;
 uint64_t serving_until;uint32_t serving_epoch; /* Authenticated SERVICE, not candidacy. */
 uint32_t candidate_epoch,retired_publication,acked_publication;
 uint64_t acked_permit;
 struct {uint64_t sequence,expiry,permit;uint32_t publication;} issued[R2_ISSUED_RECORDS];unsigned issued_head;
 struct r2_auth_record response;bool response_pending;
 uint64_t member_at;bool member;
 bool connected,authenticated,map_valid,source_withdrawn;
};
struct r2_pair {
 uint64_t root_boot,standby_boot,until;
 uint32_t epoch;
 uint8_t root,standby;
};
#define R2_PAIR_RESERVE_US UINT64_C(10000000)
#define R2_MANAGEMENT_LEASE_US UINT64_C(2000000)
struct r2_network {
 /* One bounded authenticated discovery/yield hint, never a control grant. */
 struct r2_group_pair_hint discovery_pair;uint32_t discovery_sender_epoch;uint8_t discovery_sender;
 /* Independently causal UTC interval. ACL loss cannot refresh or erase it;
  * it is never root/source permission or independent source eligibility. */
 uint64_t observed_root,observed_boot,observed_until;uint32_t observed_epoch;
 struct r2_join_time freshness;uint64_t freshness_at,freshness_root,freshness_boot;
 uint32_t freshness_epoch;

 struct r2_group_config group;char name[R2_GROUP_NAME_MAX+1];uint8_t capabilities;
 uint32_t record_age_us,record_age_max_us,hot_withdrawals,hot_withdrawal_reason;
 uint32_t hello_map_refusals,hello_width_us;
 uint64_t hot_withdrawal_at;
 struct r2_pair pair;
 struct r2_net_peer peer[R2_NET_PEERS];
 struct r2_source_admission admission;
 struct r2_clock_quality quality;
 struct v12_dc_coarse candidate,view;
 uint64_t identity,boot,nonce,next_nonce,hello_sequence,tx_sequence,now,fence;
 uint64_t root_boot,coordinator_boot,service_until,service_acked_until;
 uint64_t follower_utc,follower_at,follower_until,follower_last_good;
 uint64_t management_hash,management_pending_until,management_until;
 uint32_t publication,next_publication,management_generation,management_commits;
 uint32_t rejected,authenticated_rx,hello_rx,topologies,follower_updates,service_receipts;
 uint32_t map_bound_us,timing_train,timing_epoch,gm_mask,gnss_mask;
 uint8_t self,coordinator,choice,count,members_mask,management_ready,next_peer;
 uint8_t profile_root,profile_client;
 bool ready,stopped,follower_valid,follower_clock_valid,local_gm,relative_source;
 bool timing_valid,timing_gm,timing_relay,management_committed,hot_ready;
 uint64_t timing_boot,timing_at;
 uint64_t root_health_at,takeover_started,takeover_not_before;
 uint64_t standby_root_boot;uint32_t standby_source_epoch;
 bool standby_authorized,takeover_pending;
};
int r2_network_init(struct r2_network *,uint8_t self,uint8_t count,uint64_t identity,uint64_t boot,uint64_t nonce,uint32_t bound,uint64_t now);
void r2_network_capabilities(struct r2_network *,uint8_t gm_mask,uint8_t gnss_mask);
/* Explicit volatile test profile; default init leaves255 autonomous selection.
 * It never grants source readiness or removes provisioned role capabilities. */
int r2_network_profile_root(struct r2_network *,uint8_t root,uint8_t public_client,uint64_t now);
void r2_network_timing(struct r2_network *,uint64_t boot,uint32_t epoch,uint32_t train,bool gm,bool valid,uint64_t now);
void r2_network_relay_timing(struct r2_network *,uint8_t root,uint64_t boot,uint32_t epoch,uint32_t train,bool gm,bool valid,uint64_t now);
void r2_network_link(struct r2_network *,uint8_t peer,bool connected,uint64_t now);
int r2_network_receive(struct r2_network *,uint8_t peer,const uint8_t *,size_t,uint64_t received,uint64_t now);
void r2_network_tick(struct r2_network *,const struct r2_clock *,uint64_t now);
void r2_network_tick_coordinated(struct r2_network *,const struct r2_clock *,uint64_t now,const struct r2_gm_coordination *);
/* Current authenticated DIRECT root contact/admission, including its explicit
 * source withdrawal; never relay provenance, RSSI or cached membership. */
bool r2_network_coordination_direct(const struct r2_network *,const struct r2_gm_coordination *,uint64_t now);
void r2_network_snapshot(const struct r2_network *,struct v12_dc_snapshot *);
bool r2_network_next(struct r2_network *,uint8_t *,uint8_t *bytes,bool *hello);
bool r2_network_next_ready(struct r2_network *,uint8_t available,uint8_t *,uint8_t *bytes,bool *hello);
void r2_network_sent(struct r2_network *,bool hello,uint8_t peer,const uint8_t *bytes);
void r2_network_stop(struct r2_network *,uint64_t now);

bool r2_network_storage_ready(const struct r2_network *);

/* Diagnostic refusal bits: role, service, root source, standby source, link,
 * timing/map scope, management lease, handshake, pending publication. */
uint32_t r2_network_hot_refusal(const struct r2_network *n);

/* Cached membership never authenticates a new ACL or grants timing authority. */
bool r2_network_member(const struct r2_network *,unsigned peer);

/* Reuse authenticated JOIN proof on its promoted connection; no time map. */
void r2_network_joined(struct r2_network *,unsigned,const struct r2_join_exchange *,uint64_t now);
