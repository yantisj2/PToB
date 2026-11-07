/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Production raster parity and ordinary-return cancellation, fake ownership. */
#include "ui.h"
#ifdef R2_TZ_AUTO
#include "time_zone_config.h"
bool baseline_r2_tz_db_offset(uint16_t,uint64_t,int16_t *);
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
void baseline_r2_ui_render(uint8_t *,const struct r2_ui_model *,const struct r2_ui_status *,uint64_t);
static uint8_t before[R2_UI_FRAME_BYTES],after[R2_UI_FRAME_BYTES],at_abort[R2_UI_FRAME_BYTES];
static unsigned callbacks,abort_at;static bool nested_checked;
static struct r2_ui_model m;static struct r2_ui_status s;static uint64_t now=10000;
static bool checkpoint(void *context,enum r2_ui_render_stage stage){
 if(stage!=R2_UI_RENDER_PIXELS)return true;
 assert(context==&m);++callbacks;
 if(!nested_checked){nested_checked=true;assert(!r2_ui_render_try(after,&m,&s,now,NULL,NULL));}
 if(abort_at&&callbacks==abort_at){memcpy(at_abort,after,sizeof at_abort);return false;}
 return true;
}
int main(void){
 for(unsigned scene=0;scene<200;scene++){
  m=(struct r2_ui_model){.sampled_ms=now,.unit="TC-01",.role=scene%3,.platform_ready=true,
   .profile_supported=true,.profile_rc=(scene&1)!=0,.scope_active=scene%4!=0,
   .authenticated_gm=true,.gm_source_fresh=scene%3==0,.follower_pps_valid=true,
   .ble_active=true,.ble_time_selected=true,.ble_valid_until_us=(now+1000)*1000,
   .clock={.valid=true,.utc_us=1792340000000000LL,.pps_present=true,.pps_age_us=scene%3==0?0:scene%3==1?249999:250000},
   .precision={.map_valid=true,.authorized=true,.locked=true,.rms_valid=true,.rms_ns=123}};
  memcpy(m.root_name,"TC-04",6);memcpy(m.upstream_name,"TC-04",6);memcpy(m.coordinator_name,"TC-01",6);
  s=(struct r2_ui_status){.page=scene%R2_UI_PAGES,.ready=true,.help=scene%17==0,.screensaver=scene%19==0,.screensaver_position=scene%3};
  if(scene%23==0)s.menu.open=true;
  if(scene%7==0){m.clock.valid=false;m.coarse_valid=false;m.scope_active=false;}
#ifdef R2_LORA
  if(scene%5==0){m.lora.requested=m.lora.active=m.lora.observer_available=m.lora.metadata_cached=true;
   m.lora.state=RC8_EXPIRED;m.lora.display_held=true;m.lora.display_until_us=UINT64_MAX;
   m.lora.sampled_us=now*1000;m.lora.utc_us=1792340000000000ULL;}
#endif
  memset(before,0x5a,sizeof before);memset(after,0x5a,sizeof after);
  baseline_r2_ui_render(before,&m,&s,now);r2_ui_render(after,&m,&s,now);
  assert(!memcmp(before,after,sizeof before));
  callbacks=abort_at=0;nested_checked=false;memset(after,0x5a,sizeof after);
  assert(r2_ui_render_try(after,&m,&s,now,checkpoint,&m));
  assert(callbacks>=160&&nested_checked&&!memcmp(before,after,sizeof before));
  unsigned total=callbacks;
  if(!s.menu.open&&!s.help&&s.page==0&&(!s.screensaver||r2_ui_saver_allowed(&m,now))){
   struct r2_ui_zone_cache zone={0};struct r2_ui_tile_view view;
   nested_checked=true;
   if(!s.screensaver){assert(r2_ui_tile_render(after,&m,&s,now,R2_UI_TILE_TIME,&zone,&view,checkpoint,&m));assert(!memcmp(before+16*80,after+16*80,32*80));}
   assert(r2_ui_tile_render(after,&m,&s,now,R2_UI_TILE_PPS,&zone,&view,checkpoint,&m));
   unsigned y=s.screensaver?r2_ui_saver_top(s.screensaver_position)+2:19;
   for(unsigned row=y;row<y+9;row++)assert(!memcmp(before+row*80+66,after+row*80+66,9));
  }
  for(unsigned slot=0;slot<16;slot++){
   unsigned stop=slot<4?slot+1:1+(total-1)*(slot-3)/12;
   callbacks=0;abort_at=stop;nested_checked=true;memset(after,0x5a,sizeof after);
   assert(!r2_ui_render_try(after,&m,&s,now,checkpoint,&m));assert(callbacks==stop);
   /* This includes late nested primitives, beyond the initial full clear.
    * Nothing may modify scratch after the callback refuses continuation. */
   assert(!memcmp(after,at_abort,sizeof after));
   r2_ui_render(after,&m,&s,now);assert(!memcmp(before,after,sizeof before));
   abort_at=0;callbacks=0;nested_checked=true;
   assert(r2_ui_render_try(after,&m,&s,now,checkpoint,&m));
   assert(!memcmp(before,after,sizeof before));
  }
 }
 /* Actual selected-clock histories, not renderer-derived synthetic pulses.
  * Every tile uses the full renderer's policy and a fresh shared calendar span. */
 const uint64_t times[]={1000,1249,1250,1499,1500,1750,1999,2000,2499,2500};
 const int64_t ages[]={-1,0,249999,250000,1499999,1500000};
 unsigned history=0,lora_primary=0;
 for(unsigned state=0;state<5;state++)for(unsigned rc=0;rc<2;rc++)
 for(unsigned t=0;t<sizeof times/sizeof times[0];t++)for(unsigned age=0;age<sizeof ages/sizeof ages[0];age++){
  now=times[t];m=(struct r2_ui_model){.sampled_ms=now,.unit="TEST",.platform_ready=true,
   .profile_rc=rc,.boundary_clock=rc,.role=R2_UI_FOLLOWER,.scope_active=true,.authenticated_gm=true,
   .ble_active=true,.ble_time_selected=true,.ble_valid_until_us=(now+1000)*1000,
   .face_lock_until_us=(now+1000)*1000,.follower_pps_valid=true,
   .precision={.authorized=true,.map_valid=true,.locked=true,.rms_valid=true,.rms_ns=50},
   .clock={.valid=true,.source_available=true,.fix_active=true,.pps_present=true,.pps_edges=17,
    .pps_age_us=ages[age],.utc_us=INT64_C(1793516400000000)+(int64_t)now*1000}};
  s=(struct r2_ui_status){.page=0,.ready=true};
  if(state==1){
   m.ble_valid_until_us=m.face_lock_until_us=1500000;
   if(now>=1500){m.clock.valid=false;m.scope_active=m.authenticated_gm=m.precision.map_valid=false;m.sync_attempt=true;}
   s.sync_display=(struct r2_ui_sync_display){.utc_us=INT64_C(1793516401000000),.at_ms=1000,.loss_ms=1500};
  }else if(state==2){m.role=R2_UI_GM;m.gm_source_fresh=true;m.ble_time_selected=false;}
  else if(state==3){m.ble_local_holdover=rc;m.ble_valid_until_us=1500000;}
#ifdef R2_LORA
  else if(state==4){
   m.profile_rc=m.boundary_clock=true;m.ble_valid_until_us=1500000;
   m.lora.requested=m.lora.active=m.lora.metadata_cached=m.lora.observer_available=true;
   m.lora.choice=RC8_SELECT_LORA;m.lora.state=now<1500?RC8_AVAILABLE:RC8_EXPIRED;
   m.lora.time_valid=now<1500;m.lora.display_held=now>=1500;
   m.lora.sampled_us=1000000;m.lora.until_us=1500000;m.lora.display_until_us=2000000;
   m.lora.utc_us=INT64_C(1793516401000000);m.lora.context_known=m.lora.quality_known=true;
  }
  if(r2_ui_lora_primary(&m,now))++lora_primary;
#endif
#ifdef R2_TZ_WIRE
  m.tz=(struct r2_tz){2,-300};m.tz_until_us=1500000;
  m.display_tz=m.tz;m.display_tz_source=R2_UI_TZ_NETWORK|R2_UI_TZ_CACHED;
#endif
  struct r2_ui_zone_cache zone={0};r2_ui_zone_cache_update(&zone,&m,now);
  memset(before,0x5a,sizeof before);memset(after,0x5a,sizeof after);baseline_r2_ui_render(before,&m,&s,now);
  abort_at=callbacks=0;nested_checked=true;struct r2_ui_tile_view view;
  assert(r2_ui_tile_render(after,&m,&s,now,R2_UI_TILE_TIME,&zone,&view,checkpoint,&m));
  assert(!memcmp(before+16*80,after+16*80,32*80));
  int64_t utc=0;bool coarse=false,valid=r2_ui_display_time(&m,now,&utc,&coarse);
  bool grace=r2_ui_sync_display_time(&s.sync_display,&m,now,&utc);if(grace){valid=coarse=true;}
  assert(view.valid==valid&&view.coarse==coarse&&(!valid||view.utc_us==utc));
  assert(view.analog==(valid&&(r2_ui_face_locked(&m,now)||grace)));
  assert(view.pulse==r2_ui_local_pulse(&m,now)&&view.edge==17);++history;
 }
#ifdef R2_LORA
 assert(lora_primary);
#else
 (void)lora_primary;
#endif
#ifdef R2_TZ_AUTO
 unsigned spans=0;
 for(uint64_t utc=UINT64_C(1609459200000000);utc<UINT64_C(1956528000000000);utc+=UINT64_C(1234567000000)){
  int16_t offset,reference;uint64_t first,until;
  bool valid=r2_tz_db_offset_span(2,utc,&offset,&first,&until);
  assert(valid==baseline_r2_tz_db_offset(2,utc,&reference));
  if(valid){assert(offset==reference&&first<=utc&&utc<until);++spans;
   assert(baseline_r2_tz_db_offset(2,first,&reference)&&reference==offset);
   assert(baseline_r2_tz_db_offset(2,until-1,&reference)&&reference==offset);}
 }
 assert(spans);
#endif
 /* Loss strips cover the union of earlier dial positions. TIME owns its band;
  * an aborted scratch strip is retried before any driver can publish it. */
 for(unsigned position=0;position<3;position++){
  now=1000;m=(struct r2_ui_model){.sampled_ms=now,.unit="TEST",.platform_ready=true,
   .role=R2_UI_FOLLOWER,.scope_active=true,.authenticated_gm=true,.ble_active=true,
   .ble_time_selected=true,.ble_valid_until_us=5000000,.face_lock_until_us=5000000,.face_rms_ns=50,
   .follower_pps_valid=true,.precision={.authorized=true,.map_valid=true,.locked=true,.rms_valid=true,.rms_ns=50},
   .clock={.valid=true,.source_available=true,.fix_active=true,.pps_present=true,.pps_age_us=0,.utc_us=INT64_C(1793516400000000)}};
  s=(struct r2_ui_status){.ready=true,.screensaver=true,.screensaver_position=position};
  assert(r2_ui_saver_allowed(&m,now));baseline_r2_ui_render(after,&m,&s,now);
  m.clock.valid=m.clock.source_available=m.scope_active=m.authenticated_gm=m.precision.map_valid=false;
  m.ble_valid_until_us=m.face_lock_until_us=0;m.sync_attempt=true;s.screensaver=false;
  struct r2_ui_zone_cache zone={0};struct r2_ui_tile_view view;abort_at=callbacks=0;nested_checked=true;
  memset(before,0xa5,sizeof before);
  assert(r2_ui_tile_render(before,&m,&s,now,R2_UI_TILE_TIME,&zone,&view,NULL,NULL));
  assert(r2_ui_tile_render(after,&m,&s,now,R2_UI_TILE_TIME,&zone,&view,checkpoint,&m));
  for(unsigned band=0;band<10;band++)if(band!=1&&band!=2)
   assert(r2_ui_tile_render_rect(before,&m,&s,now,R2_UI_TILE_LOSS,&zone,&view,NULL,NULL,(struct r2_ui_rect){0,band*16,80,16}));
  const unsigned order[]={3,0,4,5,6,7,8,9};
  for(unsigned i=0;i<8;i++){
   struct r2_ui_rect r={0,order[i]*16,80,16};
   if(i==2){callbacks=0;abort_at=1;
    assert(!r2_ui_tile_render_rect(after,&m,&s,now,R2_UI_TILE_LOSS,&zone,&view,checkpoint,&m,r));
    assert(!memcmp(after,at_abort,sizeof after));abort_at=0;}
   assert(r2_ui_tile_render_rect(after,&m,&s,now,R2_UI_TILE_LOSS,&zone,&view,checkpoint,&m,r));
  }
  assert(!memcmp(before,after,sizeof before));
 }
 puts("PASS200 scenes: old/full/cooperative pixel parity,3200 distributed80-pixel aborts, cleanup/retry and nonreentrancy");
 printf("PASS%u critical time/PPS policy histories: BLE grace/expiry/local HOLD, GM and independent LoRa HOLD; coherent single time band\n",history);
 puts("PASS sequential loss strips erase all earlier dial positions, including aborted scratch retries");
 return 0;
}
