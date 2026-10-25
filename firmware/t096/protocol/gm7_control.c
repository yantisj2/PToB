/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_control.h"
#include <errno.h>
static bool add(uint64_t a,uint64_t b,uint64_t *v){if(a>UINT64_MAX-b)return false;*v=a+b;return true;}
bool gm7_control_live(const struct gm7_peer_control *s,uint64_t now){return s&&s->last_sequence&&now<s->control_until;}
bool gm7_control_timing_current(const struct gm7_peer_control *s){return s&&s->last_sequence&&s->control.timing_epoch&&s->control.source_gen&&s->control.capture_gen&&s->control.train_gen;}
int gm7_control_check(const struct gm7_peer_control *s,const struct gm7_packet *p,
 uint64_t rx,uint64_t now,uint64_t *original,uint64_t *until,uint64_t *source_until,uint64_t *source_expiry_mono){
 if(!s||!p||!original||!until||!source_until||!source_expiry_mono||rx>now||gm7_validate(p)||p->kind>GM7_WITHDRAW)return -EINVAL;
 const struct gm7_control *c=&p->body.control;
 if(c->sequence<=s->last_sequence)return -EALREADY;
 if(s->last_sequence&&c->sent_mono_us<s->last_sent)return -ESTALE;
 if(!add(rx,c->max_age_us,until)||now>=*until)return -ESTALE;
 if(p->kind==GM7_WITHDRAW&&s->last_sequence){
  const struct gm7_control *v=&s->control;
  if(c->birth_us!=v->birth_us||c->founder!=v->founder||c->term!=v->term||c->active!=v->active||c->active_boot!=v->active_boot||c->train_gen!=v->train_gen||c->timing_epoch!=v->timing_epoch||(c->hot&&(c->hot!=v->hot||c->hot_boot!=v->hot_boot)))return -EXDEV;
 }
 *source_expiry_mono=s->source_expiry_mono;*source_until=s->source_until;
 if(p->kind==GM7_ANNOUNCE){
  uint64_t e,d;if(!add(c->sent_mono_us,c->source_valid_us,&e)||!add(rx,c->source_valid_us,&d))return -EOVERFLOW;
  if(e<=s->source_expiry_mono){if(d>s->source_until)d=s->source_until;}
  else *source_expiry_mono=e;
  *source_until=d;if(now>=d)return -ESTALE;
 }else if(p->kind==GM7_WITHDRAW&&*source_until>rx)*source_until=rx;
 *original=rx;return 0;
}
int gm7_control_diagnostic(struct gm7_peer_control *s,const struct gm7_packet *p,uint64_t rx,uint64_t now,uint64_t *until){
 if(!s||!p||!until||rx>now||gm7_validate(p)||(p->kind!=GM7_ROLE&&p->kind!=GM7_GNSS))return -EINVAL;
 uint64_t sender,boot,seq,birth,founder,term,active,aboot;uint32_t source,capture,train,timing,valid;
#define D(m) do{const struct gm7_##m *d=&p->body.m;sender=d->sender;boot=d->boot;seq=d->sequence;birth=d->birth_us;founder=d->founder;term=d->term;active=d->active;aboot=d->active_boot;source=d->source_gen;capture=d->capture_gen;train=d->train_gen;timing=d->timing_epoch;valid=d->valid_us;}while(0)
 if(p->kind==GM7_ROLE){D(role);}else{D(gnss);}
#undef D
 unsigned i=p->kind-GM7_ROLE;const struct gm7_control *c=&s->control;
 if(!gm7_control_live(s,now)||seq<=s->last_diag[i]||sender!=c->sender||boot!=c->boot||birth!=c->birth_us||founder!=c->founder||term!=c->term||active!=c->active||aboot!=c->active_boot||source!=c->source_gen||capture!=c->capture_gen||train!=c->train_gen||timing!=c->timing_epoch)return -ESTALE;
 if(!add(rx,valid,until)||now>=*until)return -ESTALE;
 s->last_diag[i]=seq;return 0;
}
