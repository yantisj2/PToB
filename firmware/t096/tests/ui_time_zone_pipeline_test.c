/* MIT. Jonathan Yantis2026. Actual UI snapshot/switch/main/cache/render chain. */
#include "ui.h"
#include "ble_service.h"
#include "time_zone_config.h"
#include "gm7_owner.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t now_us=1000000;
static int stopping;
static int atomic_get(const int *p){return *p;}
static int atomic_set(int *p,int v){int old=*p;*p=v;return old;}
static uint64_t kernel_now(void){return now_us;}
struct k_mutex {int unused;};static struct k_mutex lock;
static int k_mutex_lock(struct k_mutex *p,int timeout){(void)p;(void)timeout;return 0;}
static void k_mutex_unlock(struct k_mutex *p){(void)p;}
#define K_FOREVER (-1)
static struct r2_service_status status;
static struct {uint64_t at;uint32_t hot_ms,active_ms,pps_ms;uint8_t known,flags;} diagnostic;
static struct {struct gm7_reported_role report;uint64_t identity;uint8_t state;bool valid,current;} peer_display[GM7_PEERS];
enum {PEER_FRESH=2,PEER_ELIGIBLE_CLAIM=4};
#define r2_service_snapshot r2_normal_service_snapshot
#define r2_service_ui r2_normal_service_ui
#include R2_GM_UI_BODY
#undef r2_service_snapshot
#undef r2_service_ui
static bool pending,rc_selected;static int switch_error;
static bool r2_switch_rc_active(void){return rc_selected;}
static void r2_field_service_ui(struct r2_ui_model *m){r2_normal_service_ui(m);}
#include R2_SWITCH_UI_BODY
static struct r2_ui_model model;
static struct r2_ui_status ui_state;
struct k_spinlock {int unused;};static struct k_spinlock ui_lock;
typedef int k_spinlock_key_t;static unsigned lock_depth;
static int k_spin_lock(struct k_spinlock *p){(void)p;assert(!lock_depth++);return 0;}
static void k_spin_unlock(struct k_spinlock *p,int key){(void)p;(void)key;assert(lock_depth--==1);}
static int stop_requested;
#define lock ui_lock
#define status ui_state
#include R2_UI_PUBLISH_BODY
#undef lock
#undef status
static struct {char unit[6];} identity={"TC-04"};
static struct r2_clock_view local_clock;
static bool own_utc;
static struct r2_lora_view lora;
static uint32_t r2_watchdog_reset_kind(void){return 0;}
static uint64_t k_uptime_get(void){return now_us/1000;}
static void r2_time_snapshot(struct r2_clock_view *out,uint32_t *drops){*out=local_clock;*drops=0;}
static bool r2_time_source_utc_now(uint32_t epoch,int64_t *utc){assert(epoch==5);if(own_utc)*utc=1793659574000000LL+(int64_t)now_us;return own_utc;}
static int64_t r2_time_now_us(void){return(int64_t)now_us;}
void r2_lora_view_named(struct r2_lora_view *v,uint64_t *id,char name[25]){*v=lora;*id=11;name[0]=0;}
#include R2_MAIN_UI_BODY
static bool checked_offset(uint16_t id,uint64_t utc,int16_t *out){assert(!lock_depth);return r2_tz_db_offset(id,utc,out);}
#define r2_tz_db_offset checked_offset
#include "../src/ui_render.c"
#undef r2_tz_db_offset
static uint8_t actual[R2_UI_FRAME_BYTES],utc_pixels[R2_UI_FRAME_BYTES];
static void assert_clock_and_saver(void){
 int64_t utc,local;bool coarse;
 assert(r2_ui_display_time(&model,now_us/1000,&utc,&coarse));
 assert(!strcmp(r2_ui_clock_zone(&model,now_us/1000,utc,&local),"CST")&&local==utc-21600000000LL);
 for(unsigned saver=0;saver<2;saver++)for(unsigned pos=0;pos<3;pos++){
  struct r2_ui_status s={.screensaver=saver,.screensaver_position=(uint8_t)pos};
  r2_ui_render(actual,&model,&s,now_us/1000);
  struct r2_ui_model u=model;u.tz=r2_tz_unknown();u.lora.tz=r2_tz_unknown();u.display_tz=r2_tz_unknown();
  r2_ui_render(utc_pixels,&u,&s,now_us/1000);assert(memcmp(actual,utc_pixels,sizeof actual));
  bool using_saver=saver&&r2_ui_saver_allowed(&model,now_us/1000);
  unsigned top=using_saver?r2_ui_saver_top(pos)+3:20;
  memset(utc_pixels,0,sizeof utc_pixels);label(utc_pixels,8,(int)top,"CST",1,C_DIM);
  for(unsigned i=0;i<sizeof actual;i++)if(utc_pixels[i]==C_DIM)assert(actual[i]==C_DIM);
 }
}
int main(void){
 /* Exactly the sampled04 condition: own ACTIVE source, known CST, LoRa TX. */
 status=(struct r2_service_status){.normal=true,.configured=true,.active=true,.serving=true,
  .source_eligible=true,.local_source_epoch=5,.root=0,.root_boot=22,.source_epoch=5,
  .identities={11,12},.tz={2,-360},.tz_until_us=UINT64_MAX};
 diagnostic.known=2;own_utc=true;
 local_clock=(struct r2_clock_view){.valid=true,.source_available=true,.fix_active=true,
  .pps_present=true,.pps_age_us=0,.source_epoch=5,.utc_us=1793659575000000LL};
 lora=(struct r2_lora_view){.requested=true,.active=true,.transmitter=true,.choice=RC8_SELECT_LOCAL_GM};
 assert(gm7_ui_publish(now_us/1000,true,true)==now_us/1000+20);
 assert(model.role==R2_UI_GM&&model.tz.id==2&&model.display_tz.id==2&&model.display_tz_source==R2_UI_TZ_LOCAL);
 assert(r2_ui_saver_allowed(&model,now_us/1000));
 assert_clock_and_saver();
 /* Local source/metadata loss and logical stop cannot erase a learned zone. */
 status.tz=r2_tz_unknown();status.tz_until_us=0;own_utc=false;
 local_clock.valid=local_clock.fix_active=local_clock.pps_present=false;
 status.serving=status.source_eligible=false;pending=true;now_us+=1000000;
 (void)gm7_ui_publish(now_us/1000,true,true);assert(model.display_tz.id==2);
 r2_ui_stop();(void)gm7_ui_publish(now_us/1000,true,true);assert(model.display_tz.id==2);
 /* Fresh BLE clock returns without any GNSS; cached local zone still draws. */
 pending=false;status.root=1;status.follower=status.follower_clock_valid=true;
 status.root_boot=33;status.source_epoch=6;status.authority_until=status.service_until=now_us+1500000;
 status.follower_utc=1793659576000000LL;status.time_sampled_us=now_us;status.source_frame_ns=1000000;
 status.precision.authorized=status.precision.locked=status.precision.fit_valid=status.precision.map_valid=status.precision.rms_valid=true;status.precision.rms_ns=49;
 lora.choice=RC8_SELECT_BLE;diagnostic.known=0;
 (void)gm7_ui_publish(now_us/1000,true,true);assert(model.ble_time_selected&&model.display_tz.id==2);
 assert_clock_and_saver();
 /* A completely cold standby learns known BLE metadata before UTC exists. */
 memset(&model,0,sizeof model);++status.profile_changes;status.follower_clock_valid=false;status.display_coarse_valid=false;
 status.tz=(struct r2_tz){2,-360};status.tz_until_us=now_us+750000;
 (void)gm7_ui_publish(now_us/1000,true,true);assert(model.display_tz.id==2);
 int64_t unavailable;bool coarse;assert(!r2_ui_display_time(&model,now_us/1000,&unavailable,&coarse));
 status.follower_clock_valid=true;(void)gm7_ui_publish(now_us/1000,true,true);assert_clock_and_saver();
 puts("PASS actual GM7 status snapshot -> service switch/UI -> main GM publisher -> boot RAM cache -> active-TX clock/saver and cold BLE standby pixels");
 return 0;
}
