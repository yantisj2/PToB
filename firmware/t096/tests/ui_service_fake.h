#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "ui.h"
#include "rc_shadow.h"
#include <errno.h>
typedef int atomic_t;
static int atomic_get(atomic_t *v){return *v;}
static int atomic_cas(atomic_t *v,int a,int b){if(*v!=a)return 0;*v=b;return 1;}
static int atomic_set(atomic_t *v,int n){int old=*v;*v=n;return old;}
struct k_spinlock {int unused;};typedef int k_spinlock_key_t;
static int k_spin_lock(struct k_spinlock *l){return 0;}
static void k_spin_unlock(struct k_spinlock *l,int k){}
#define K_THREAD_STACK_DEFINE(n,s) char n[s]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
#define K_NO_WAIT 0
#define K_MSEC(n) n
struct k_thread {int unused;};
static uint64_t fake_now;static int mode,priority,init_calls,frame_calls,off_calls;
static void (*entry)(void*,void*,void*);
static uint64_t starts[64];
static uint8_t retained[R2_UI_BYTES];static const uint8_t *retained_fb;static bool buffer_changed;
static uint64_t first_saver;static unsigned saver_positions;static bool dim_seen,awake_after_dim;
static unsigned random_calls;static uint64_t random_at[32];
static unsigned sync_grace_frames;static bool sync_long_expired,sync_recovered;
static struct r2_ui_model sync_model(uint64_t now){
 bool loss=(now>=1000&&now<1200)||(now>=1800&&now<2600);
 struct r2_ui_model m={.sampled_ms=now,.unit="TC-04",.platform_ready=true,
  .profile_supported=true,.profile_rc=true,.boundary_clock=true,.ble_active=true,
  .role=loss?R2_UI_NO_ROLE:R2_UI_FOLLOWER,.scope_active=!loss,.authenticated_gm=!loss,
  .ble_time_selected=!loss,.ble_valid_until_us=(now+50)*1000,.sync_attempt=loss,
  .clock={.valid=!loss,.utc_us=1792340000000000LL+(int64_t)now*1000},
  .precision={.map_valid=!loss,.authorized=!loss,.locked=!loss}};
 return m;
}
static uint32_t sys_rand32_get(void){assert(random_calls<32);random_at[random_calls]=fake_now;++random_calls;return 1;}
static int64_t k_uptime_get(void){return fake_now;}
static void k_sleep(unsigned ms){fake_now+=ms;
 if(mode==34){
  struct r2_ui_status s;r2_ui_snapshot(&s);
  if(s.drawn_sync_grace){assert(!s.analog&&s.drawn_time&&s.drawn_coarse&&!s.drawn_map);++sync_grace_frames;}
  if(s.last_frame_ms>=2350&&s.last_frame_ms<2600){assert(!s.drawn_time&&!s.drawn_sync_grace);sync_long_expired=true;}
  if(s.last_frame_ms>=2750&&s.analog&&s.drawn_time&&!s.drawn_sync_grace)sync_recovered=true;
  struct r2_ui_model m=sync_model(fake_now);r2_ui_publish(&m);
  if(fake_now>=3200)r2_ui_stop();return;
 }
 if(mode>=16){
  struct r2_ui_model m={.sampled_ms=fake_now,.unit="TC-02",.platform_ready=true,
   .scope_active=true,.ble_active=true,.profile_supported=true,.role=R2_UI_GM,.gm_source_fresh=true,
   .clock={.valid=!(mode==18&&fake_now>=300700),.source_available=true,.pps_present=true,.utc_us=36642000000LL}};
  if((mode>=19&&mode<=21)||mode>=24){
   m.role=R2_UI_FOLLOWER;m.authenticated_gm=m.follower_pps_valid=true;
   m.ble_time_selected=true;m.ble_valid_until_us=m.face_lock_until_us=mode>=24?800000000:400000000;
   m.precision.authorized=m.precision.map_valid=m.precision.locked=true;
   if(mode==28&&fake_now>=7000&&fake_now<9000)m.precision.map_valid=false;
   if(fake_now>=300700){
    if(mode==19){m.scope_active=false;m.ble_active=false;}
    if(mode==20){m.follower_pps_valid=false;m.precision.locked=false;}
    if(mode==21)m.ble_valid_until_us=300700000;
   }
  }
  if(mode==22&&fake_now>=300700){m.clock.pps_present=false;m.gm_source_fresh=false;}
  if(mode==23&&fake_now>=300700)m.clock.pps_age_us=R2_PPS_FRESH_US;
  if(mode>=35){m.gm_capable=true;m.clock.fix_active=mode!=38;
   if(mode==36)m.role=R2_UI_GM;
   if(mode==37)m.profile_rc=m.boundary_clock=true;
  }
  r2_ui_publish(&m);
  if(mode==39&&fake_now>=14000&&fake_now<14020)(void)r2_ui_page(R2_UI_SHADOW_PAGE);
  if(mode==25&&fake_now>=300700&&fake_now<300720)(void)r2_ui_page(R2_UI_SHADOW_PAGE);
  if(fake_now>=(mode==35?622000:mode>=26?25000:mode==24?622000:mode==25?316000:mode==16?367000:mode>=18?301200:3500))r2_ui_stop();
  return;
 }
 if(mode>=8&&mode<14){struct r2_ui_model m={.sampled_ms=fake_now,.unit="TC-03",.scope_active=true,.role=(mode==9||(mode==13&&fake_now>=3500&&fake_now<4500))?R2_UI_GM:R2_UI_FOLLOWER,.clock={.pps_present=mode!=12,.pps_age_us=10},.precision={.map_valid=true}};r2_ui_publish(&m);
  if(mode==11&&fake_now>=2000&&fake_now<2020)(void)r2_ui_page(0);
  if(mode==11&&fake_now>=2800&&fake_now<2820)(void)r2_ui_page(R2_UI_SHADOW_PAGE);
 }
 if(mode==1||fake_now>=(mode==8||mode==11?23000:mode>=4?6500:3300))r2_ui_stop();}
