/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_control.h"
#include <errno.h>
static bool add(uint64_t a,uint64_t b,uint64_t *v){if(a>UINT64_MAX-b)return false;*v=a+b;return true;}
bool gm7_control_live(const struct gm7_peer_control *s,uint64_t now){return s&&s->control.sequence&&now<s->control_until;}
bool gm7_control_timing_current(const struct gm7_peer_control *s){return s&&s->control.sequence&&s->control.timing_epoch&&s->control.source_gen&&s->control.capture_gen&&s->control.train_gen;}
int gm7_control_check(const struct gm7_peer_control *s,const struct gm7_packet *p,
 uint64_t rx,uint64_t now,uint64_t *original,uint64_t *until){
 if(!s||!p||!original||!until||rx>now||gm7_validate(p)||p->kind>GM7_WITHDRAW)return -EINVAL;
 const struct gm7_control *c=&p->body.control;
 if(c->sequence<=s->control.sequence)return -EALREADY;
 if(s->control.sequence&&c->sent_mono_us<s->control.sent_mono_us)return -ESTALE;
 if(!add(rx,c->max_age_us,until)||now>=*until)return -ESTALE;
 if(p->kind==GM7_WITHDRAW&&s->control.sequence){
  const struct gm7_control *v=&s->control;
  if(c->birth_us!=v->birth_us||c->founder!=v->founder||c->term!=v->term||c->active!=v->active||c->active_boot!=v->active_boot)return -EXDEV;
 }
 *original=rx;return 0;
}
int gm7_control_diagnostic(struct gm7_peer_control *s,const struct gm7_packet *p,uint64_t rx,uint64_t now,uint64_t *until){
 if(!s||!p||!until||rx>now||gm7_validate(p)||(p->kind!=GM7_ROLE&&p->kind!=GM7_GNSS))return -EINVAL;
 uint64_t seq;uint32_t valid;
 if(p->kind==GM7_ROLE){seq=p->body.role.sequence;valid=p->body.role.valid_us;}
 else{seq=p->body.gnss.sequence;valid=p->body.gnss.valid_us;}
 unsigned i=p->kind-GM7_ROLE;
 /* A diagnostic is an independent authenticated observation. It cannot renew
  * any ANN/source permission and does not depend on timing generations. */
 if(seq<=s->last_diag[i])return -ESTALE;
 if(!add(rx,valid,until)||now>=*until)return -ESTALE;
 s->last_diag[i]=seq;return 0;
}
