/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_capture_cleanup_fake.h"
#include "gm7_controller.h"
#include "radio_capture.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
struct fake_radio fake_radio;
static unsigned routes,cleanups,releases;
static bool already_done,disable_fault;
unsigned nrfx_ppi_channel_enable(unsigned ch){routes|=1u<<ch;return NRFX_SUCCESS;}
unsigned nrfx_ppi_channel_disable(unsigned ch){if(disable_fault)return 1;routes&=~(1u<<ch);return NRFX_SUCCESS;}
bool nrf_radio_event_check(void *p,unsigned e){assert(p==NRF_RADIO);return true;}
uint32_t nrf_timer_cc_get(void *p,unsigned c){assert(p==NRF_TIMER3);return 123;}
bool r2_gm7_controller_receiving(void){return true;}
void r2_gm7_controller_cancel(uint64_t now){(void)now;}
void r2_gm7_controller_capture(void *o,bool aux,uint32_t a,uint32_t e,uint32_t epoch,bool complete){assert(!complete);}
int gm7_air_received(const uint8_t *p,size_t n,const uint8_t **ad,size_t *len,uint8_t address[6]){return -EINVAL;}
#define CONFIG_BT_CTLR_ADV_EXT 1
#define ADV_CHAN_MAX 3
#define TICKER_INSTANCE_ID_CTLR 0
#define TICKER_USER_ID_LLL 0
#define TICKER_ID_SCAN_STOP 0
#define EVENT_DONE_EXTRA_TYPE_SCAN 1
#define NODE_RX_TYPE_EXT_AUX_RELEASE 2
#define LL_ASSERT assert
#define unlikely(x) (x)
struct lll_scan { unsigned chan,duration_reload,duration_expire,is_stop,is_aux_sched;void *lll_aux; };
struct node_rx_pdu {struct {int type;void *link;} hdr;struct {void *param,*lll_aux;} rx_ftr;};
struct event_done_extra {int unused;};
static struct node_rx_pdu node;
static struct event_done_extra extra;
static void lll_isr_status_reset(void){}
static bool lll_is_done(void *p,bool *resume){*resume=false;return already_done;}
static void radio_filter_disable(void){}
static int ticker_stop(int a,int b,int c,void *d,void *e){return 0;}
static struct event_done_extra *ull_done_extra_type_set(int t){return &extra;}
static struct node_rx_pdu *ull_pdu_rx_alloc(void){return &node;}
static void ull_rx_put_sched(void *link,struct node_rx_pdu *p){assert(p->hdr.type==NODE_RX_TYPE_EXT_AUX_RELEASE);++releases;}
static void lll_isr_cleanup(void *p){++cleanups;}
#include "scan_cleanup.inc"
int main(int argc,char **argv){
 assert(argc==2);bool original=atoi(argv[1]);
 uint8_t management[200]={0},timing[161]={7,159,0,157,255,255,255};int owner;
 struct lll_scan scan={.is_aux_sched=1};
 assert(!r2_radio_capture_enable(1,6));assert(!r2_gm7_capture_enable(1,7,8));
 fake_radio.PACKETPTR=(uintptr_t)management;
 assert(r2_gm7_capture_arm(management,false,true));
 r2_gm7_capture_abort(timing);assert(r2_gm7_capture_busy()); /* foreign owner */
 isr_done_cleanup(&scan);
 assert(cleanups==1&&releases==1&&!scan.is_aux_sched);
 r2_radio_capture_arm(&owner,timing,1,2,1,true);
 struct r2_radio_capture_stats s;r2_radio_capture_stats(&s);
 if(original){assert(r2_gm7_capture_busy()&&!s.armed&&s.invalid==1);puts("REPRODUCED: primary scanner AUX cancellation blocks G1 capture");return 0;}
 assert(!r2_gm7_capture_busy()&&s.armed&&s.invalid==0);
 r2_radio_capture_complete(&owner,true,true);struct r2_radio_record record;
 assert(r2_radio_capture_pop(&record)&&record.tx&&record.event==1);
 /* The scanner's duplicate-done early return still releases its capture. */
 assert(r2_gm7_capture_arm(management,false,true));already_done=true;
 isr_done_cleanup(&scan);assert(!r2_gm7_capture_busy()&&cleanups==1);
 /* Driver failure must keep ownership; no manufactured release. */
 assert(r2_gm7_capture_arm(management,false,true));disable_fault=true;
 isr_done_cleanup(&scan);assert(r2_gm7_capture_busy());disable_fault=false;
 isr_done_cleanup(&scan);assert(!r2_gm7_capture_busy()&&!routes);
 puts("PASS scanner AUX cleanup -> immediate actual G1 capture; duplicate cleanup, foreign owner and failed-disable boundaries");
}
