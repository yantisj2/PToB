/* SPDX-License-Identifier: Apache-2.0 */
#ifndef R2_TIME_SERVICE_H
#define R2_TIME_SERVICE_H
#include "clock.h"
#include "precision.h"
#define R2_TIME_QUEUE 16
#define R2_TIME_CHUNK 64
int64_t r2_time_now_us(void);
void r2_time_init(void);
void r2_time_start(void);
void r2_time_service(void);
void r2_time_stop(void);
int r2_time_close_result(void);
void r2_time_snapshot(struct r2_clock_view *out,uint32_t *drops);
/* Callback ingress: bounded copy only; timestamps are original callback times,
 * not precise hardware captures. No parser, estimator, formatting or storage. */
void r2_time_input_bytes(const uint8_t *data,size_t size,int64_t received);
void r2_time_input_pps(int64_t edge);
/* Actual hardware event plus callback arrival; different clock domains. */
void r2_time_input_precise_pps(struct r2_capture capture,int64_t arrival);
void r2_time_precision_snapshot(struct r2_pps_precision_view *out);
/* Owner-thread lookup only. Relative source map, no UTC or role authority. */
bool r2_time_source_read(struct r2_capture event,uint64_t *ns,uint64_t *generation);
void r2_time_input_fault(enum r2_clock_fault reason);
/* Target adapter; simulator implements exactly this boundary. */
enum r2_association r2_gnss_association(void);
int r2_gnss_start(void);
/* Main-owner only: fresh authenticated peer UTC is an acquisition hint. */
struct r2_gnss_aid_status {uint32_t attempts,completed,aborted,ack;int result;bool busy;uint64_t utc_ms;};
void r2_gnss_aid(uint64_t utc_us,uint64_t at_us,bool valid,bool local_fix);
void r2_gnss_aid_status(struct r2_gnss_aid_status *out);
void r2_gnss_observe(const uint8_t *bytes,size_t size,int64_t received);
int r2_gnss_input_error(void);
void r2_gnss_poll(int64_t now);
bool r2_gnss_capture_now(struct r2_capture *current);
void r2_gnss_stop(void);
int r2_gnss_close_result(void);
/* Causal source-map phase at actual PPS; relative seconds only, no UTC label. */
struct r2_source_pps_view {uint64_t tick,epoch,ns,generation,map_from;int32_t phase_ns;uint32_t observations;bool valid;};
void r2_time_source_pps_snapshot(struct r2_source_pps_view *);

#endif

void r2_time_copy(struct r2_clock *out);

void r2_time_source_snapshot(struct r2_source_clock *out);

/* Main-owner display only; same unqualified source translation as radio. */
bool r2_time_source_utc_now(uint32_t source_epoch,int64_t *utc_us);
