/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "rc_boundary.h"
#include "../protocol/group_join.h"
#include "../protocol/group_precision.h"
/* Authenticated normal-GM JOIN is a coarse replay anchor, never a fit sample.
 * The RC's local held map does not expire with this admission context. */
struct rc5_gm_context {struct r2_join_record source;struct r2_join_time time;uint64_t at_tick;bool authenticated,rebind;};
bool rc5_gm_accept(struct rc5_gm_context *,const struct r2_join_record *,uint64_t proof_us,uint64_t received_us,uint64_t tick);
/* Reacquiring RF never renews the authenticated context or source authority. */
static inline bool rc5_gm_reacquire(const struct rc5_gm_context *s,const struct rc5_state *c,uint64_t parent,uint64_t tick){
 return s&&c&&s->authenticated&&c->bound&&tick>=s->at_tick&&
  tick-s->at_tick<RC5_CONTEXT_TICKS&&tick<c->context_until&&
  parent==s->source.sender&&parent==s->source.root&&
  c->routes.count&&c->routes.node[0].id==parent&&
  c->parent_boot==s->source.sender_boot&&c->root_boot==s->source.root_boot&&
  c->root_epoch==s->source.source_epoch&&c->parent_train==s->source.publication;
}
bool rc5_gm_packet(struct rc5_gm_context *,struct rc5_state *,struct rc5_packet *,const uint8_t *,size_t,const struct r2_group_config *,uint64_t tick);
