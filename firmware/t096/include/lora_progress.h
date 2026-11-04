/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define LP_FAULT_RESTART_MS 300000u
enum lp_progress_mode {LP_PROGRESS_IDLE,LP_PROGRESS_DECLARED,LP_PROGRESS_TX_STALLED};
struct lp_progress_watch {uint32_t since_ms,done;uint8_t mode;};
/* Unsigned elapsed arithmetic remains valid across the32-bit uptime wrap.
 * Silence/source absence is not an error. A deliberate maintenance boundary
 * disables this deferred policy; existing urgent hardware fences are separate. */
static inline bool lp_progress_poll(struct lp_progress_watch *s,uint32_t now,
 bool enabled,bool declared,bool eligible,uint32_t done){
 unsigned mode=!enabled?LP_PROGRESS_IDLE:declared?LP_PROGRESS_DECLARED:
  eligible?LP_PROGRESS_TX_STALLED:LP_PROGRESS_IDLE;
 if(!mode){s->mode=0;s->done=done;return false;}
 if(s->mode!=mode||(mode==LP_PROGRESS_TX_STALLED&&s->done!=done))s->since_ms=now;
 s->mode=(uint8_t)mode;s->done=done;
 return (uint32_t)(now-s->since_ms)>=LP_FAULT_RESTART_MS;
}
