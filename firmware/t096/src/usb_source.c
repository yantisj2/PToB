/* SPDX-License-Identifier: Apache-2.0
 * Two immutable publication intervals. A later map cannot authorize an earlier
 * SOF. Source permission, capture epoch and publication order are independent
 * of USB connectivity. Existing USB worker owns serialization and EP0 buffer.
 */
#include "usb_source.h"
#include "usb_sof_probe.h"
#include "rc_unicast.h"
#include "capture.h"
#include "app.h"
#include "health.h"
#include <zephyr/usb/usb_device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
struct published_map {struct r2_uc_clock clock;uint64_t lo,hi;bool set;};
static struct published_map maps[2], estimate;
static struct k_spinlock map_lock;
static uint64_t silicon,boot,transition_tick,transition_epoch;
static uint32_t sequence,overwrites,revision;
/* Saturation fails closed. Never let an ABA wrap validate an old request. */
static void changed(void){if(revision<UINT32_MAX)++revision;}
static bool busy,permission,estimate_current;
static uint8_t wire[256];
static bool same_scope(const struct r2_uc_clock *a,const struct r2_uc_clock *b){
 return a->capture_epoch==b->capture_epoch&&a->origin.root==b->origin.root&&
  a->origin.boot==b->origin.boot&&a->origin.epoch==b->origin.epoch&&
  a->origin.utc_offset==b->origin.utc_offset&&!memcmp(a->scope,b->scope,16);
}
void r2_usb_source_init(const struct r2_identity *id){
 if(!id||id->result)return;
 boot=id->boot;
 for(unsigned i=0;i<16;i++)silicon=(silicon<<4)|(id->silicon[i]<='9'?(unsigned)(id->silicon[i]-'0'):(unsigned)(id->silicon[i]-'a'+10));
}
void r2_usb_source_begin(uint64_t activation,uint64_t capture_epoch){
 k_spinlock_key_t key=k_spin_lock(&map_lock);changed();
 if(!busy){transition_tick=activation;transition_epoch=capture_epoch;}busy=true;
 k_spin_unlock(&map_lock,key);
}
void r2_usb_source_prepare(uint64_t activation,uint64_t capture_epoch,
                           const struct r2_uc_clock *previous){
 k_spinlock_key_t key=k_spin_lock(&map_lock);
 const struct r2_uc_clock *published=&maps[0].clock;
 bool compatible=previous&&previous->valid&&permission&&!busy&&maps[0].set&&
  capture_epoch==published->capture_epoch&&same_scope(previous,published)&&
  activation<published->until_tick&&
  previous->map.generation==published->map.generation;
 /* Preparation does not change the previously committed observation contract.
  * Its map, source envelope and session deadlines remain immutable. Commit
  * closes that interval at publication; the new map is never backdated. */
 if(!compatible){changed();if(!busy){transition_tick=activation;transition_epoch=capture_epoch;}busy=true;}
 k_spin_unlock(&map_lock,key);
}
void r2_usb_source_withdraw(void){
 k_spinlock_key_t key=k_spin_lock(&map_lock);changed();permission=false;busy=false;
 memset(maps,0,sizeof maps);estimate_current=false;k_spin_unlock(&map_lock,key);
}
void r2_usb_source_publish(const struct r2_uc_clock *clock){
 struct r2_capture after;
 if(!clock||!r2_capture_now(&after)){r2_usb_source_withdraw();return;}
 k_spinlock_key_t key=k_spin_lock(&map_lock);changed();
 /* Estimate publication never refreshes observation age or original leases.
  * Keep the old segment through authority withdrawal/search, until a new map
  * is committed; capture epoch and arithmetic remain hard projection fences. */
 estimate_current=clock->estimate&&clock->session_until>after.tick;
 if(clock->estimate&&clock->map.generation&&clock->capture_epoch==after.epoch){
  if(!estimate.set||estimate.clock.map.generation!=clock->map.generation||
      estimate.clock.capture_epoch!=clock->capture_epoch||
      estimate.clock.origin.root!=clock->origin.root||
      estimate.clock.origin.boot!=clock->origin.boot||
      estimate.clock.origin.epoch!=clock->origin.epoch||
      memcmp(estimate.clock.scope,clock->scope,16))
   estimate=(struct published_map){.clock=*clock,.lo=after.tick,.hi=clock->until_tick,.set=true};
  /* Revocation can shorten, never extend, an already published session. */
  if(clock->session_until<estimate.clock.session_until)
   estimate.clock.session_until=clock->session_until;
 }
 permission=clock->valid&&clock->capture_epoch==after.epoch&&after.tick<clock->until_tick;
 if(!permission){busy=false;k_spin_unlock(&map_lock,key);return;}
 bool changed=!maps[0].set||maps[0].clock.map.generation!=clock->map.generation||
  maps[0].clock.capture_epoch!=clock->capture_epoch||maps[0].clock.origin.root!=clock->origin.root||
  maps[0].clock.origin.boot!=clock->origin.boot||maps[0].clock.origin.epoch!=clock->origin.epoch||
  memcmp(maps[0].clock.scope,clock->scope,16);
 if(changed){
  uint64_t cut=busy&&transition_epoch==after.epoch?transition_tick:after.tick;
  if(maps[1].set&&overwrites<UINT32_MAX)++overwrites;
  maps[1]=maps[0];if(maps[1].set&&cut<maps[1].hi)maps[1].hi=cut;
  maps[0]=(struct published_map){.clock=*clock,.lo=after.tick,.hi=clock->until_tick,.set=true};
  if(maps[0].lo<clock->map.effective)maps[0].lo=clock->map.effective;
 }
 /* Same generation cannot be refreshed with a later validity/map snapshot. */
 busy=false;k_spin_unlock(&map_lock,key);
}
static bool project(const struct published_map *map,uint64_t tick,uint64_t current,uint32_t epoch,int64_t *utc){
 uint64_t ns,current_ns,elapsed;
 if(!map->set||epoch!=map->clock.capture_epoch||tick<map->lo||tick>=map->hi||
    current>=map->clock.until_tick||!rc5_map_project(&map->clock.map,tick,&ns,&elapsed)||
    ns>=map->clock.origin.expires||ns>INT64_MAX||
    !rc5_map_project(&map->clock.map,current,&current_ns,&elapsed)||
    current_ns>=map->clock.origin.expires)return false;
 int64_t offset=map->clock.origin.utc_offset;
 if((offset>0&&(int64_t)ns>INT64_MAX-offset)||(offset<0&&(int64_t)ns<INT64_MIN-offset))return false;
 *utc=(int64_t)ns+offset;return *utc>0;
}
/* One192B scratch on the existing USB worker stack;
 * never snapshot all three maps onto that1KiB stack. Arithmetic, division and
 * serialization run with regular controller interrupts enabled. */
