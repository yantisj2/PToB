/* SPDX-License-Identifier: Apache-2.0
 * Experimental fixed-route RC5 contract. No GM membership or controller calls.
 * Worker-owned, bounded memory. Not enabled in the installed V14 application.
 */
#pragma once
#include "../../r2-g1/group_auth.h"
#define RC5_BYTES 190u
/* Profile3 separates functional time from precision qualification. */
#if defined(R2_RC_AUTO)
#define RC5_PROFILE 4u
#define RC5_VERSION 6u
#define RC5_CONTROL_BYTES 144u
#define RC5_CONTROL_MAC 128u
#else
#define RC5_PROFILE 3u
#define RC5_VERSION 5u
#define RC5_CONTROL_BYTES 136u
#define RC5_CONTROL_MAC 120u
#endif
#define RC5_NODES 8u
#define RC5_HISTORY 16u
#define RC5_UNKNOWN UINT32_MAX
#define RC5_FRAME_NS UINT64_C(62500000)
#define RC5_SOURCE_NS UINT64_C(360000000000)
#define RC5_CONTEXT_TICKS UINT64_C(5760000000)
#define RC5_LEASE_US UINT32_C(360000000)
#define RC5_RENEW_TICKS UINT64_C(4800000000)
#define RC5_MAP_SPAN_TICKS (UINT64_MAX/125) /* checked conversion domain, not a holdover gate */
#define RC5_CLOCK_BOUND_NS 1000u
struct rc5_node {uint64_t id;uint8_t parent,slot,depth;bool transmitter;};
struct rc5_routes {struct rc5_node node[RC5_NODES];uint8_t count;};
struct rc5_origin {uint64_t root,boot;uint32_t epoch;uint64_t issued,expires;int64_t utc_offset;uint64_t frame;uint32_t hold_us,hold_ns;};
/* Boundary-clock correction: own captured TX timestamp and immutable local
 * map generation/age. No transparent root-event residence identity. */
struct rc5_correction {uint32_t publication;uint16_t event;uint8_t channel;uint32_t generation,age_us;uint64_t tx_ns;uint32_t bound;};
struct rc5_packet {struct rc5_origin origin;uint8_t proof[16];uint8_t sender,count,slot;uint64_t boot;uint32_t train,publication,epoch;struct rc5_correction correction[2];};
bool rc5_routes_valid(const struct rc5_routes *);
int rc5_origin_sign(uint8_t proof[16],const struct rc5_origin *,const struct rc5_routes *,const struct r2_group_config *);
int rc5_encode(uint8_t ad[RC5_BYTES],const struct rc5_packet *,const struct rc5_routes *,const struct r2_group_config *);
int rc5_decode(struct rc5_packet *,const uint8_t *,size_t,const struct rc5_routes *,const struct r2_group_config *);
struct rc5_rate {int32_t ppb,rx_tx_bias_ns;uint32_t uncertainty_ppb,capture_ns,link_ns,link_bound,bias_bound_ns;};
struct rc5_reference {struct rc5_origin origin;uint8_t proof[16];uint64_t rx,completed_at,parent_tx_ns;uint32_t generation,age_us,bound;bool valid;
 /* Immutable match provenance for deferred diagnostics; never estimator input. */
 uint64_t original_available,followup_rx;uint32_t publication;uint16_t event;uint8_t channel;};
struct rc5_pending {struct rc5_packet packet;uint64_t rx,available;uint16_t event;uint8_t channel;bool used;};
/* Challenge-bound coarse time is VALIDITY ONLY, never a precision sample.
 * Interval includes declared coarse oscillator uncertainty and challenge age.
 * expires is the authenticated sample-admission horizon, independent of local
 * holdover and of the root provenance envelope. Stream floor fences replay. */
struct rc5_admission {uint64_t parent_boot,root_boot;uint32_t root_epoch,parent_train,parent_epoch,publication_floor;uint64_t now_tick,root_lo_ns,root_hi_ns,root_expires_ns;};
struct rc5_state {struct rc5_admission admission;struct rc5_routes routes;unsigned self,head;uint64_t parent_boot,root_boot,valid_until,context_until,last_parent_ns;uint32_t publication_high;uint16_t event_high;bool event_seen;uint32_t root_epoch;bool bound;uint32_t parent_train,parent_epoch,accepted,rejected,evicted;struct rc5_pending pending[RC5_HISTORY];struct rc5_reference reference;};
void rc5_init(struct rc5_state *,const struct rc5_routes *,unsigned self);
void rc5_withdraw(struct rc5_state *);
/* Fresh authenticated admission binds boot/source before timing can be used. */
bool rc5_bind(struct rc5_state *,const struct rc5_admission *);
bool rc5_coarse_interval(const struct rc5_state *,uint64_t tick,uint64_t *lo,uint64_t *hi);
/* Caller verifies wire MAC and fixed sender route first; now is local16MHz
 * monotonic capture. No shared UTC is needed for bounded pending history. */
