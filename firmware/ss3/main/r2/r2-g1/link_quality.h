/* SPDX-License-Identifier: Apache-2.0
 * Delivery hysteresis, not clock/UTC validity. Serialized receiver owner only.
 * RFC3626 smoothing/hysteresis and RFC6719 current-parent retention inform the
 * method. Numeric thresholds below are deployment defaults, not BLE standards.
 * Each sample contains one second of actual eligible listening at 62.5ms PA.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define R2_LINK_INTERVAL_US UINT64_C(62500)
#define R2_LINK_SAMPLE_US UINT64_C(1000000)
#define R2_LINK_LOW 750u
#define R2_LINK_HIGH 900u
#define R2_LINK_BAD_SAMPLES 5u
#define R2_LINK_GOOD_SAMPLES 5u
struct r2_link_quality_view {
 uint64_t last_rx;
 uint32_t samples,received;
 uint16_t delivery_per_mille;
 bool poor,healthy;
};
struct r2_link_quality {
 struct r2_link_quality_view view;
 uint64_t parent,boot,epoch,at,listened;
 uint32_t train;
 uint16_t event,in_sample;
 uint8_t bad,good;
 bool clock_seen,listening,event_seen;
};
static inline void r2_link_quality_bind(struct r2_link_quality *q,uint64_t parent,uint64_t boot,uint64_t epoch){
 if(parent&&boot&&epoch&&(q->parent!=parent||q->boot!=boot||q->epoch!=epoch)){
  memset(q,0,sizeof *q);q->parent=parent;q->boot=boot;q->epoch=epoch;
 }
}
static inline void r2_link_quality_tick(struct r2_link_quality *q,bool listening,uint64_t now){
 uint64_t elapsed=q->clock_seen&&now>=q->at?now-q->at:0;
 q->clock_seen=true;q->at=now;
 /* A local ownership pause or delayed owner is not measured RF loss. */
 if(!listening||!q->listening||elapsed>250000){
  q->listening=listening;return;
 }
 q->listened+=elapsed;
 if(q->listened<R2_LINK_SAMPLE_US)return;
 uint64_t expected=q->listened/R2_LINK_INTERVAL_US;
 unsigned delivery=(unsigned)((uint64_t)q->in_sample*1000/expected);
 if(delivery>1000)delivery=1000;
 q->view.delivery_per_mille=q->view.samples?(uint16_t)((3u*q->view.delivery_per_mille+delivery)/4u):(uint16_t)delivery;
 if(q->view.samples<UINT32_MAX)++q->view.samples;
 if(delivery<R2_LINK_LOW&&q->view.delivery_per_mille<R2_LINK_LOW){
  q->good=0;if(q->bad<R2_LINK_BAD_SAMPLES)++q->bad;
  if(q->bad>=R2_LINK_BAD_SAMPLES){q->view.poor=true;q->view.healthy=false;}
 }else if(delivery>=R2_LINK_HIGH&&q->view.delivery_per_mille>=R2_LINK_HIGH){
  q->bad=0;if(q->good<R2_LINK_GOOD_SAMPLES)++q->good;
  if(q->good>=R2_LINK_GOOD_SAMPLES){q->view.poor=false;q->view.healthy=true;}
 }else {q->bad=q->good=0;}
 q->listened=0;q->in_sample=0;
}
static inline void r2_link_quality_rx(struct r2_link_quality *q,uint32_t train,uint16_t event,uint64_t now){
 if(!q->parent||!q->boot||!q->epoch||!train||!q->listening)return;
 if(q->event_seen&&q->train==train){
  uint16_t step=(uint16_t)(event-q->event);
  if(!step||(step>=32768&&(now<q->view.last_rx||now-q->view.last_rx<2000000)))return;
  /* A fresh authenticated capture after prolonged silence can reseed a
   * recreated controller counter; it grants no map or source authority. */
 }
 q->event_seen=true;q->train=train;q->event=event;q->view.last_rx=now;
 if(q->in_sample<UINT16_MAX)++q->in_sample;
 if(q->view.received<UINT32_MAX)++q->view.received;
}
