/* SPDX-License-Identifier: Apache-2.0
 * Experimental fixed-route RC4 contract. No GM membership or controller calls.
 * Worker-owned, bounded memory. Not enabled in the installed V14 application.
 */
#pragma once
#include "../protocol/group_auth.h"
#define RC4_BYTES 190u
#define RC4_NODES 8u
#define RC4_HISTORY 16u
#define RC4_UNKNOWN UINT32_MAX
#define RC4_FRAME_NS UINT64_C(62500000)
#define RC4_SOURCE_NS UINT64_C(1650000000)
#define RC4_CONTEXT_TICKS UINT64_C(5760000000)
#define RC4_LEASE_US UINT32_C(360000000)
#define RC4_RENEW_TICKS UINT64_C(4800000000)
#define RC4_REFERENCE_NS UINT64_C(500000000)
struct rc4_node {uint64_t id;uint8_t parent,slot,depth;bool transmitter;};
struct rc4_routes {struct rc4_node node[RC4_NODES];uint8_t count;};
struct rc4_origin {uint64_t root,boot;uint32_t epoch;uint64_t issued,expires;int64_t utc_offset;uint64_t frame;uint32_t hold_us,hold_ns;};
/* root_ns identifies the original root physical event under boot/source epoch.
 * tx_ns is that root timestamp plus every completed residence+propagation once.
 * publication/event/channel identify THIS transmitter's exact original packet.
 */
struct rc4_correction {uint32_t publication;uint16_t event;uint8_t channel;uint64_t root_ns,tx_ns;uint32_t bound;};
struct rc4_packet {struct rc4_origin origin;uint8_t proof[16];uint8_t sender,count;uint64_t boot;uint32_t train,publication,epoch;struct rc4_correction correction[2];};
bool rc4_routes_valid(const struct rc4_routes *);
int rc4_origin_sign(uint8_t proof[16],const struct rc4_origin *,const struct rc4_routes *,const struct r2_group_config *);
int rc4_encode(uint8_t ad[RC4_BYTES],const struct rc4_packet *,const struct rc4_routes *,const struct r2_group_config *);
int rc4_decode(struct rc4_packet *,const uint8_t *,size_t,const struct rc4_routes *,const struct r2_group_config *);
struct rc4_rate {int32_t ppb,rx_tx_bias_ns;uint32_t uncertainty_ppb,capture_ns,link_ns,link_bound,bias_bound_ns;};
struct rc4_reference {struct rc4_origin origin;uint8_t proof[16];uint64_t rx,completed_at,root_ns,parent_tx_ns;uint32_t bound;bool valid;};
struct rc4_pending {struct rc4_packet packet;uint64_t rx,available;uint16_t event;uint8_t channel;bool used;};
/* Challenge-bound coarse time is VALIDITY ONLY, never a precision sample.
 * Interval includes source uncertainty and challenge age. Root expiry is the
 * authenticated permission expiry. Stream floor attests current parent state. */
struct rc4_admission {uint64_t parent_boot,root_boot;uint32_t root_epoch,parent_train,parent_epoch,publication_floor;uint64_t now_tick,root_lo_ns,root_hi_ns,root_expires_ns;};
struct rc4_state {struct rc4_admission admission;struct rc4_routes routes;unsigned self,head;uint64_t parent_boot,root_boot,valid_until,context_until,last_root_ns,last_parent_ns;uint32_t publication_high;uint16_t event_high;bool event_seen;uint32_t root_epoch;bool bound;uint32_t parent_train,parent_epoch,accepted,rejected,evicted;struct rc4_pending pending[RC4_HISTORY];struct rc4_reference reference;};
void rc4_init(struct rc4_state *,const struct rc4_routes *,unsigned self);
void rc4_withdraw(struct rc4_state *);
/* Fresh authenticated admission binds boot/source before timing can be used. */
bool rc4_bind(struct rc4_state *,const struct rc4_admission *);
bool rc4_coarse_interval(const struct rc4_state *,uint64_t tick,uint64_t *lo,uint64_t *hi);
/* Caller verifies wire MAC and fixed sender route first; now is local16MHz
 * monotonic capture. No shared UTC is needed for bounded pending history. */
bool rc4_receive(struct rc4_state *,const struct rc4_packet *,uint64_t rx,uint16_t event,uint8_t channel,uint64_t now);
/* Forward only a completed upstream event. No UTC-map projection is an input.
 * Exact TX capture must follow reference availability. */
bool rc4_forward(const struct rc4_state *,uint64_t tx,uint64_t available_at,const struct rc4_rate *,uint32_t publication,uint16_t event,uint8_t channel,struct rc4_correction *);
/* Cold authenticated demand is local monotonic authority to START acquisition;
 * it never grants source/timing authority. One transient session, one deadline. */
struct rc4_demand {uint64_t local_boot,peer_boot,challenge,challenge_highwater,deadline,challenge_until;uint32_t highwater;bool pending;};
bool rc4_demand_begin(struct rc4_demand *,uint64_t local_boot,uint64_t peer_boot,uint64_t challenge,uint64_t now_us);
bool rc4_demand_accept(struct rc4_demand *,uint64_t local_boot,uint64_t peer_boot,uint64_t challenge,uint32_t sequence,uint32_t lease_us,uint64_t now_us);
uint32_t rc4_upstream_lease(uint64_t remaining_us);
bool rc4_demand_live(const struct rc4_demand *,uint64_t now_us);
uint32_t rc4_bound_add(uint32_t,uint32_t);
/* Passive-client output only: original source expiry, max1.5s local reference;
 * cannot be passed to forwarding and never grants PTS or source authority. */
bool rc4_read(const struct rc4_state *,uint64_t now,const struct rc4_rate *,uint64_t *ns,uint32_t *bound);
/* Rate estimator consumes only completed parent event/local RX pairs. It does
 * not know simulator oscillator settings. Bound remains UNKNOWN without an
 * independent calibrated rate/capture model. */
struct rc4_rate_fit {uint64_t rx[16],ns[16];unsigned head,count;int32_t ppb;bool valid;};
bool rc4_rate_add(struct rc4_rate_fit *,const struct rc4_reference *);

#define RC4_CONTROL_BYTES 136u
enum rc4_control_type { RC4_CAPABILITY=1, RC4_REQUEST=2, RC4_RESPONSE=3 };
struct rc4_control {
 uint8_t type,sender,target;bool ready;
 uint64_t sender_boot,target_boot,challenge,nonce;
 uint32_t sequence,train,capture_epoch,publication;
 uint64_t root_boot;uint32_t root_epoch;
 uint64_t root_lo_ns,root_hi_ns,root_expires_ns;
 uint32_t lease_us;uint64_t next_challenge;
};
int rc4_control_encode(uint8_t ad[RC4_CONTROL_BYTES],const struct rc4_control *,const struct rc4_routes *,const struct r2_group_config *);
int rc4_control_decode(struct rc4_control *,const uint8_t *,size_t,const struct rc4_routes *,const struct r2_group_config *);
