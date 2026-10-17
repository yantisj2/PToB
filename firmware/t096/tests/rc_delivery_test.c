/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis. Production transition snapshots only. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
#include "radio_capture.h"
#include "timing_events.h"
static struct rc5_state timing;
static struct rc5_clock local_clock;
static struct rc5_origin current_origin;
static uint64_t last_rx,discipline_at,usable_tx_from;
static uint64_t delivery_packet_tuple,delivery_correction_tuple[2],delivery_origin_issued,delivery_origin_expires,delivery_discipline_tuple;
static uint32_t delivery_packet_count,delivery_discipline_rx;
static uint32_t rx,wire_rejected,capture_epoch,holdover_expiries;
static unsigned sent_count;
static bool holdover_expired;
#define RC_FIELD_HOLD_TICKS UINT64_C(8000000)
static struct r2_radio_capture_stats hardware,delivery_capture_baseline;
void r2_radio_capture_stats(struct r2_radio_capture_stats *out){*out=hardware;}
struct event {unsigned kind;int result;uint64_t peer;uint32_t detail,a,b;};
static struct event events[64];static unsigned count;
#define R2_TIMING_EVENTS_TEST 1
/* The production header's host inline is redirected at this fake boundary. */
#define r2_timing_event record_event
static void record_event(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b)
{assert(count<64);events[count++]=(struct event){k,r,p,d,a,b};}
static void r2_radio_capture_freeze_loss(void){}
#include "rc_delivery_under_test.h"
int main(void){
 timing=(struct rc5_state){.accepted=33,.rejected=7,.evicted=9,.context_until=UINT64_MAX,.reference={.completed_at=123}};
 timing.pending[0].used=timing.pending[RC5_HISTORY-1].used=true;
 last_rx=101;rx=55;wire_rejected=11;current_origin.expires=UINT64_MAX;
 local_clock.generation=19;capture_epoch=4;hardware.invalid=13;hardware.overflow=17;
 delivery_discipline_tuple=UINT64_MAX;delivery_discipline_rx=51;
 delivery_packet_tuple=1234;delivery_packet_count=2;
 delivery_correction_tuple[0]=123;delivery_correction_tuple[1]=456;
 delivery_origin_issued=700;delivery_origin_expires=900;
 hardware.completed=100;hardware.aborted=20;
 hardware.rx=(struct r2_radio_rx_stats){.armed=80,.completed=60,.aborted=9,.no_done=7,.no_address=6,.bad_crc=5,.malformed=4,.overflow=3,.last_arm=11,.last_complete=12,.last_abort=13,.last_arm_owner=14,.last_abort_owner=15};
 delivery_capture_baseline=(struct r2_radio_capture_stats){.completed=90,.aborted=10,.rx={.armed=70,.completed=50,.aborted=8,.no_done=6,.no_address=5,.bad_crc=4,.malformed=3,.overflow=2}};
 discipline_at=1000;sent_count=2;
 holdover_poll(discipline_at-1);assert(!count&&!holdover_expired&&sent_count==2);
 holdover_poll(discipline_at+RC_FIELD_HOLD_TICKS-1);assert(!count);
 holdover_poll(discipline_at+RC_FIELD_HOLD_TICKS);
 assert(count==19&&holdover_expiries==1&&holdover_expired&&!sent_count&&usable_tx_from==8001000);
 assert(events[0].kind==R2_TIMING_HOLDOVER_EXPIRE&&events[0].a==500000);
 for(unsigned i=1;i<=5;i++)assert(events[i].kind==30&&events[i].result==1&&events[i].detail==i);
 assert(events[1].peer==101&&events[1].a==55&&events[1].b==11);
 assert(events[2].peer==123&&events[2].a==33&&events[2].b==7);
 assert(events[3].peer==UINT64_MAX&&events[3].a==9&&events[3].b==2);
 assert(events[4].peer==8001000&&events[4].a==13&&events[4].b==17);
 assert(events[5].peer==UINT64_MAX&&events[5].a==19&&events[5].b==4);
 assert(events[6].peer==UINT64_MAX&&events[6].a==51&&events[6].b==55);
 assert(events[7].peer==1234&&events[7].a==2&&events[8].peer==123&&events[9].peer==456);
 assert(events[10].peer==700&&events[11].peer==900);
 assert(events[12].peer==((UINT64_C(80)<<32)|70)&&events[12].a==60&&events[12].b==50);
 assert(events[13].peer==((UINT64_C(9)<<32)|8)&&events[13].a==7&&events[13].b==6);
 assert(events[14].peer==((UINT64_C(5)<<32)|4)&&events[14].a==4&&events[14].b==3);
 assert(events[15].peer==((UINT64_C(3)<<32)|2)&&events[15].a==6&&events[15].b==5);
 assert(events[16].peer==13&&events[16].a==20&&events[16].b==10);
 assert(events[17].peer==11&&events[17].a==100&&events[17].b==90);
 assert(events[18].peer==12&&events[18].a==14&&events[18].b==15);
 holdover_poll(UINT64_MAX);assert(count==19); /* no periodic spam */
 struct rc5_state before=timing;
 delivery_snapshot(2,0);assert(count==30&&events[22].peer==0&&!memcmp(&before,&timing,sizeof timing));
 timing=(struct rc5_state){0};last_rx=0;rx=wire_rejected=0;
 delivery_snapshot(1,0);assert(events[30].peer==0&&events[31].peer==0&&events[32].b==0);
 assert(events[33].a==13&&events[33].b==17); /* global counters survive scope clear */
 puts("PASS production RC delivery snapshots: exact expiry, one transition, raw zero/max ticks, stage counters, no receiver mutation, scope/global baselines");
}
