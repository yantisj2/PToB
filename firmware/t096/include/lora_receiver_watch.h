/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "lora_passive_core.h"

/* Same-boot human clock, deliberately separate from the live receiver. */
struct lp_receiver_watch {
 struct lp_rx rx;
 uint64_t admission_epoch,error_us;
 double square;
 uint32_t recent[8],recent_count,recent_head,predictions,total_predictions,maximum,over1us;
 uint32_t recent_rms,total_rms;
 int32_t last_error;
 uint8_t reason;
 bool admitted,last_clipped;
};
void lp_receiver_watch_fence(struct lp_receiver_watch *,uint8_t reason);
bool lp_receiver_watch_meta(struct lp_receiver_watch *,const struct lp_packet *,const struct r2_group_config *,uint64_t epoch,uint64_t event,uint64_t now);
bool lp_receiver_watch_sync(struct lp_receiver_watch *,uint32_t cursor,uint64_t tick,uint64_t epoch,uint64_t event,uint64_t now);
bool lp_receiver_watch_follow(struct lp_receiver_watch *,const struct lp_packet *,uint64_t epoch,uint64_t now,bool delivered_known,uint64_t delivered_ns);
bool lp_receiver_watch_time(struct lp_receiver_watch *,uint64_t tick,uint64_t epoch,uint64_t now,uint64_t *ns);
uint32_t lp_receiver_watch_rms(const struct lp_receiver_watch *);
uint32_t lp_receiver_watch_total_rms(const struct lp_receiver_watch *);
uint32_t lp_receiver_watch_age(uint64_t now,uint64_t event);
