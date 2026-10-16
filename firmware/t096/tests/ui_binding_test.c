/* Actual direct service-to-display path, with no local GNSS. */
#include "ble_service.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static struct r2_service_status fixture;
static uint64_t current;
static int stop_requested;
static bool profile_supported=true;
static int atomic_get(int *p){return *p;}
static uint64_t now_us(void){return current;}
void r2_service_snapshot(struct r2_service_status *s){*s=fixture;}
void r2_time_precision_snapshot(struct r2_pps_precision_view *p){memset(p,0,sizeof(*p));}
#include "ui_binding_under_test.h"
static struct r2_ui_model model(void){struct r2_ui_model m={.sampled_ms=current/1000,.platform_ready=true};strcpy(m.unit,"TC-03");r2_service_ui(&m);return m;}
int main(void){
 current=90000000;
 fixture=(struct r2_service_status){.active=true,.display_coarse_valid=true,.display_coarse_utc=1700000000000000ULL,.display_coarse_at=89000000};
 struct r2_ui_model initial=model();assert(initial.coarse_valid&&!initial.clock.valid&&!initial.ble_time_selected&&!r2_ui_analog(&initial,current/1000));
 assert(initial.coarse_utc_us==1700000000000000ULL&&initial.coarse_at_us==89000000);
 int64_t displayed;bool held;assert(r2_ui_display_time(&initial,current/1000,&displayed,&held)&&held&&displayed==1700000001000000LL);
 assert(!r2_ui_display_time(&initial,current/1000+2501,&displayed,&held));
 struct r2_ui_model edge=initial;edge.coarse_utc_us=INT64_MAX;assert(!r2_ui_display_time(&edge,current/1000,&displayed,&held));
 edge=initial;edge.coarse_at_us=current+1;assert(!r2_ui_display_time(&edge,current/1000,&displayed,&held));
 fixture.display_coarse_valid=false;current+=5000000;initial=model();assert(initial.coarse_valid&&!initial.scope_active&&!initial.precision.authorized);
 current=100060000;
 fixture=(struct r2_service_status){.at=100000000,.time_sampled_us=100000000,.source_frame_ns=100,.active=true,.self=0,.root=1,.coordinator=255,.standby=255,.follower=true,.follower_clock_valid=true,.follower_utc=1000000000000,.service_until=100250000,.authority_until=100250000,.root_boot=11,.source_epoch=9,
 .precision={.authorized=true,.locked=true,.fit_valid=true,.map_valid=true,.rms_valid=true,.rms_ns=89,.drift_valid=true}};
 fixture.identities[0]=UINT64_C(0x1000000000000003);fixture.identities[1]=UINT64_C(0x1000000000000001);
 fixture.reception=(struct r2_link_quality_view){.samples=12,.last_rx=current-50000,.delivery_per_mille=950};
 struct r2_ui_model m=model();assert(m.scope_active&&m.role==R2_UI_FOLLOWER&&m.authenticated_gm);
 assert(m.precision_beat_hz==16&&m.ble_reception_valid&&m.ble_delivery_per_mille==950&&m.ble_rx_age_ms==50);
 assert(m.ble_time_selected&&m.clock.valid&&m.clock.utc_us==1000000060000&&m.follower_pps_valid);
 assert(!m.clock.pps_present&&!m.clock.source_available&&r2_ui_analog(&m,current/1000));
 assert(!strcmp(m.root_name,"TC-01"));
 assert(!strcmp(r2_ui_servo_state(&m,current/1000),"LOCKED"));
 assert(!strcmp(r2_ui_servo_state(&m,100250),"LOCKED"));
 assert(!r2_ui_face_locked(&m,100560));
 assert(!r2_ui_analog(&m,100250)); /* renderer must honor delivered expiry */
 struct r2_service_status good=fixture;
 fixture.identities[1]=fixture.identities[0];m=model();assert(!m.scope_active&&!m.ble_time_selected);fixture=good;
 fixture.source_frame_ns=0;m=model();assert(m.ble_time_selected&&!m.follower_pps_valid&&!r2_ui_analog(&m,current/1000));fixture=good;
 fixture.source_epoch=0;m=model();assert(!m.follower_pps_valid);fixture=good;
 fixture.root_boot=0;m=model();assert(!m.follower_pps_valid);fixture=good;
 fixture.precision.locked=false;m=model();assert(!m.follower_pps_valid);fixture=good;
 fixture.precision.fit_valid=false;m=model();assert(!m.follower_pps_valid);fixture=good;
 fixture.precision.authorized=false;m=model();assert(!m.follower_pps_valid);fixture=good;
 fixture.precision.map_valid=false;m=model();assert(!m.follower_pps_valid&&!r2_ui_analog(&m,current/1000));fixture=good;
 fixture.service_until=current;m=model();assert(!m.ble_time_selected&&!r2_ui_analog(&m,current/1000));fixture=good;
 fixture.authority_until=current;m=model();assert(!m.scope_active&&!m.ble_time_selected);fixture=good;
 stop_requested=1;m=model();assert(!m.scope_active&&!m.ble_time_selected);stop_requested=0;
 /* Short freshness gaps retain only a settling label, never time/authority. */
 fixture=good;fixture.precision.locked=false;fixture.precision.accepted=10;m=model();
 fixture.precision.accepted=11;current+=1000;m=model();assert(m.sync_attempt);
 fixture.authority_until=fixture.service_until=current;fixture.follower_clock_valid=false;
 fixture.precision.authorized=fixture.precision.map_valid=false;current+=1000000;m=model();
 assert(m.sync_attempt&&!m.scope_active&&!m.ble_time_selected&&!r2_ui_analog(&m,current/1000));assert(m.coarse_valid&&m.coarse_utc_us==1000000061000ULL);assert(!strcmp(m.root_name,"TC-01"));
 current+=5000000;m=model();assert(!m.sync_attempt&&m.coarse_valid&&!r2_ui_analog(&m,current/1000)); /* real loss cannot remain syncing */
 fixture=good;fixture.authority_until=current+1000000;m=model();assert(m.sync_attempt);
 stop_requested=1;m=model();assert(!m.sync_attempt);stop_requested=0;
 fixture=good;current=100060000;
 /* UI lock follows delivered RMS, independent of strict PLL or local GPS. */
 fixture=good;current=200000000;fixture.time_sampled_us=current;fixture.authority_until=fixture.service_until=current+10000000;
 fixture.precision.locked=false;fixture.precision.rms_ns=50;m=model();
 assert(r2_ui_face_locked(&m,current/1000)&&!r2_ui_analog(&m,current/1000)&&!m.clock.pps_present&&!m.hot_ready);
 fixture.precision.rms_ns=201;m=model();assert(!r2_ui_face_locked(&m,current/1000));
 fixture.precision.rms_ns=200;m=model();assert(r2_ui_face_locked(&m,current/1000));
 fixture.precision.rms_valid=fixture.precision.map_valid=fixture.precision.authorized=false;
 fixture.follower_clock_valid=false;fixture.authority_until=fixture.service_until=current;
 current+=499000;m=model();assert(r2_ui_face_locked(&m,current/1000)&&!m.scope_active&&!m.ble_time_selected);
 current+=1000;m=model();assert(!r2_ui_face_locked(&m,current/1000));
 fixture=good;fixture.time_sampled_us=current;fixture.authority_until=fixture.service_until=current+10000000;m=model();assert(r2_ui_face_locked(&m,current/1000));
 fixture.root_boot++;fixture.follower_clock_valid=false;m=model();assert(!r2_ui_face_locked(&m,current/1000));
 fixture=good;fixture.time_sampled_us=current;fixture.authority_until=fixture.service_until=current+10000000;m=model();assert(r2_ui_face_locked(&m,current/1000));
 stop_requested=1;m=model();assert(!r2_ui_face_locked(&m,current/1000));stop_requested=0;
 uint8_t pixels[R2_UI_BYTES];struct r2_ui_status s={0};m=model();
 for(unsigned page=0;page<R2_UI_PAGES;page++){s.page=page;r2_ui_render(pixels,&m,&s,current/1000);}
 puts("Actual service->BLE UTC->role/servo/all pages PASS; bootstrap/coarse,wrong identity/boot/epoch,unlocked fit,expired map/source refuse PPS claim; local GNSS absent supported; physical UTC/output UNKNOWN");
}