static bool snapshot_map(unsigned index,uint32_t expected,struct published_map *out){
#if defined(CONFIG_ARM)
 unsigned mask=irq_lock();r2_health_lock_enter(0x60001u,mask);
#endif
 k_spinlock_key_t key=k_spin_lock(&map_lock);
 bool ok=revision==expected&&revision!=UINT32_MAX;
 if(ok)*out=index<2?maps[index]:estimate;
 k_spin_unlock(&map_lock,key);
#if defined(CONFIG_ARM)
 r2_health_lock_leave(mask);irq_unlock(mask);
#endif
 return ok;
}
/* An older applicable map can supply UTE1 only in the currently published
 * authenticated scope. Keep its original discipline/source/session fields,
 * rather than replacing them with a newer, inapplicable generation. */
static bool snapshot_scope(uint32_t expected,const struct published_map *map){
 k_spinlock_key_t key=k_spin_lock(&map_lock);
 bool ok=revision==expected&&revision!=UINT32_MAX&&maps[0].set&&
  same_scope(&map->clock,&maps[0].clock);
 k_spin_unlock(&map_lock,key);return ok;
}
static void write_metadata(const struct published_map *metadata,uint64_t current){
 const struct r2_uc_clock *c=&metadata->clock;
 sys_put_le64(c->origin.root,wire+80);sys_put_le64(c->origin.boot,wire+88);
 sys_put_le32(c->origin.epoch,wire+96);sys_put_le32(c->map.generation,wire+100);
 uint64_t current_ns,elapsed,until=c->until_tick;
 if(rc5_map_project(&c->map,current,&current_ns,&elapsed)&&current_ns<c->origin.expires){
  uint64_t remaining=(c->origin.expires-current_ns)/1001*16;
  if(current<until&&remaining<until-current)until=current+remaining;
 }
 sys_put_le64(until,wire+104);sys_put_le64(c->origin.expires,wire+112);
 sys_put_le64(c->map.anchor_tick,wire+120);sys_put_le64(c->map.anchor_ns,wire+128);
 sys_put_le64(c->map.effective,wire+136);sys_put_le64(metadata->lo,wire+144);sys_put_le64(metadata->hi,wire+152);
 sys_put_le32((uint32_t)c->map.ppb,wire+160);sys_put_le32((uint32_t)c->map.phase_ns,wire+164);
 sys_put_le32(c->map.age_us,wire+168);sys_put_le32(c->map.bound,wire+172);
 sys_put_le32(c->map.rate_bound,wire+176);sys_put_le32(c->map.capture_bound,wire+180);
 sys_put_le64((uint64_t)c->origin.utc_offset,wire+184);sys_put_le64(c->origin.issued,wire+192);
 memcpy(wire+200,c->scope,16);sys_put_le32(c->origin.hold_us,wire+224);sys_put_le32(c->origin.hold_ns,wire+228);
}
int r2_usb_source_request(struct usb_setup_packet *setup,int32_t *length,uint8_t **data){
 if(setup->bmRequestType!=0xc1||(setup->bRequest!=0x52&&setup->bRequest!=0x53&&setup->bRequest!=0x57)||
    setup->wValue||setup->wIndex!=r2_usb_reference_number()||setup->wLength!=sizeof wire)return -ENOTSUP;
 uint32_t before_epoch=0,after_epoch=0;
 struct r2_usb_sof_snapshot raw={0};bool captured=false,sof=false;
 uint32_t age=0,first_epoch=0;
 /* A SOF can arrive between FRAMECNTR/CC3 reads. Retry that incoherent read
  * once, with interrupts enabled between the ordinary capture calls. Never
  * retry missing hardware or cross a USB epoch, and never fabricate a SOF. */
 for(unsigned attempt=0;attempt<2;attempt++){
  bool live=r2_usb_sof_scope(&before_epoch);
  if(!attempt)first_epoch=before_epoch;
  captured=live&&before_epoch==first_epoch&&r2_capture_usb_sample(&raw);
  captured=captured&&r2_usb_sof_scope(&after_epoch)&&before_epoch==after_epoch;
  age=(uint32_t)raw.current_tick-raw.cc_after;
  sof=captured&&raw.frame_before==raw.frame_after&&raw.cc_before==raw.cc_after&&
   raw.frame_after<=R2_USB_FRAME_MASK&&age<R2_USB_SOF_TICKS&&raw.current_tick>=age;
  if(sof||!captured||setup->bRequest==0x52)break;
 }
 uint64_t event=raw.current_tick;
 if(setup->bRequest==0x53||setup->bRequest==0x57){captured=sof;if(sof)event-=age;}
 bool was_busy,permitted,estimate_is_current;
 uint32_t expected,serial,overwritten;
#if defined(CONFIG_ARM)
 unsigned mask=irq_lock();r2_health_lock_enter(0x60002u,mask);
#endif
 k_spinlock_key_t key=k_spin_lock(&map_lock);
 expected=revision;was_busy=busy;permitted=permission;estimate_is_current=estimate_current;
 overwritten=overwrites;if(sequence<UINT32_MAX)++sequence;serial=sequence;
 k_spin_unlock(&map_lock,key);
#if defined(CONFIG_ARM)
 r2_health_lock_leave(mask);irq_unlock(mask);
#endif
 int64_t utc=0;uint32_t flags=0,reason=1;
 bool found=false,current_found=false,raced=false;
 struct published_map local;
 uint64_t current_source=0;
 if(captured&&!was_busy&&permitted){for(unsigned i=0;i<2;i++){
  if(!snapshot_map(i,expected,&local)){raced=true;break;}
  if(project(&local,event,raw.current_tick,raw.capture_epoch,&utc)){found=true;break;}
 }}
 if(!found&&!raced&&setup->bRequest==0x53&&captured&&!was_busy&&permitted){
  int64_t current_utc;uint64_t elapsed;
  for(unsigned i=0;i<2;i++){
   if(!snapshot_map(i,expected,&local)){raced=true;break;}
   if(project(&local,raw.current_tick,raw.current_tick,raw.capture_epoch,&current_utc)&&
      rc5_map_project(&local.clock.map,raw.current_tick,&current_source,&elapsed)){current_found=true;break;}
  }
 }
 if(found){flags=1;reason=0;}else{utc=0;if(was_busy)reason=4;else if(!captured)reason=2;else if(current_found)reason=7;}
 if(serial==UINT32_MAX){flags=0;reason=6;}
 memset(wire,0,sizeof wire);memcpy(wire,"UTS1",4);
 sys_put_le16(1,wire+4);sys_put_le16(sizeof wire,wire+6);
 sys_put_le64(silicon,wire+8);sys_put_le64(boot,wire+16);
 sys_put_le32(after_epoch,wire+24);sys_put_le32(raw.capture_epoch,wire+28);
 sys_put_le32(serial,wire+32);sys_put_le32(flags,wire+36);
 sys_put_le16(sof?raw.frame_after:UINT16_MAX,wire+40);sys_put_le16(1,wire+42);
 sys_put_le64(event,wire+48);sys_put_le64(raw.current_tick,wire+56);
 sys_put_le64((uint64_t)utc,wire+64);wire[72]=setup->bRequest==0x53?1:2;
 if(found||current_found)write_metadata(&local,raw.current_tick);
 bool extended=setup->bRequest==0x57,estimate_ok=false;
 uint64_t projected=0,elapsed_est=0,current_est=0;
 if(extended&&!raced){
  if(found&&local.clock.estimate){if(!snapshot_scope(expected,&local))raced=true;}
  else if(!snapshot_map(2,expected,&local))raced=true;
  if(!raced&&captured&&!was_busy&&local.set&&raw.capture_epoch==local.clock.capture_epoch&&
     event>=local.lo&&event>=local.clock.discipline_tick&&
     rc5_map_project(&local.clock.map,event,&projected,&elapsed_est)&&
     rc5_map_project(&local.clock.map,raw.current_tick,&current_est,&elapsed_est)&&projected<=INT64_MAX){
   int64_t offset=local.clock.origin.utc_offset;
   if(!((offset>0&&(int64_t)projected>INT64_MAX-offset)||(offset<0&&(int64_t)projected<INT64_MIN-offset))){
    int64_t value=(int64_t)projected+offset;
    if(value>0){estimate_ok=true;sys_put_le64((uint64_t)value,wire+64);}
   }
  }
 }
 if(extended){
  memcpy(wire,"UTE1",4);wire[72]=1;
  if(!estimate_ok)sys_put_le64(0,wire+64);
  uint32_t states=captured?4:0;
  if(estimate_ok){
   states|=2;
   if(estimate_is_current&&local.clock.session_until&&raw.current_tick<local.clock.session_until)states|=8;
   if(estimate_is_current&&current_est>=local.clock.origin.issued&&current_est<local.clock.origin.expires)states|=16;
   /* Read the already serialized strict identity before replacing metadata
    * with the retained estimate. No second full192B map scratch is needed. */
   if((states&24)==24&&found&&sys_get_le32(wire+100)==local.clock.map.generation&&
      sys_get_le64(wire+80)==local.clock.origin.root&&sys_get_le64(wire+88)==local.clock.origin.boot&&
      sys_get_le32(wire+96)==local.clock.origin.epoch&&!memcmp(wire+200,local.clock.scope,16))states|=1;
   write_metadata(&local,raw.current_tick);
   sys_put_le64(local.clock.discipline_tick,wire+232);sys_put_le64(local.clock.session_until,wire+240);
   sys_put_le32(event-local.clock.discipline_tick<UINT64_C(4000000)?1:2,wire+248);
  }
  if(serial==UINT32_MAX)states=0;
  sys_put_le32(states,wire+36);sys_put_le32(estimate_ok?0:captured?3:2,wire+252);
 }else if(current_found&&reason==7){
  memcpy(wire+232,"USM1",4);sys_put_le16(1,wire+236);sys_put_le16(24,wire+238);
  sys_put_le64(current_source,wire+240);sys_put_le32(1,wire+248);
 }
 sys_put_le32(overwritten,wire+216);sys_put_le32(reason,wire+220);
 /* A concurrent publish/begin/withdraw invalidates this entire packet. It
  * cannot resurrect a previous map or original authority. Next normal poll
  * will observe the new publication; host projection need not disappear. */
#if defined(CONFIG_ARM)
 mask=irq_lock();r2_health_lock_enter(0x60003u,mask);
#endif
 key=k_spin_lock(&map_lock);raced=raced||revision!=expected||revision==UINT32_MAX;k_spin_unlock(&map_lock,key);
#if defined(CONFIG_ARM)
 r2_health_lock_leave(mask);irq_unlock(mask);
#endif
 if(raced||serial==UINT32_MAX){
  memset(wire+64,0,192);sys_put_le32(extended&&captured?4:0,wire+36);
  wire[72]=extended||setup->bRequest==0x53?1:2;sys_put_le32(serial==UINT32_MAX?6:4,wire+220);
  if(extended)sys_put_le32(4,wire+252);
 }
 *data=wire;*length=sizeof wire;return 0;
}

