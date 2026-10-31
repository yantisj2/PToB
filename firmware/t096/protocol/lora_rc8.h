/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "group_auth.h"
#include "time_zone.h"
#define RC8_WIRE_VERSION 3u
#define RC8_COMPANION_VERSION 4u
#define RC8_META_BYTES 128u
#define RC8_COMPANION_BYTES RC8_META_BYTES
#define RC8_BYTES 80u
#define RC8_BODY_BYTES 64u
#define RC8_CONTEXT_BYTES 16u
#define RC8_NAME_BYTES RC8_BYTES
#define RC8_NAME_MAX 24u
#if RC8_NAME_MAX != R2_GROUP_NAME_MAX
#error "RC8 name/group name compatibility"
#endif
#define RC8_UNKNOWN UINT32_MAX
#define RC8_MAX_PREP_NS UINT64_C(1000000000)
enum rc8_scale { RC8_RELATIVE=0, RC8_UTC=1 };
enum rc8_origin_kind { RC8_NO_SOURCE=0, RC8_OWN_GM=1, RC8_SELECTED_BLE=2 };
struct rc8_scope {
 uint64_t emitter,emitter_boot,stream,root,root_boot,parent,parent_boot;
 uint32_t source_epoch;
 uint8_t scale,kind;
};
/* A separate immutable-scope HMAC is the authenticated context reference. Stream is
 * a nonzero monotonically increasing incarnation within each emitter boot. */
struct rc8_correction {
 uint64_t event_ns,created_ns,expires_ns;
 uint32_t sequence,map_generation,map_age_us,upstream_bound_ns;
};
enum rc8_quality_bit {RC8_Q_LOCK=1u,RC8_Q_HOLDOVER=2u,RC8_Q_PARENT_PPS=4u,RC8_Q_PARENT_GNSS=8u,
 RC8_Q_PARENT_CLASS=16u,RC8_Q_PARENT_ACCURACY=32u,RC8_Q_PARENT_BOUND=64u,RC8_Q_CORRECTION_AGE=128u,
 RC8_Q_MAP_LIFE=256u,RC8_Q_RATE=512u,RC8_Q_RESIDUAL=1024u,RC8_Q_ROLE=2048u,RC8_Q_HOP=4096u};
enum rc8_diag_event {RC8_D_BOOT=1u,RC8_D_PARENT=2u,RC8_D_ROOT=4u,RC8_D_SOURCE=8u,RC8_D_LOSS=16u,
 RC8_D_RECOVERY=32u,RC8_D_HOLDOVER=64u,RC8_D_QUALITY=128u,RC8_D_PERIODIC=256u,RC8_D_TZ=512u};
struct rc8_quality {
 uint32_t parent_bound_ns,correction_age_ms,map_life_ms;
 int32_t rate_ppb;uint32_t residual_ns;
 uint16_t known,values;uint8_t role,hop,parent_class,parent_accuracy,losses,recoveries;
};
struct rc8_companion {
 struct rc8_scope scope;struct rc8_quality quality;
 struct r2_tz tz;
 uint32_t sequence,snapshot_age_ms,timing_sequence;uint16_t events;bool time_valid;
};
/* Optional, source-opened measurement. Separate permission from source TX.
 * The full context binds emitter/root boots, stream and source epoch. */
enum rc8_probe_kind {RC8_OFFER=4,RC8_REQUEST=5,RC8_RESPONSE=6,RC8_FOLLOW=7};
struct rc8_probe_packet {
 uint8_t context[RC8_CONTEXT_BYTES];
 uint64_t client,boot,nonce,t2,t3;
 uint32_t offer;
 uint8_t kind;
};
int rc8_probe_encode(uint8_t out[RC8_BYTES],const struct rc8_probe_packet *,const struct r2_group_config *);
int rc8_probe_decode(struct rc8_probe_packet *,const uint8_t *,size_t,const struct r2_group_config *);
/* Optional descriptive packet. A name never supplies scope, time, source
 * permission or freshness. The service must match a current full authenticated
 * context and independently reject reordered name sequences. */
struct rc8_name {
 uint8_t context[RC8_CONTEXT_BYTES];uint32_t sequence;char name[RC8_NAME_MAX+1];
};
int rc8_name_encode(uint8_t out[RC8_NAME_BYTES],const struct rc8_name *,const struct r2_group_config *);
/* Output is cleared on every failure. */
int rc8_name_decode(struct rc8_name *,const uint8_t *,size_t,const struct r2_group_config *);
int rc8_companion_encode(uint8_t out[RC8_META_BYTES],const struct rc8_companion *,const struct r2_group_config *);
int rc8_companion_decode(struct rc8_companion *,uint8_t context[RC8_CONTEXT_BYTES],const uint8_t *,size_t,const struct r2_group_config *);
struct rc8_packet {
 uint8_t context[RC8_CONTEXT_BYTES];
 struct rc8_correction correction;
 uint64_t root;uint32_t source_epoch;
 uint32_t sequence;uint8_t scale,kind;
 bool corrected;
};
bool rc8_scope_equal(const struct rc8_scope *,const struct rc8_scope *);
int rc8_scope_validate(const struct rc8_scope *);
int rc8_metadata_encode(uint8_t out[RC8_META_BYTES],const struct rc8_scope *,const struct r2_group_config *);
int rc8_metadata_decode(struct rc8_scope *,uint8_t context[RC8_CONTEXT_BYTES],const uint8_t *,size_t,const struct r2_group_config *);
int rc8_context_id(uint8_t out[RC8_CONTEXT_BYTES],const struct rc8_scope *,const struct r2_group_config *);
int rc8_packet_validate(const struct rc8_packet *);
int rc8_encode(uint8_t out[RC8_BYTES],const struct rc8_packet *,const struct r2_group_config *);
/* Output cleared on failure. Symmetric group authentication is not individual
 * hardware attestation, current-source authority, or absolute replay freshness. */
int rc8_decode(struct rc8_packet *,const uint8_t *,size_t,const struct r2_group_config *);
