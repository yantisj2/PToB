/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "time_zone.h"
#include "gm7_codec.h"
#include "rc_dynamic.h"
#include "rc_gm_adapter.h"
#include "role_announcement.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint8_t bytes[7];} bt_addr_le_t;
static int bt_addr_le_cmp(const bt_addr_le_t *a,const bt_addr_le_t *b){return memcmp(a,b,sizeof *a);}
enum {BT_DATA_MANUFACTURER_DATA=255,BT_LE_SCAN_TYPE_PASSIVE=0};
struct bt_data {uint8_t type,data_len;const uint8_t *data;};
static uint64_t capture_us;
static uint64_t mono_us(void){return capture_us;}
static uint32_t k_uptime_get_32(void){return (uint32_t)(capture_us/1000);}
#include "rc_tz_discovery_under_test.h"
static struct r2_group_config group;
static struct r2_tz_lease coarse_tz;
static uint64_t coarse_tz_sequence;
static struct rc5_routes routes;
static struct rc7_path upstream_path;
static struct rc5_gm_context gm_context;
static bt_addr_le_t parent;
static unsigned parent_index;
static bool parent_known,parent_relay;
static bool parent_synced,upstream_usable,scanning,creating,lt_paused,relay_fallback,slot_scan_failed;
static void *lt_conn;
static unsigned node=1,scan_starts,scan_stops;
static struct {bool scanning;} slot_state;
struct bt_le_scan_param {unsigned type,interval,window;};
static struct bt_le_scan_param last_scan;
static int bt_le_scan_start(const struct bt_le_scan_param *p,void *cb){
 assert(!cb);last_scan=*p;++scan_starts;return 0;
}
static int bt_le_scan_stop(void){++scan_stops;return 0;}
static void check(bool value,const char *reason){(void)reason;assert(value);}
#include "rc_tz_scan_policy_under_test.h"
#include "rc_tz_observers_under_test.h"
static void emit(struct discovery *d,struct gm7_packet *p){
 uint8_t ad[GM7_TZ_MAX_AD_BYTES];assert(!gm7_tz_ad_encode(ad,sizeof ad,p,&group));
 struct discovery received={.address=d->address};capture_us=d->received_us;
 struct bt_data element={.type=BT_DATA_MANUFACTURER_DATA,.data_len=sizeof ad-2,.data=ad+2};
 assert(!scan_data(&element,&received)&&received.len==sizeof ad-2&&received.received_us==capture_us);
 *d=received;
}
int main(void){
 group=(struct r2_group_config){.id=0x123456789abcdef0ULL,.epoch=7};
 for(unsigned i=0;i<32;i++)group.psk[i]=(uint8_t)(i+1);
 parent_known=true;routes.count=2;routes.node[0].id=11;
 /* Initial admission may supply unknown. Stable timing must keep listening
  * for the later authenticated selection, without changing scan parameters. */
 parent_synced=upstream_usable=scanning=true;
 scan_policy(true);assert(scanning&&!scan_stops);
 scanning=false;lt_conn=(void *)1;scan_policy(true);assert(!scanning&&!scan_starts);
 lt_conn=NULL;lt_paused=true;scan_policy(true);assert(!scanning&&!scan_starts);
 lt_paused=false;scan_policy(false);assert(!scanning&&!scan_starts);
 scan_policy(true);assert(scanning&&scan_starts==1&&last_scan.interval==32&&last_scan.window==8);
 assert(!slot_scan_failed&&!coarse_tz.value.id);
 const uint64_t utc=1700000000000000ULL,tick=16000000;
 gm_context=(struct rc5_gm_context){.source={.root=11,.root_boot=22,.source_epoch=9},
  .time={.utc_us=utc},.at_tick=tick,.authenticated=true};
 struct gm7_packet p={.group=group.id,.key_epoch=group.epoch,.kind=GM7_ANNOUNCE,.flags=GM7_ELIGIBLE};
 p.body.control=(struct gm7_control){.sender=11,.boot=22,.sequence=1,.sent_utc_us=utc,
  .max_age_us=2750000,.source_valid_us=1000000,.source_gen=1,.capture_gen=1,.train_gen=1,
  .birth_us=utc-10000000,.founder=11,.term=1,.active=11,.active_boot=22,.timing_epoch=9,.tz={2,-300}};
 struct discovery d={.received_us=100};emit(&d,&p);gm_tz_observed(&d,tick,100);
 assert(coarse_tz.value.id==2&&coarse_tz.value.offset_min==-300&&coarse_tz_sequence==1);
 p.body.control.tz.offset_min=-360;p.body.control.sequence=2;emit(&d,&p);
 gm_tz_observed(&d,tick,100);assert(coarse_tz.value.offset_min==-360&&coarse_tz_sequence==2);
 p.body.control.tz.offset_min=-300;p.body.control.sequence=1;emit(&d,&p);
 gm_tz_observed(&d,tick,100);assert(coarse_tz.value.offset_min==-360&&coarse_tz_sequence==2);
 p.body.control.sequence=3;p.body.control.tz=r2_tz_unknown();emit(&d,&p);
 gm_tz_observed(&d,tick,100);assert(!coarse_tz.value.id&&coarse_tz_sequence==3);
 r2_tz_renew(&coarse_tz,200,R2_TZ_GM_PERIOD_US);assert(!r2_tz_current(&coarse_tz,200).id);
 p.body.control.sequence=4;p.body.control.tz=r2_tz_manual();emit(&d,&p);
 d.data[170]^=1;gm_tz_observed(&d,tick,100);assert(coarse_tz_sequence==3);d.data[170]^=1;
 d.address.bytes[0]=1;gm_tz_observed(&d,tick,100);assert(coarse_tz_sequence==3);d.address=parent;
 gm_tz_observed(&d,tick,750100);assert(coarse_tz_sequence==3);
 p.body.control.timing_epoch=10;emit(&d,&p);gm_tz_observed(&d,tick,100);assert(coarse_tz_sequence==3);
 p.body.control.timing_epoch=9;p.body.control.boot=p.body.control.active_boot=23;
 emit(&d,&p);gm_tz_observed(&d,tick,100);assert(coarse_tz_sequence==3);
 p.body.control.boot=p.body.control.active_boot=22;p.body.control.sent_utc_us=utc-800000;
 emit(&d,&p);gm_tz_observed(&d,tick,100);assert(coarse_tz_sequence==3);
 /* Reuse the current parent's monotonic challenge. Duplicate matching
  * advertisements renew presence; older or same-token changed pairs cannot. */
 parent_relay=true;coarse_tz_sequence=0;coarse_tz=(struct r2_tz_lease){0};
 upstream_path=(struct rc7_path){.source_epoch=9,.depth=0,.node={{.id=11,.boot=22,.train=1,.epoch=9}}};
 struct rc7_control cap={.sender=11,.sender_boot=22,.path=upstream_path,.next_challenge=10,.tz={2,-300}};
 relay_tz_observed(&cap,&d,100);assert(coarse_tz.value.offset_min==-300);
 cap.next_challenge=11;cap.tz.offset_min=-360;relay_tz_observed(&cap,&d,100);
 assert(coarse_tz_sequence==11&&coarse_tz.value.offset_min==-360);
 cap.next_challenge=10;cap.tz.offset_min=-300;relay_tz_observed(&cap,&d,100);
 assert(coarse_tz.value.offset_min==-360);
 cap.next_challenge=11;relay_tz_observed(&cap,&d,100);assert(coarse_tz.value.offset_min==-360);
 cap.tz.offset_min=-360;d.received_us=200;relay_tz_observed(&cap,&d,200);
 assert(coarse_tz.until_us==300200);
 cap.next_challenge=12;cap.tz=r2_tz_unknown();relay_tz_observed(&cap,&d,200);
 assert(!coarse_tz.value.id);r2_tz_renew(&coarse_tz,300,R2_TZ_RC_PERIOD_US);assert(!coarse_tz.value.id);
 cap.next_challenge=13;cap.tz=r2_tz_manual();cap.path.source_epoch=10;
 relay_tz_observed(&cap,&d,200);assert(coarse_tz_sequence==12);
 puts("PASS actual stable scan/ingress and direct/relay TZ receipt: auth, scope, DST, replay, unknown and presence");
}
