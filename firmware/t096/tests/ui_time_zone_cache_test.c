/* MIT. Jonathan Yantis, 2026. Actual RX, auth, UI publication and calendar. */
#define main existing_owner_main
#include "lora_ram_fixture.h"
#undef main
#include "ui.h"
#include "time_zone_selector.h"
#include "gm7_owner.h"
#include "gm7_bridge.h"
/* Calendar reads must not run inside the short UI publication IRQ fence. */
static bool checked_offset(uint16_t id,uint64_t utc,int16_t *offset){assert(!lock_depth);return r2_tz_db_offset(id,utc,offset);}
#define r2_tz_db_offset checked_offset
#include "../src/ui_render.c"
#undef r2_tz_db_offset
static struct r2_ui_model model;
static struct r2_ui_status status;
static atomic_t stop_requested;
#include R2_UI_PUBLISH_BODY
static struct gm7_owner coarse_owner;
static struct r2_tz_lease coarse_tz;
#define owner coarse_owner
#include R2_GM_TZ_BODY
#undef owner
static const uint64_t transition=UINT64_C(1793516400000000);
static uint8_t pixels[R2_UI_FRAME_BYTES];
static void reset_ui(void){memset(&model,0,sizeof model);memset(&status,0,sizeof status);stop_requested=0;}
static void show(const char *zone,int16_t offset,unsigned flags){
 int64_t utc,local;bool coarse;assert(r2_ui_display_time(&model,model.sampled_ms,&utc,&coarse));
 assert(!strcmp(r2_ui_clock_zone(&model,model.sampled_ms,utc,&local),zone));
 uint8_t actual;struct r2_tz value=r2_ui_display_zone(&model,model.sampled_ms,utc,&actual);
 assert(value.offset_min==offset&&(actual&flags)==flags);
 assert(local==utc+(int64_t)offset*60000000);
 r2_ui_render(pixels,&model,&status,model.sampled_ms);
}
static struct r2_ui_model ble_model(uint64_t now,uint64_t utc){
 return(struct r2_ui_model){.sampled_ms=now/1000,.platform_ready=true,.role=R2_UI_FOLLOWER,
  .scope_active=true,.ble_time_selected=true,.ble_valid_until_us=now+2000000,
  .clock={.valid=true,.utc_us=(int64_t)utc}};
}
static void cold_ble(void){
 reset_ui();struct r2_group_config g=test_group;
 assert(!gm7_owner_init(&coarse_owner,&g,81,97,true,1000000));
 struct r2_ui_model m=ble_model(1000000,transition-1000000);r2_ui_publish(&m);
 assert(!model.display_tz.id&&!m.clock.pps_present&&!m.clock.source_available);
 struct gm7_packet p={.group=g.id,.key_epoch=g.epoch,.kind=GM7_ANNOUNCE,.flags=GM7_ELIGIBLE};
 p.body.control=(struct gm7_control){.sender=11,.boot=22,.sequence=1,.sent_mono_us=1000000,
  .sent_utc_us=transition-1000000,.max_age_us=750000,.source_valid_us=1000000,
  .source_gen=1,.capture_gen=1,.train_gen=1,.clock_class=1,.birth_us=transition-10000000,
  .founder=11,.term=1,.active=11,.active_boot=22,.timing_epoch=9,.tz={2,-300}};
 uint8_t ad[GM7_TZ_MAX_AD_BYTES];struct gm7_packet decoded;
 assert(!gm7_ad_encode(ad,sizeof ad,&p,&g));
 assert(!gm7_ad_decode(&decoded,ad,sizeof ad,&g));
 assert(!gm7_owner_receive(&coarse_owner,ad,sizeof ad,NULL,1000000,1000000));
 struct gm7_source source={0};struct gm7_preparation prep={0};
 gm7_owner_update(&coarse_owner,&source,&prep,1000000);
 assert(coarse_owner.scope.active==11&&coarse_owner.scope.boot==22&&!coarse_owner.serving);
 observe_coarse_tz(&decoded,0,1000000);
 assert(coarse_tz.value.id==2&&coarse_tz.until_us==1750000);
 struct r2_tz_lease original=coarse_tz;
 m.tz=r2_tz_current(&coarse_tz,1000000);m.tz_until_us=coarse_tz.until_us;
 m.clock.valid=false;r2_ui_publish(&m);assert(model.display_tz.id==2);
 m.clock.valid=true;r2_ui_publish(&m);assert(model.display_tz.id==2);
 show("CDT",-300,R2_UI_TZ_NETWORK|R2_UI_TZ_CALENDAR|R2_UI_TZ_LOCAL_VALID);
 /* Actual duplicate/authentication failures cannot renew/replace metadata. */
 int rc=gm7_owner_receive(&coarse_owner,ad,sizeof ad,NULL,1100000,1100000);
 assert(rc);observe_coarse_tz(&decoded,rc,1100000);
 ad[sizeof ad-1]^=1;rc=gm7_owner_receive(&coarse_owner,ad,sizeof ad,NULL,1200000,1200000);
 assert(rc);observe_coarse_tz(&decoded,rc,1200000);
 assert(!memcmp(&original,&coarse_tz,sizeof original));
 /* Precision stays selected across real coarse expiry and a DST boundary. */
 m=ble_model(2000000,transition);m.tz=r2_tz_current(&coarse_tz,2000000);m.tz_until_us=original.until_us;
 assert(!m.tz.id);r2_ui_publish(&m);
 show("CST",-360,R2_UI_TZ_NETWORK|R2_UI_TZ_CACHED|R2_UI_TZ_CALENDAR);
 assert(!memcmp(&original,&coarse_tz,sizeof original));
 /* Explicit unknown is no new zone; a fresh valid known pair replaces it. */
 m.tz=r2_tz_unknown();m.tz_until_us=3000000;r2_ui_publish(&m);assert(model.display_tz.id==2);
 m.tz=(struct r2_tz){65534,345};r2_ui_publish(&m);
 show("LOC",345,R2_UI_TZ_NETWORK|R2_UI_TZ_FOREIGN|R2_UI_TZ_LOCAL_VALID);
 m=ble_model(4000000,transition+2000000);r2_ui_publish(&m);
 show("LOC",345,R2_UI_TZ_CACHED|R2_UI_TZ_FOREIGN);
 m.tz=(struct r2_tz){2,-300};m.tz_until_us=5000000;r2_ui_publish(&m);
 /* Cached identity survives calendar expiry, but conversion must fail. */
 m=ble_model(5000000,UINT64_C(1956528000000000));r2_ui_publish(&m);
 int64_t local;uint8_t flags;struct r2_tz zone=r2_ui_display_zone(&model,5000,m.clock.utc_us,&flags);
 assert(zone.id==2&&(flags&R2_UI_TZ_CALENDAR)&&!(flags&R2_UI_TZ_LOCAL_VALID));
 assert(!strcmp(r2_ui_clock_zone(&model,5000,m.clock.utc_us,&local),"UTC")&&local==m.clock.utc_us);
 assert(model.display_tz.id==2);
 puts("PASS cold GM standby without GNSS: actual authenticated ANN/replay owner -> coarse observer -> UI cache; original lease, DST, foreign uncertainty and calendar expiry");
}
static void pair(struct lp_scope *scope,uint8_t context[16],uint64_t slot){
 struct lp_packet p={.kind=LP_META,.schedule=LP_SLOW,.scope=*scope,.serial=1,.cursor=0,.time_valid=true,.has_pair=true,.utc_ns=(transition-1000000+slot)*1000,.tz={2,-300}};
 p.delay_10ms=(uint16_t)((LP_SYNC_OFFSET_US-(lp_duration(p.kind)-10000)+9999)/10000);
 packet(&p,context,slot+lp_duration(p.kind)-10000,slot+lp_duration(p.kind)-9000);
 now_us=slot+LP_SYNC_OFFSET_US-100000;step();
 p=(struct lp_packet){.kind=LP_SYNC,.schedule=LP_SLOW,.cursor=0};
 packet(&p,context,slot+LP_SYNC_OFFSET_US+75000,slot+LP_SYNC_OFFSET_US+223000);
 p.kind=LP_FOLLOW;p.utc_ns=(transition-1000000+slot+LP_SYNC_OFFSET_US)*1000;
 packet(&p,context,slot+LP_SYNC_OFFSET_US+560000,slot+LP_SYNC_OFFSET_US+745000);
}
static struct r2_ui_model lora_model(void){
 return(struct r2_ui_model){.sampled_ms=now_us/1000,.platform_ready=true,
  .ble_rx_supported=true,.ble_rx_requested=false,.lora=published};
}
static void rx_and_loss(void){
 init(false);step();reset_ui();
 struct lp_scope scope={.kind=1,.emitter=1,.boot=99,.root=1,.root_boot=101,.epoch=5,.stream=1,.seed=0x51a7c003};
 uint8_t context[16];assert(!lp_context(context,&scope,LP_SLOW,&test_group));
 pair(&scope,context,now_us+1000000);
 assert(role.rx.display_acquiring&&published.utc_us&&published.tz.id==2&&published.tz_observed);
 assert(!role.rx.bounded&&!role.rx.tz_deadline_us&&!role.rx.meta_deadline_us);
 assert(published.tz_until_us==role.rx.observed_deadline_us);
 struct r2_ui_model m=lora_model();r2_ui_publish(&m);
 show("CST",-360,R2_UI_TZ_LORA|R2_UI_TZ_OBSERVED|R2_UI_TZ_CALENDAR);
 uint64_t original=role.rx.observed_deadline_us;
 /* A corrupt later META cannot replace the accepted presentation hint. */
 struct lp_packet bad={.kind=LP_META,.schedule=LP_SLOW,.scope=scope,.serial=2,.cursor=2,
  .time_valid=true,.utc_ns=(transition+now_us)*1000,.tz={65534,345}};
 assert(!tune(R2_LORA_DTS,0,true));now_us+=2000000;
 queued=(struct r2_lora_hw_event){.tick=(now_us-1000)*16,.capture_epoch=7,.generation=generation,
  .kind=R2_LORA_HW_RX_DONE,.length=(uint8_t)lp_size(LP_META)};
 assert(!lp_encode(queued.frame,sizeof queued.frame,&bad,context,&test_group));
 queued.frame[queued.length-1]^=1;have_event=true;step();
 assert(role.rx.tz.id==2&&role.rx.serial==1&&role.rx.observed_deadline_us==original);
 m=lora_model();r2_ui_publish(&m);assert(model.display_tz.id==2);
 now_us=original+1;step();assert(!published.tz.id&&published.display_acquiring&&published.utc_us);
 m=lora_model();r2_ui_publish(&m);
 show("CST",-360,R2_UI_TZ_LORA|R2_UI_TZ_OBSERVED|R2_UI_TZ_CACHED);
 assert(role.rx.observed_deadline_us==original&&!role.rx.bounded&&!role.rx.tz_deadline_us);
 /* A normal BLE -> LoRa HOLD handover preserves the learned network zone. */
 reset_ui();struct r2_ui_model b=ble_model(now_us,transition+now_us);b.tz=(struct r2_tz){2,-360};b.tz_until_us=now_us+1000;r2_ui_publish(&b);
 m=lora_model();m.lora.utc_us=transition+now_us;m.lora.tz=r2_tz_unknown();r2_ui_publish(&m);
 assert(model.display_tz.id==2&&model.display_tz_source==R2_UI_TZ_NETWORK);
 show("CST",-360,R2_UI_TZ_CACHED|R2_UI_TZ_NETWORK);
 /* Total timing loss hides conversion, but boot-lifetime learned zone stays. */
 m.lora.utc_us=0;m.lora.display_acquiring=false;m.lora.display_held=false;r2_ui_publish(&m);
 assert(model.display_tz.id==2);
 m=lora_model();r2_ui_publish(&m);assert(model.display_tz.id==2);
 /* Fresh accepted observation can replace it, with no lease or source grant. */
 m.lora.tz=(struct r2_tz){2,-360};m.lora.tz_until_us=now_us+1000000;m.lora.tz_observed=true;r2_ui_publish(&m);assert(model.display_tz.id==2);
 struct r2_ui_model rollback=m;--rollback.sampled_ms;r2_ui_publish(&rollback);assert(model.display_tz.id==2);
 r2_ui_publish(&m);assert(model.display_tz.id==2);
 /* A valid UTC correction is still usable time; zone/DST follows it. */
 rollback=m;rollback.lora.tz=r2_tz_unknown();rollback.lora.utc_us=transition-1000000;r2_ui_publish(&rollback);assert(model.display_tz.id==2);
 show("CDT",-300,R2_UI_TZ_CACHED|R2_UI_TZ_CALENDAR);
 r2_ui_publish(&m);assert(model.display_tz.id==2);r2_ui_stop();assert(model.display_tz.id==2);
 r2_ui_publish(&m);assert(model.display_tz.id==2);
 puts("PASS actual authenticated LoRa META/SYNC/FOLLOW -> observational TZ view -> UI acquisition/cache; expiry/HOLD/fallback, no authority renewal, boot-lifetime retention across loss/monotonic rollback/stop and valid UTC correction");
}
static void sync_grace(void){
 reset_ui();struct r2_ui_model m=ble_model(1000000,transition);m.tz=(struct r2_tz){2,-360};m.tz_until_us=1100000;r2_ui_publish(&m);
 assert(model.display_tz.id==2);
 status.sync_display=(struct r2_ui_sync_display){.utc_us=(int64_t)transition,.at_ms=1000,.loss_ms=2000};
 m=(struct r2_ui_model){.sampled_ms=2200,.platform_ready=true,.profile_supported=true,
  .profile_rc=true,.boundary_clock=true,.ble_active=true};
 r2_ui_publish(&m);assert(model.display_tz.id==2&&status.sync_display.loss_ms==2000);
 m.sampled_ms=2499;r2_ui_publish(&m);assert(model.display_tz.id==2&&status.sync_display.loss_ms==2000);
 m.sampled_ms=2500;r2_ui_publish(&m);assert(model.display_tz.id==2&&status.sync_display.loss_ms==2000);
 puts("PASS UI UTC grace still expires at original500ms; learned zone remains in boot RAM");
}
int main(void){cold_ble();rx_and_loss();sync_grace();printf("RAM ui_model=%zu ui_status=%zu lora_view=%zu\n",sizeof model,sizeof status,sizeof published);return 0;}