/* A zero-data OUT marker is stamped before its status completion. Reading the
 * immutable tuple later does not widen the OUT's causal host-time bracket.
 * This is frame-identity evidence only, with no UTC/source permission. */
static uint8_t anchor[64];
static uint32_t anchor_sequence;
int r2_usb_anchor_request(struct usb_setup_packet *s,int32_t *length,uint8_t **data){
 if(s->wIndex!=r2_usb_reference_number()||!s->wValue)return -ENOTSUP;
 if(s->bmRequestType==0x41&&s->bRequest==0x54&&s->wLength==0){
  struct r2_usb_sof_snapshot r={0};uint32_t before=0,after=0;
  bool valid=r2_usb_sof_scope(&before)&&r2_capture_usb_sample(&r)&&
   r2_usb_sof_scope(&after)&&before==after;
  uint32_t age=(uint32_t)r.current_tick-r.cc_after;
  valid=valid&&r.frame_before==r.frame_after&&r.cc_before==r.cc_after&&
   r.frame_after<=R2_USB_FRAME_MASK&&age<R2_USB_SOF_TICKS&&r.current_tick>=age;
  if(anchor_sequence<UINT32_MAX)++anchor_sequence;else valid=false;
  memset(anchor,0,sizeof anchor);memcpy(anchor,"UFA1",4);
  sys_put_le16(1,anchor+4);sys_put_le16(sizeof anchor,anchor+6);
  sys_put_le64(silicon,anchor+8);sys_put_le64(boot,anchor+16);
  sys_put_le32(after,anchor+24);sys_put_le32(r.capture_epoch,anchor+28);
  sys_put_le32(anchor_sequence,anchor+32);sys_put_le32(valid?1:0,anchor+36);
  sys_put_le16(valid?r.frame_after:UINT16_MAX,anchor+40);sys_put_le16(s->wValue,anchor+42);
  sys_put_le64(valid?r.current_tick-age:0,anchor+44);sys_put_le64(r.current_tick,anchor+52);
  sys_put_le32(valid?0:2,anchor+60);*length=0;*data=NULL;return 0;
 }
 if(s->bmRequestType==0xc1&&s->bRequest==0x55&&s->wLength==sizeof anchor&&
    sys_get_le16(anchor+42)==s->wValue){
  uint32_t epoch=0;struct r2_capture current;memcpy(wire,anchor,sizeof anchor);
  if(!r2_usb_sof_scope(&epoch)||epoch!=sys_get_le32(anchor+24)||
     !r2_capture_now(&current)||current.epoch!=sys_get_le32(anchor+28)){
   sys_put_le32(0,wire+36);sys_put_le32(3,wire+60);
  }
  *length=sizeof anchor;*data=wire;return 0;
 }
 return -ENOTSUP;
}
