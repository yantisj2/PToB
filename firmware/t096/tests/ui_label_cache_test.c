/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Production label publication/optional-cache commit with synthetic owners. */
#include <stdio.h>
static void cache_holds(struct r2_ui_holds *,const struct r2_ui_model *,uint64_t);
#define r2_ui_holds_update cache_holds
#include "../src/ui_service.c"
#undef r2_ui_holds_update
static struct r2_service_status owner;
static struct r2_ui_context ctx={.incarnation=1,.root=4,.boot=73,.source_epoch=1,.train=1,
 .flags=R2_UI_CONTEXT_RC|R2_UI_CONTEXT_LIVE};
static bool inject_metadata;
static struct r2_ui_model owned(uint64_t now){
 return(struct r2_ui_model){.unit="TC-01",.sampled_ms=now,.platform_ready=true,.profile_supported=true,
 .profile_rc=true,.boundary_clock=true,.ble_active=true,.scope_active=true,
 .role=R2_UI_FOLLOWER,.authenticated_gm=true,.ble_time_selected=true,
 .ble_valid_until_us=(now+5000)*1000,.clock={.valid=true,.utc_us=1793972200000000LL+(int64_t)now*1000},
 .precision={.authorized=true,.locked=true,.map_valid=true}};
}
void r2_service_snapshot(struct r2_service_status *s){*s=owner;}
void r2_service_ui_details(struct r2_ui_model *m,const struct r2_service_status *s){
 snprintf(m->root_name,25,"%s",s->peer_names[1][0]?s->peer_names[1]:"--");
 snprintf(m->upstream_name,25,"%s",s->peer_names[1][0]?s->peer_names[1]:"--");
 m->coordinator_name[0]=0;
}
bool r2_capture_now_at(struct r2_capture *c,uint64_t *at){(void)c;(void)at;return false;}
void r2_service_rf_clock(struct r2_rf_clock_sample *s,struct r2_capture c,uint64_t at){(void)s;(void)c;(void)at;}
void r2_lora_rf_clock(struct r2_rf_clock_sample *s,struct r2_capture c,uint64_t at){(void)s;(void)c;(void)at;}
static void ui_schedule_atomic(atomic_t *v,int value){(void)v;(void)value;}
static void ui_schedule_lock(void){}
static void ui_schedule_unlock(void){}
static void ui_schedule_yield(void){}
static void ui_schedule_frame(void){}
static void ui_schedule_poll(void){}
static void rename_cached(const char *name,uint32_t revision,unsigned hint,struct r2_ui_label_ids ids){
 struct r2_ui_model m=model;snprintf(m.root_name,25,"%s",name);snprintf(m.upstream_name,25,"%s",name);
 snprintf(owner.peer_names[1],25,"%s",name);cache_details(&m,&owner,revision,hint,ids);
}
static void cache_holds(struct r2_ui_holds *h,const struct r2_ui_model *m,uint64_t now){
 r2_ui_holds_update(h,m,now);
 if(inject_metadata){inject_metadata=false;rename_cached("FreshRename",model_context,model_hint,model_labels);}
}
static void publish(uint64_t now,struct r2_ui_label_ids ids){fake_now=now;struct r2_ui_model m=owned(now);r2_ui_publish_owned(&m,0,&ids);}
int main(void){
 owner=(struct r2_service_status){.profile_incarnation=1,.root=1,.root_boot=73,.source_epoch=1,
  .precision={.train=1},.profile_rc=true,.active=true,.follower_clock_valid=true,.identities={1,4},.self=0};
 r2_ui_context_update(&ctx);r2_ui_source_hint(R2_UI_HINT_KNOWN|R2_UI_HINT_BLE_VALID);
 struct r2_ui_label_ids ids={4,4,0};publish(1,ids);assert(!strcmp(model.root_name,"TC-04"));
 uint64_t serial=model_serial,sampled=model.sampled_ms,until=model.ble_valid_until_us;
 struct r2_ui_model draw=model;snprintf(owner.peer_names[1],25,"LivingRoom");
 assert(optional_details(&draw,model_context,model_hint,0));
 assert(!strcmp(model.root_name,"LivingRoom")&&model_serial==serial&&model.sampled_ms==sampled&&model.ble_valid_until_us==until);
 /* Busy owner/render failures do not own or clear the descriptive cache. */
 uint8_t pixels[R2_UI_FRAME_BYTES],header[80*16];struct r2_ui_status display={0};
 r2_ui_render(pixels,&model,&display,fake_now);memcpy(header,pixels,sizeof header);
 owner.supervision.state=2;status.error=-ETIMEDOUT;
 for(unsigned now=21;now<2021;now+=20){publish(now,ids);draw=model;assert(!optional_details(&draw,model_context,model_hint,0));assert(!strcmp(model.root_name,"LivingRoom"));
  if(now==21||now==541||now==1021||now==2001){r2_ui_render(pixels,&draw,&display,now);assert(!memcmp(header,pixels,sizeof header));}
  assert(status.error==-ETIMEDOUT);
 }
 /* Boot, source epoch and expired permission are not name leases. */
 ctx.boot=74;ctx.source_epoch=2;r2_ui_context_update(&ctx);owner.root_boot=74;owner.source_epoch=2;
 r2_ui_source_hint(R2_UI_HINT_KNOWN);publish(2041,ids);assert(!strcmp(model.root_name,"LivingRoom"));
 publish(2061,(struct r2_ui_label_ids){0});assert(!strcmp(model.root_name,"LivingRoom"));
 owner.supervision.state=0;owner.peer_names[1][0]=0;draw=model;
 assert(optional_details(&draw,model_context,model_hint,0));assert(!strcmp(model.root_name,"LivingRoom"));
 /* A worker name update between main's first copy and final commit wins,
  * while the mandatory timing publication still completes exactly once. */
 serial=model_serial;inject_metadata=true;publish(2081,ids);
 assert(model_serial==serial+1&&model.sampled_ms==2081&&!strcmp(model.root_name,"FreshRename"));
 uint32_t old_revision=model_context;unsigned old_hint=model_hint;struct r2_ui_label_ids old_ids=model_labels;
 ctx.root=2;ctx.boot=438;r2_ui_context_update(&ctx);publish(2101,(struct r2_ui_label_ids){2,2,0});
 assert(!strcmp(model.root_name,"TC-02")&&!strcmp(model.upstream_name,"TC-02"));
 r2_ui_render(pixels,&model,&display,fake_now);assert(memcmp(header,pixels,sizeof header));
 ctx.root=4;ctx.boot=75;r2_ui_context_update(&ctx);publish(2121,ids);
 rename_cached("StaleABA",old_revision,old_hint,old_ids);assert(!strcmp(model.root_name,"TC-04"));
 /* A new actual relay parent is keyed separately from the same GM root. */
 publish(2141,(struct r2_ui_label_ids){4,3,0});old_ids=model_labels;old_revision=model_context;old_hint=model_hint;
 publish(2161,(struct r2_ui_label_ids){4,5,0});rename_cached("OldParent",old_revision,old_hint,old_ids);
 assert(!strcmp(model.upstream_name,"TC-05"));
 /* LoRa descriptors are emitter-keyed and independent of BLE root changes. */
 fake_now=2181;struct r2_ui_model lora=owned(fake_now);lora.upstream_is_lora=true;snprintf(lora.upstream_name,25,"GardenTX");
 r2_ui_publish_owned(&lora,3,&(struct r2_ui_label_ids){4,3,0});assert(!strcmp(model.upstream_name,"GardenTX"));
 ctx.root=2;r2_ui_context_update(&ctx);fake_now=2201;lora.sampled_ms=fake_now;lora.upstream_name[0]=0;
 r2_ui_publish_owned(&lora,3,&(struct r2_ui_label_ids){2,3,0});assert(!strcmp(model.upstream_name,"GardenTX")&&!strcmp(model.root_name,"TC-02"));
 fake_now=2221;lora.sampled_ms=fake_now;snprintf(lora.upstream_name,25,"NewGardenTX");r2_ui_publish_owned(&lora,3,&(struct r2_ui_label_ids){2,3,0});assert(!strcmp(model.upstream_name,"NewGardenTX"));
 fake_now=2241;lora.sampled_ms=fake_now;lora.upstream_name[0]=0;r2_ui_publish_owned(&lora,6,&(struct r2_ui_label_ids){2,6,0});assert(!strcmp(model.upstream_name,"TC-06"));
 publish(2261,(struct r2_ui_label_ids){2,2,0});assert(!strcmp(model.upstream_name,"TC-02"));
 puts("PASS durable factory-keyed names: busy/boot/epoch/permission/error-independent, metadata race, unknown retention, source/parent/ABA replacement, independent LoRa namespace");
}
