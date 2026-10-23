// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Shares T096's actual wire3 codec/policy. This receiver owns no BLE API/map.
#include "lora_core.h"
#include "lora_rc8_policy.h"
#include <string.h>
static struct rc8_receiver receiver;
static struct r2_group_config group;
static uint32_t metadata;
bool w12_rc8_init(uint64_t factory,uint64_t transmitter,uint64_t id,uint32_t epoch,const uint8_t key[32]){
 if(!key)return false;
 group=(struct r2_group_config){.id=id,.epoch=epoch};memcpy(group.psk,key,32);metadata=0;
 const struct rc8_enrollment enrollment={.factory=factory,.transmitter_factory=transmitter,.enrolled=r2_group_valid(&group)};
 return factory!=transmitter&&rc8_receiver_init(&receiver,&enrollment);
}
uint32_t w12_rc8_switch(bool on){rc8_receiver_switch(&receiver,on);return receiver.generation;}
bool w12_rc8_receive(const uint8_t* wire,size_t size,uint64_t tick,uint64_t epoch,uint64_t local,uint64_t now,uint32_t generation){
 const struct rc8_event event={.tick=tick,.capture_epoch=epoch,.local_us=local,.generation=generation,.kind=RC8_EVENT_RX_DONE};
 if(!rc8_receiver_freeze(&receiver,wire,size,event))return false;
 const bool result=rc8_receiver_process(&receiver,&group,now);
 if(size==RC8_META_BYTES&&receiver.metadata_known&&receiver.diagnostic_us==local&&metadata<UINT32_MAX)metadata++;
 return result;
}
void w12_rc8_snapshot(struct w12_lora_view* out,uint64_t now){
 if(!out)return;
 memset(out,0,sizeof *out);const struct rc8_receiver* r=&receiver;
 out->sampled_us=now;out->generation=r->generation;out->state=rc8_receiver_state(r,now);out->assurance=rc8_receiver_assurance(r,now);out->authority=rc8_receiver_authority(r,now);
 out->emitter=r->scope.emitter;out->emitter_boot=r->scope.emitter_boot;out->root=r->scope.root;out->root_boot=r->scope.root_boot;out->parent=r->scope.parent;out->parent_boot=r->scope.parent_boot;out->stream=r->scope.stream;out->source_epoch=r->scope.source_epoch;out->kind=r->scope.kind;out->scale=r->scope.scale;
 memcpy(out->context,r->context,16);out->sequence=r->highest_sequence;out->accepted=r->accepted;out->rejected=r->rejected;out->replayed=r->replayed;out->overflow=r->overflow;out->unmatched=r->unmatched;out->metadata=metadata;out->metadata_misses=r->metadata_misses;
 out->metadata_known=r->metadata_known&&now>=r->diagnostic_us&&now<r->metadata_until_us;out->time_valid=r->time_valid;
 out->metadata_us=r->diagnostic_us;out->snapshot_age_ms=r->snapshot_age_ms;out->diagnostic_sequence=r->diagnostic_sequence;out->events=r->diagnostic_events;
 const struct rc8_quality* q=&r->quality;out->quality_known=q->known;out->quality_values=q->values;out->role=q->role;out->hop=q->hop;out->parent_class=q->parent_class;out->parent_accuracy=q->parent_accuracy;out->parent_bound_ns=q->parent_bound_ns;out->correction_age_ms=q->correction_age_ms;out->map_life_ms=q->map_life_ms;out->rate_ppb=q->rate_ppb;out->residual_ns=q->residual_ns;out->losses=q->losses;out->recoveries=q->recoveries;
 out->parent_quality=out->metadata_known&&(q->known&(RC8_Q_PARENT_CLASS|RC8_Q_PARENT_ACCURACY))==(RC8_Q_PARENT_CLASS|RC8_Q_PARENT_ACCURACY)&&out->snapshot_age_ms<10000&&(now-r->diagnostic_us)/1000<10000-out->snapshot_age_ms;
 out->age_ms=UINT32_MAX;out->link_rms_ns=UINT32_MAX;
 if(r->sample.valid){
  const uint64_t age=now>=r->sample.rx.local_us?now-r->sample.rx.local_us:UINT64_MAX;
  out->edge_us=r->sample.rx.local_us;out->edge_tick=r->sample.rx.tick;out->capture_epoch=r->sample.rx.capture_epoch;
  out->corrected_sequence=r->sample.tx.sequence;out->created_ns=r->sample.tx.created_ns;out->expires_ns=r->sample.tx.expires_ns;out->event_ns=r->sample.tx.event_ns;out->map_generation=r->sample.tx.map_generation;out->map_age_us=r->sample.tx.map_age_us;
  out->age_ms=age/1000>UINT32_MAX?UINT32_MAX:(uint32_t)(age/1000);
  if(out->state==RC8_AVAILABLE){
   out->link_samples=r->fit.n;out->link_rms_ns=r->fit.n>=4?r->fit.rms_ns:UINT32_MAX;
   if(r->scope.scale==RC8_UTC&&age<RC8_HOLD_US&&r->sample.tx.event_ns<=UINT64_MAX-age*1000&&r->sample.rx.local_us<=UINT64_MAX-RC8_HOLD_US){out->utc_us=r->sample.tx.event_ns/1000+age;out->until_us=r->sample.rx.local_us+RC8_HOLD_US;}
  }
 }
}
