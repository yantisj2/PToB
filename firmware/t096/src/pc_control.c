/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "pc_experiment.h"
#include "capture.h"
#include <errno.h>
#include <stdio.h>
#include <zephyr/kernel.h>
static struct k_spinlock lock;
static struct pc_experiment experiment;
void r2_pc_init(void){pc_init(&experiment);}
int r2_pc_begin(unsigned mode,uint64_t durations){
 struct r2_capture n;if(!r2_capture_now(&n))return -EAGAIN;
 k_spinlock_key_t k=k_spin_lock(&lock);
 int rc=pc_begin(&experiment,mode,(uint32_t)(durations>>32),(uint32_t)durations,n.tick,n.epoch);
 k_spin_unlock(&lock,k);return rc;
}
int r2_pc_begin_cadence(uint32_t cycles,uint64_t durations){
 struct r2_capture n;if(!r2_capture_now(&n))return -EAGAIN;
 k_spinlock_key_t k=k_spin_lock(&lock);
 int rc=pc_begin_cadence(&experiment,cycles,(uint32_t)(durations>>32),(uint32_t)durations,n.tick,n.epoch);
 k_spin_unlock(&lock,k);return rc;
}
void r2_pc_poll(void){struct r2_capture n;if(!r2_capture_now(&n))return;k_spinlock_key_t k=k_spin_lock(&lock);pc_tick(&experiment,n.tick,n.epoch);k_spin_unlock(&lock,k);}
bool r2_pc_reset(uint64_t tick,uint64_t epoch){
 k_spinlock_key_t k=k_spin_lock(&lock);bool reset=experiment.reset_pending;
 if(reset){experiment.reset_pending=false;experiment.reset_from=tick;experiment.epoch=epoch;}
 k_spin_unlock(&lock,k);return reset;
}
bool r2_pc_observe(const struct rc5_reference *r,uint64_t epoch){k_spinlock_key_t k=k_spin_lock(&lock);bool train=pc_observe(&experiment,r,epoch);k_spin_unlock(&lock,k);return train;}
void r2_pc_offer(const struct rc_shadow_map *m){k_spinlock_key_t k=k_spin_lock(&lock);pc_offer(&experiment,m);k_spin_unlock(&lock,k);}
bool r2_pc_witness_map(struct r2_capture e,struct rc_shadow_map *m){k_spinlock_key_t k=k_spin_lock(&lock);bool found=pc_witness_map(&experiment,e,m);k_spin_unlock(&lock,k);return found;}
bool r2_pc_quiet(void){k_spinlock_key_t k=k_spin_lock(&lock);bool quiet=pc_quiet(&experiment);k_spin_unlock(&lock,k);return quiet;}
void r2_pc_closed(uint64_t tick){k_spinlock_key_t k=k_spin_lock(&lock);if(!experiment.rf_closed)experiment.rf_closed=tick;k_spin_unlock(&lock,k);}
int r2_pc_json(char *out,size_t cap,uint32_t after,int result){
 struct pc_record q={0};struct rc_shadow_map m;uint64_t requested,first,until,from,end,closed,now;uint32_t trial,phase,mode,listen,hold,produced,overwritten,pairs,training,returned,failures,cycle,cycles,missed,burst;bool found,map;
 k_spinlock_key_t k=k_spin_lock(&lock);
 const struct pc_experiment *p=&experiment;found=pc_read(p,after,&q);m=p->have_frozen?p->frozen:p->latest;map=p->have_frozen||p->have_map;
 requested=p->requested;first=p->first_pair;until=p->listen_until;from=p->hold_from;end=p->hold_until;closed=p->rf_closed;now=p->last_tick;
 trial=p->trial;phase=p->phase;mode=p->mode;listen=p->listen_ms;hold=p->hold_ms;produced=p->produced;overwritten=p->overwritten;pairs=p->pairs;training=p->training_pairs;returned=p->first_return_sequence;failures=p->failures;cycle=p->cycle;cycles=p->cycles;missed=p->missed_bursts;burst=p->burst_pairs;
 k_spin_unlock(&lock,k);
 return snprintf(out,cap,"{\"image_profile\":\"PC\",\"schema\":1,\"result\":%d,\"awake_only\":true,\"experimental\":true,\"absolute_utc_qualified\":false,\"airtime_measured\":false,\"trial\":%u,\"phase\":%u,\"mode\":%u,\"listen_ms\":%u,\"hold_ms\":%u,\"requested_tick\":%llu,\"first_pair_tick\":%llu,\"listen_until_tick\":%llu,\"hold_from_tick\":%llu,\"hold_until_tick\":%llu,\"rf_closed_tick\":%llu,\"now_tick\":%llu,\"pairs\":%u,\"training_pairs\":%u,\"cycle\":%u,\"cycles\":%u,\"missed_bursts\":%u,\"burst_pairs\":%u,\"first_return_sequence\":%u,\"failures\":%u,\"map_present\":%s,\"parent\":\"%016llx\",\"parent_boot\":\"%016llx\",\"root\":\"%016llx\",\"root_boot\":\"%016llx\",\"root_epoch\":%u,\"capture_epoch\":%llu,\"map_generation\":%u,\"map_effective_tick\":%llu,\"rate_ppb\":%d,\"produced\":%u,\"overwritten\":%u,\"found\":%s,\"record\":{\"sequence\":%u,\"trial\":%u,\"phase\":%u,\"rx_tick\":%llu,\"available_tick\":%llu,\"remote_ns\":%llu,\"predicted_ns\":%llu,\"error_ns\":%lld,\"valid\":%s,\"capture_epoch\":%llu,\"generation\":%u,\"cycle\":%u,\"train\":%s}}",
 result,trial,phase,mode,listen,hold,(unsigned long long)requested,(unsigned long long)first,(unsigned long long)until,(unsigned long long)from,(unsigned long long)end,(unsigned long long)closed,(unsigned long long)now,pairs,training,cycle,cycles,missed,burst,returned,failures,map?"true":"false",(unsigned long long)m.parent,(unsigned long long)m.parent_boot,(unsigned long long)m.root,(unsigned long long)m.root_boot,m.root_epoch,(unsigned long long)m.epoch,m.map.generation,(unsigned long long)m.map.effective,m.map.ppb,produced,overwritten,found?"true":"false",q.sequence,q.trial,q.phase,(unsigned long long)q.rx,(unsigned long long)q.available,(unsigned long long)q.remote_ns,(unsigned long long)q.predicted_ns,(long long)q.error_ns,q.valid?"true":"false",(unsigned long long)q.epoch,q.generation,q.cycle,q.train?"true":"false");
}