static void k_thread_create(struct k_thread *t,char *stack,size_t size,void (*fn)(void*,void*,void*),void *a,void *b,void *c,int p,int opts,int delay){entry=fn;priority=p;}
static void k_thread_name_set(struct k_thread *t,const char *n){}
int r2_display_init(void){++init_calls;fake_now+=330;return 0;}
int r2_display_frame(const uint8_t *fb){if(frame_calls<64)starts[frame_calls]=fake_now;++frame_calls;
 struct r2_ui_status s;r2_ui_snapshot(&s);if(s.screensaver){if(!first_saver)first_saver=fake_now;saver_positions|=1u<<s.screensaver_position;}
 fake_now+=mode>=16?26:100;if(mode==3||mode==6||(mode==7&&frame_calls==1)){memcpy(retained,fb,sizeof(retained));retained_fb=fb;return -ETIMEDOUT;}return 0;}
int r2_display_brightness(uint8_t percent){assert(percent==50||percent==100);if(percent==50)dim_seen=true;else if(dim_seen)awake_after_dim=true;return 0;}
int r2_display_button(void){if(mode==16)return (fake_now>=360800&&fake_now<365400)||(fake_now>=366000&&fake_now<366200);if(mode>=17)return 0;if(mode==14)return fake_now>=500&&fake_now<2700;if(mode==15)return fake_now>=500&&fake_now<6000;if(mode>=8)return mode==10&&fake_now>=1000&&fake_now<1160;if(mode==4)return fake_now>=500&&fake_now<3000;
 if(mode==5||mode==6)return fake_now>=500&&fake_now<6000;
 return (fake_now>=1000&&fake_now<1160)||(fake_now>=1800&&fake_now<2860);}
int r2_display_off(void){++off_calls;if(retained_fb)buffer_changed=memcmp(retained,retained_fb,sizeof(retained))!=0;return mode==3||mode==6?-ETIMEDOUT:0;}

int r2_display_recover(void){
 if(!retained_fb)return 0;
 buffer_changed|=memcmp(retained,retained_fb,sizeof(retained))!=0;
 if(mode!=7||fake_now<1800)return -EAGAIN;
 retained_fb=NULL;return 0;
}

int r2_display_temperature(int16_t *quarter_c){*quarter_c=92;return 0;}
static bool witness_enabled;
void r2_shadow_enable(bool enabled){witness_enabled=enabled;assert(!enabled||mode!=9);}
bool r2_shadow_read(uint32_t after,struct rc_shadow_record *r,struct rc_shadow_status *st){
 *st=(struct rc_shadow_status){.enabled=witness_enabled,.now_tick=fake_now*16000,.epoch=mode==31&&fake_now>=8000?2:1,.edges=(uint32_t)(fake_now/1000)};
 if(after==UINT32_MAX)return false;
 uint32_t sequence=after+1;if(mode==30&&sequence==8)++sequence;
 if((uint64_t)sequence*1000+800>fake_now)return false;
 *r=(struct rc_shadow_record){.sequence=sequence,.edge={(uint64_t)sequence*16000000,mode==31&&sequence>=8?2:1,R2_CAPTURE_PPS},.arrival_us=(int64_t)sequence*1000000,.valid=true,.reason=RC_SHADOW_OK,.phase_ns=-37,.reference_seen=true,.reference_fix=true,.reference_pps=true,.map={.root=mode==29&&sequence>=8?3:1,.root_boot=9,.root_epoch=1,.parent=2,.parent_boot=10}};
 if(mode==26&&sequence==2)r->phase_ns=122;
 if(mode==27&&sequence==13)r->phase_ns=-500;
 if(mode==32&&sequence>=8)r->map.train=2;
 if(mode==33&&sequence>=8)r->map.parent_epoch=2;
 return true;
}