bool rc5_receive(struct rc5_state *,const struct rc5_packet *,uint64_t rx,uint16_t event,uint8_t channel,uint64_t now);
/* Cold authenticated demand is local monotonic authority to START acquisition;
 * it never grants source/timing authority. One transient session, one deadline. */
struct rc5_demand {uint64_t local_boot,peer_boot,challenge,challenge_highwater,deadline,challenge_until;uint32_t highwater;bool pending;};
bool rc5_demand_begin(struct rc5_demand *,uint64_t local_boot,uint64_t peer_boot,uint64_t challenge,uint64_t now_us);
bool rc5_demand_accept(struct rc5_demand *,uint64_t local_boot,uint64_t peer_boot,uint64_t challenge,uint32_t sequence,uint32_t lease_us,uint64_t now_us);
uint32_t rc5_upstream_lease(uint64_t remaining_us);
bool rc5_demand_live(const struct rc5_demand *,uint64_t now_us);
uint32_t rc5_bound_add(uint32_t,uint32_t);
/* Rate estimator consumes only completed parent event/local RX pairs. It does
 * not know simulator oscillator settings. Bound remains UNKNOWN without an
 * independent calibrated rate/capture model. */
struct rc5_rate_fit {uint64_t rx[16],ns[16];unsigned head,count;int32_t ppb;bool valid;};
bool rc5_rate_add(struct rc5_rate_fit *,const struct rc5_reference *);
/* Demeaned relative residual of captured pairs against the current rate.
 * Diagnostic only: excludes absolute offset, propagation and UTC uncertainty. */
bool rc5_rate_rms(const struct rc5_rate_fit *,uint32_t *ns);

enum rc5_control_type { RC5_CAPABILITY=1, RC5_REQUEST=2, RC5_RESPONSE=3 };
struct rc5_control {
 uint8_t type,sender,target,operation;bool ready,publishing;uint16_t slots;
 uint64_t sender_boot,target_boot,challenge,nonce;
 uint32_t sequence,train,capture_epoch,publication;
 uint64_t root_boot;uint32_t root_epoch;
 uint64_t root_lo_ns,root_hi_ns,root_expires_ns;
 uint32_t lease_us;uint64_t next_challenge;
};
int rc5_control_encode(uint8_t ad[RC5_CONTROL_BYTES],const struct rc5_control *,const struct rc5_routes *,const struct r2_group_config *);
int rc5_control_decode(struct rc5_control *,const uint8_t *,size_t,const struct rc5_routes *,const struct r2_group_config *);

bool rc6_occupancy_hint(const uint8_t *,size_t,const struct r2_group_config *,uint64_t own_boot,uint8_t *slot);

/* Functional state and precision qualification are distinct. UNKNOWN bounds
 * never become zero. A simulator error screen is not qualification. */
enum rc5_clock_state {RC5_UNAVAILABLE,RC5_LOCKED,RC5_HELD};
enum rc5_quality {RC5_UNQUALIFIED,RC5_BOUNDED};
struct rc5_map {uint64_t effective,anchor_tick,anchor_ns;int32_t ppb,phase_ns;uint32_t generation,age_us,bound,rate_bound,capture_bound;bool continuous;};
struct rc5_clock {struct rc5_map map[4];unsigned head,count;uint32_t generation,step_fences;uint64_t root_boot,parent_boot;uint32_t root_epoch,parent_epoch;uint64_t last_tick,last_ns,hold_ticks;bool output_seen;};
struct rc5_stamp {uint64_t ns;uint32_t generation,age_us,bound;enum rc5_clock_state state;enum rc5_quality quality;bool pll_tracking;};
void rc5_clock_reset(struct rc5_clock *);
bool rc5_clock_discipline(struct rc5_clock *,const struct rc5_state *,const struct rc5_rate_fit *,const struct rc5_rate *);
bool rc5_clock_capture(const struct rc5_clock *,const struct rc5_state *,uint64_t tick,struct rc5_stamp *);
bool rc5_clock_read(struct rc5_clock *,const struct rc5_state *,uint64_t tick,struct rc5_stamp *);
