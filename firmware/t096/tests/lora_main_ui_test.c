/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual two main UI publication bodies; fake only source/service boundaries. */
#include "ui.h"
#include "ble_service.h"
#include "time_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {char unit[6];} identity={"RX-A"};
static struct {char name[25];} service;
static struct {bool bt_done;int bt_result,bt_submit;} p={true,0,0};
static struct r2_lora_view view;
static struct r2_ui_model delivered;
static uint64_t delivered_emitter,delivered_upstream;
static const char *air_name="OTA Sender";
static uint64_t emitter=UINT64_C(0xdeadbeef1234),clock_ms=1000;
static char transmitted_name[25];
static uint64_t k_uptime_get(void){return clock_ms;}
static uint32_t r2_watchdog_reset_kind(void){return 0;}
void r2_time_snapshot(struct r2_clock_view *t,uint32_t *drops){(void)t;(void)drops;}
void r2_service_ui(struct r2_ui_model *m){m->ble_rx_supported=true;m->ble_rx_requested=false;strcpy(m->upstream_name,"BLE Cached");}
void r2_service_ui_logic(struct r2_ui_model *m,const struct r2_service_status *s){(void)s;r2_service_ui(m);}
bool r2_time_source_utc_now(uint32_t epoch,int64_t *utc){(void)epoch;(void)utc;return false;}
int64_t r2_time_now_us(void){return (int64_t)clock_ms*1000;}
void r2_time_ui_guard(struct r2_clock_view *t,uint32_t *drops){(void)t;(void)drops;}
void r2_time_precision_snapshot(struct r2_pps_precision_view *p){memset(p,0,sizeof(*p));}
void r2_lora_view_named(struct r2_lora_view *v,uint64_t *factory,char name[25]){*v=view;*factory=emitter;memset(name,0,25);if(air_name)strcpy(name,air_name);}
void r2_ui_publish(const struct r2_ui_model *m){delivered=*m;}
void r2_ui_publish_owned(const struct r2_ui_model *m,uint64_t id,const struct r2_ui_label_ids *labels){delivered=*m;delivered_emitter=id;delivered_upstream=labels->upstream;}
bool r2_lora_source_needed(void){return true;}
void r2_lora_name_publish(const char *name){strcpy(transmitted_name,name);}
#include "main_lora_ui_under_test.h"
int main(void){
 for(unsigned path=0;path<2;path++)for(unsigned scenario=0;scenario<6;scenario++){
  view=(struct r2_lora_view){.requested=true,.active=true,.choice=RC8_SELECT_LORA};
  air_name=scenario==0?"Field Sender":scenario==1?"Clock-Actual":scenario==2?"\xc3\x89metteur":NULL;
  emitter=scenario==5?0:UINT64_C(0xdeadbeef1234);
  if(scenario>=3){view.choice=RC8_SELECT_NONE;view.utc_us=0;} /* expected provider while WAIT/expired */
  if(path)normal_ui_publish(999,true);
  else {struct r2_clock local;struct r2_service_status status={0};r2_clock_init(&local,0,R2_ASSOC_UNKNOWN);gm7_ui_logic(999,true,true,&status,&local);}
  assert(!strcmp(delivered.unit,"RX-A"));
  const bool supplied=air_name&&strncmp(air_name,"Clock-",6);
  if(path)assert(!strcmp(delivered.upstream_name,supplied?air_name:emitter?"1234":"--"));
  else {assert(delivered_emitter==emitter&&delivered_upstream==emitter&&delivered.upstream_is_lora);
        assert(!strcmp(delivered.upstream_name,air_name?air_name:""));}
  assert(delivered.sampled_ms==clock_ms);
 }
 strcpy(service.name,"Clock-Default");publish_lora_name();assert(!strcmp(transmitted_name,"RX-A"));
 strcpy(service.name,"Custom Emitter");publish_lora_name();assert(!strcmp(transmitted_name,"Custom Emitter"));
 puts("PASS actual GM7 and normal main UI publications: authenticated custom/UTF8 names, generic-name ID fallback, expected ID/unknown WAIT, own advertised name resolution");
}
