/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "ble_service.h"
/* Coarse authenticated JOIN is sufficient for receiver acquisition; never use
 * a cached UI anchor or infer source eligibility from assistance. */
static inline bool r2_peer_aid(const struct r2_service_status *s,uint64_t now,uint64_t *utc,uint64_t *at){
 if(!s->active||s->closing||s->error||s->serving)return false;
 if(s->follower_clock_valid&&s->precision.authorized&&s->root_boot&&s->source_epoch&&now<s->service_until){
  *utc=s->follower_utc;*at=s->time_sampled_us;
 }else if(s->display_coarse_valid){*utc=s->display_coarse_utc;*at=s->display_coarse_at;}
 else return false;
 return now>=*at&&now-*at<1500000;
}
