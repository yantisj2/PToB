// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Passive wire3/schedule8 receiver. No BLE model, transmitter or clock authority.
#include "lora_core.h"
#include "lora_passive_core.h"
#include <string.h>
static struct lp_rx receiver;
static struct lp_replay replay;
static struct r2_group_config group;
static uint64_t transmitter,current_tick,current_epoch,current_us,meta_us;
static uint32_t generation,metadata,rejected,replayed;
static bool enabled,time_valid;
static void inc(uint32_t *n){if(*n<UINT32_MAX)++*n;}
bool w12_rc8_init(uint64_t factory,uint64_t source,uint64_t id,uint32_t epoch,const uint8_t key[32]){
 if(!key||!factory||!source||factory==source)return false;
 group=(struct r2_group_config){.id=id,.epoch=epoch};memcpy(group.psk,key,32);
 if(!r2_group_valid(&group))return false;
 lp_rx_clear(&receiver);memset(&replay,0,sizeof replay);transmitter=source;
 generation=metadata=rejected=replayed=0;current_tick=current_epoch=current_us=meta_us=0;enabled=time_valid=false;return true;
}
uint32_t w12_rc8_switch(bool on){
 if(generation==UINT32_MAX){enabled=false;return 0;}
 ++generation;enabled=on;lp_rx_clear(&receiver);time_valid=false;current_tick=current_epoch=current_us=meta_us=0;return generation;
}
void w12_rc8_clock(uint64_t tick,uint64_t epoch,uint64_t now){
 if(!enabled)return;
 lp_rx_expire(&receiver,now);
 if(!tick||!epoch||(current_epoch==epoch&&(tick<current_tick||now<current_us))){lp_rx_clear(&receiver);time_valid=false;return;}
 current_tick=tick;current_epoch=epoch;current_us=now;
 (void)lp_rx_discipline(&receiver,tick,epoch,now);
}
bool w12_rc8_receive(const uint8_t* wire,size_t size,uint64_t tick,uint64_t epoch,uint64_t local,uint64_t now,uint32_t gen){
 if(!enabled||gen!=generation||!wire||!epoch||!tick||now<local){inc(&rejected);return false;}
 lp_rx_expire(&receiver,now);struct lp_packet packet;
 if(lp_decode(&packet,wire,size,receiver.cached?receiver.context:NULL,&group)||packet.kind==LP_DISC){inc(&rejected);return false;}
 if(packet.kind==LP_META){
  if(packet.scope.emitter!=transmitter||!lp_replay_admit(&replay,&packet)){inc(&replayed);return false;}
  // No independent UTC age/source-error interval exists on this no-GNSS target.
  // Authenticated observation leases are not calibrated age/accuracy bounds.
  if(!lp_rx_meta(&receiver,&packet,&group,NULL,local,now)){inc(&rejected);return false;}
  lp_replay_commit(&replay,&packet);time_valid=packet.time_valid;meta_us=local;inc(&metadata);return false;
 }
 if(!receiver.cached||!time_valid){inc(&rejected);return false;}
 if(packet.kind==LP_SYNC){
  const bool ok=lp_rx_sync(&receiver,packet.cursor,tick,epoch,local,now);
  if(!ok)inc(&rejected);
  return false;
 }
 const bool ok=lp_rx_follow(&receiver,&packet,NULL,epoch,now);
 if(!ok)inc(&rejected);
 return ok;
}
void w12_rc8_snapshot(struct w12_lora_view* out,uint64_t now){
 if(!out)return;
 lp_rx_expire(&receiver,now);memset(out,0,sizeof *out);const struct lp_rx* r=&receiver;
 out->sampled_us=now;out->generation=generation;out->state=enabled?1:0;
 out->emitter=r->scope.emitter;out->emitter_boot=r->scope.boot;out->root=r->scope.root;out->root_boot=r->scope.root_boot;out->parent=r->scope.parent;out->parent_boot=r->scope.parent_boot;out->stream=r->scope.stream;out->source_epoch=r->scope.epoch;out->kind=r->scope.kind;out->scale=1;
 memcpy(out->context,r->context,16);out->sequence=r->highest;out->corrected_sequence=r->highest;out->accepted=r->accepted;out->rejected=rejected;out->replayed=replayed;out->unmatched=r->missing;out->metadata=metadata;
 out->metadata_known=r->cached&&now<r->observed_deadline_us;out->time_valid=time_valid&&out->metadata_known;
 out->meta_seed=r->scope.seed;out->meta_cursor=r->meta_cursor;out->meta_utc_ns=r->last_meta_ns;
 out->metadata_us=meta_us;out->diagnostic_sequence=r->serial;out->snapshot_age_ms=meta_us&&now>=meta_us?(uint32_t)((now-meta_us)/1000):UINT32_MAX;
 out->parent_bound_ns=out->residual_ns=out->correction_age_ms=out->map_life_ms=UINT32_MAX;
 out->age_ms=UINT32_MAX;out->link_rms_ns=UINT32_MAX;
 out->trained=r->trained;out->slow_fit=r->hold_selected;
 out->model_valid=r->display_valid&&!r->display_acquiring&&r->model_accepted==r->accepted;
 out->phase_ns=r->display_phase_ns;
 out->reference_age_ms=out->model_valid&&now>=r->display_us&&(now-r->display_us)/1000<=UINT32_MAX?(uint32_t)((now-r->display_us)/1000):UINT32_MAX;
 if(r->last_tick){
  out->edge_us=r->last_us;out->edge_tick=r->last_tick;out->capture_epoch=r->last_epoch;out->event_ns=r->last_tx_ns;
  out->age_ms=now>=r->last_us&&(now-r->last_us)/1000<=UINT32_MAX?(uint32_t)((now-r->last_us)/1000):UINT32_MAX;
  out->link_samples=r->fit_count;out->link_rms_ns=r->trained?r->rms_ns:UINT32_MAX;out->rate_ppb=r->display_rate_ppb;
  uint64_t ns;
  if(enabled&&current_us==now&&lp_rx_estimate(r,current_tick,current_epoch,now,&ns)){
   out->state=2;out->utc_us=ns/1000;out->until_us=r->physical_deadline_us<r->observed_deadline_us?r->physical_deadline_us:r->observed_deadline_us;
  }else if(!r->have_pair)out->state=3;
 }
 // assurance/authority remain zero: no independently bounded admission or relay.
}
