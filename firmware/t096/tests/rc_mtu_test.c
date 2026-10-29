/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis. Production host MTU callback boundary. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
#define BT_ATT_ERR_UNLIKELY 14
#define BT_CONN_STATE_CONNECTED 2
#define R2_TIMING_GM_CONTROL 32
#define LT_MTU_DONE 2
#define LT_DISCOVERED 3
#define BT_GATT_ITER_STOP 0
struct bt_conn {uint16_t mtu;unsigned state;int info_error;};
struct bt_conn_info {unsigned state;};
struct bt_gatt_exchange_params {int unused;};
struct bt_gatt_read_params {struct {uint16_t start_handle;} by_uuid;};
struct lt_event {unsigned type;struct bt_conn *conn;uint8_t error;uint16_t handle;};
static struct lt_event event;
static unsigned calls,notes,noted_mtu,noted_live;
static uint16_t bt_gatt_get_mtu(struct bt_conn *c){return c->mtu;}
static int bt_conn_get_info(struct bt_conn *c,struct bt_conn_info *i){i->state=c->state;return c->info_error;}
static void lt_event_put(struct lt_event *e){event=*e;++calls;}
static void r2_timing_event(unsigned k,int r,uint64_t p,uint32_t d,uint32_t a,uint32_t b){
 assert(k==32&&r&&p==0&&d==21);++notes;noted_mtu=a;noted_live=b;
}
#include "rc_mtu_under_test.h"
int main(void){
 struct bt_conn c={.mtu=247,.state=2};
 lt_mtu_done(&c,0,NULL);assert(!event.error&&event.conn==&c&&calls==1&&!notes);
 lt_mtu_done(&c,14,NULL);assert(!event.error&&notes==1&&noted_mtu==247&&noted_live);
 c.mtu=RC_DYNAMIC_CONTROL_BYTES+2;lt_mtu_done(&c,14,NULL);assert(event.error==14&&notes==2);
 lt_mtu_done(&c,0,NULL);assert(event.error==14);
 c.mtu=RC_DYNAMIC_CONTROL_BYTES+3;lt_mtu_done(&c,0,NULL);assert(!event.error);
 c.state=0;lt_mtu_done(&c,14,NULL);assert(event.error==14&&!noted_live);
 lt_mtu_done(&c,0,NULL);assert(event.error==14);
 c.state=2;c.info_error=-1;lt_mtu_done(&c,14,NULL);assert(event.error==14&&!noted_live);
 c.info_error=0;lt_mtu_done(&c,5,NULL);assert(event.error==5);
 struct bt_gatt_read_params read={.by_uuid={.start_handle=42}};
 gm_located(&c,14,&read,NULL,0);assert(event.error==14);
 gm_located(&c,5,&read,NULL,0);assert(event.error==5);
 gm_located(&c,0,&read,NULL,0);assert(event.error==1);
 gm_located(&c,0,&read,"R2G1\1",5);assert(!event.error&&event.handle==42);
 puts("PASS actual MTU callback: live negotiated>=control+3 only; insufficient/disconnected/unknown/hard errors refused; no retry or deadline change");
}
