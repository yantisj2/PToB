/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm_diagnostics.h"
#include <stdio.h>
#include <stdint.h>
static const char *role(unsigned v){static const char *const r[]={"COLD","ACQUIRING","HOT","ACTIVE"};return v<4?r[v]:"UNKNOWN";}
int r2_gm_diagnostic_format(char *out,size_t size,const struct r2_gm5_diagnostic *d,unsigned page,const char *source,uint64_t physical_boot)
{
 if(!out||!d||!source||page>4)return -1;
 if(size>2200)size=2200;
 const struct r2_gm5 *g=&d->state;uint64_t selected_boot=0;
 for(unsigned i=0;i<R2_GM5_CANDIDATES;i++)if(g->candidate[i].identity==g->selected)selected_boot=g->candidate[i].boot;
 int n=snprintf(out,size,"{\"schema\":1,\"source\":\"%s\",\"boot\":\"%016llx\",\"published_at_us\":%llu,\"page\":%u,\"available\":%s,\"gm_protocol\":6,\"control_envelope\":19,\"auth_config\":5,",source,(unsigned long long)physical_boot,(unsigned long long)d->at,page,g->enabled?"true":"false");
 if(n<0||(size_t)n>=size)return -1;
 int k;
#define U(v) (unsigned long long)(v)
 if(!page)k=snprintf(out+n,size-(size_t)n,
  "\"role\":\"%s\",\"incarnation\":\"%016llx\",\"physical_source_eligible\":%s,\"local_source_epoch\":%u,\"source_epoch\":%u,\"source_until_us\":%llu,\"coordination_inhibited\":%s,\"bridge_leaf\":%s,\"active\":\"%016llx\",\"active_boot\":\"%016llx\",\"active_contact_until_us\":%llu,\"selected\":\"%016llx\",\"selected_boot\":\"%016llx\",\"serving\":%s,\"local_hot\":%s,\"global_hot\":%s,\"global_hot_known\":%s,\"map_valid\":%s,\"locked\":%s,\"hot_refusal\":%u,\"pair_generation\":%u,\"grant_generation\":%u,\"attempt_phase\":\"%s\",\"attempt_started_us\":%llu,\"attempt_until_us\":%llu,\"stable_since_us\":%llu,\"grant_until_us\":%llu,\"issued_until_us\":%llu,\"retire_until_us\":%llu,\"next_attempt_us\":%llu,\"next_poll_us\":%llu,\"root_health_at_us\":%llu,\"output_not_before_us\":%llu,\"last_reason\":%u,\"counts\":[%u,%u,%u,%u,%u,%u,%u]}",
  role(g->role),U(d->boot),d->physical_eligible?"true":"false",d->source_epoch,d->mesh_epoch,U(d->source_until),g->inhibited?"true":"false",g->bridge?"true":"false",U(g->active),U(g->active_boot),U(g->active_until),U(g->selected),U(selected_boot),d->serving?"true":"false",d->local_hot?"true":"false",d->global_hot?"true":"false",d->global_hot_known?"true":"false",d->map_valid?"true":"false",d->locked?"true":"false",d->refusal,d->pair_generation,g->grant_generation,g->role==R2_GM5_COLD||!g->attempt_until?"IDLE":g->contacting?"CONTACT":g->repair_used?"REPAIR":"ACQUIRE",U(g->attempt_started),U(g->attempt_until),U(g->stable_since),U(g->grant_until),U(g->issued_until),U(g->retire_until),U(g->next_attempt),U(g->next_poll),U(d->root_health_at),U(d->output_not_before),g->last_reason,g->attempts,g->failures,g->successes,g->status_tx,g->status_rx,g->control_tx,g->control_rx);
 else if(page<4){const struct r2_gm5_candidate *c=&g->candidate[page-1];k=snprintf(out+n,size-(size_t)n,
  "\"identity\":\"%016llx\",\"incarnation\":\"%016llx\",\"known\":%s,\"fresh\":%s,\"provenance\":\"CAUSAL_G5_JOIN_STATUS\",\"status_at_us\":%llu,\"status_until_us\":%llu,\"physical_report_at_us\":%llu,\"physical_permit_until_us\":%llu,\"physical_permit_fresh\":%s,\"physical_eligible_reported\":%s,\"coordination_inhibited_reported\":%s,\"source_epoch_reported\":%u,\"withdraw_created_remote_us\":%llu,\"withdraw_source_epoch\":%u,\"source_age_ms_reported\":%u,\"role_reported\":\"%s\",\"active_reported\":\"%016llx\",\"active_boot_reported\":\"%016llx\",\"selected_reported\":\"%016llx\",\"selected_boot_reported\":\"%016llx\",\"pair_generation_reported\":%u,\"grant_until_remote_us\":%llu,\"attempt_until_remote_us\":%llu,\"quality_reported\":[%u,%u,%u,%u,%u],\"local_attempt_failures\":%u,\"local_successes\":%u,\"cooldown_until_us\":%llu,\"last_local_failure\":%u,\"last_reason_reported\":%u,\"negative_root_reported\":\"%016llx\",\"remote_deadlines_mapped\":false}",
  U(c->identity),U(c->boot),c->identity?"true":"false",c->identity&&d->at<c->until?"true":"false",U(c->at),U(c->until),U(c->physical_at),U(c->physical_until),c->physical_until>d->at?"true":"false",c->eligible?"true":"false",c->inhibited?"true":"false",c->epoch,U(c->withdraw_created),c->withdraw_epoch,c->source_age_ms,role(c->role),U(c->root),U(c->root_boot),U(c->selected),U(c->selected_boot),c->generation,U(c->grant_until),U(c->attempt_until),c->quality.priority1,c->quality.clock_class,c->quality.accuracy,c->quality.variance,c->quality.priority2,c->failures,c->successes,U(c->cooldown),c->last_reason,c->reported_reason,U(c->negative_root));
 }else{k=snprintf(out+n,size-(size_t)n,"\"produced\":%u,\"overwritten\":%u,\"reset_retained\":false,\"records\":[",g->transitions,g->transitions>R2_GM5_HISTORY?g->transitions-R2_GM5_HISTORY:0);
  if(k<0||(size_t)k>=size-(size_t)n)return -1;
  n+=k;
  unsigned count=g->transitions<R2_GM5_HISTORY?g->transitions:R2_GM5_HISTORY;
  for(unsigned i=0;i<count;i++){const struct r2_gm5_transition *e=&g->history[(g->transitions-count+i)%R2_GM5_HISTORY];
   k=snprintf(out+n,size-(size_t)n,"%s[%llu,\"%016llx\",%u,%u,%u]",i?",":"",U(e->at),U(e->peer),e->generation,e->role,e->reason);
   if(k<0||(size_t)k>=size-(size_t)n)return -1;
  n+=k;
  }k=snprintf(out+n,size-(size_t)n,"]}");
 }
#undef U
 return k<0||(size_t)k>=size-(size_t)n?-1:n+k;
}
