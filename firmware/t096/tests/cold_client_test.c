/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual CONTROL publication slice; admission producer/PT consumer are faked.
 * No permission, RF scheduling, source lifetime or HOT authority is invented. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cold_client_types.inc"
#define R2_GM5_COLD 0
static struct {struct {bool enabled,bridge;unsigned role;}gm5;}net;
static int roster;static uint64_t now;
static struct r2_group_client admitted,delivered;
static void r2_group_admission_client(struct r2_group_client *p){*p=admitted;}
static void r2_radio_service_client(const struct r2_group_client *p,const int *r,uint64_t at){assert(r==&roster&&at==now);delivered=*p;}
static void publish(void){struct r2_group_client public;
#include "cold_client_publish.inc"
}
int main(void){
 for(unsigned role=0;role<4;role++)for(unsigned bridge=0;bridge<2;bridge++)for(unsigned source=0;source<2;source++){
  net.gm5.enabled=true;net.gm5.role=role;net.gm5.bridge=bridge;now=1000000;
  admitted=(struct r2_group_client){.passive=role==0,.source=source,.root=111,.boot=22,.peer=111,.peer_boot=22,.source_epoch=7,.publication=8,.at=999000,.until=1500000,.map_until=1400000,.utc_us=123456789,.uncertainty_us=40000};
  publish();assert(!memcmp(&admitted,&delivered,sizeof admitted));
  /* Appointment loss cannot erase an independently proved receive scope. */
  net.gm5.role=0;net.gm5.bridge=false;publish();assert(!memcmp(&admitted,&delivered,sizeof admitted));
  /* Expired originals remain expired; the PT owner must reject them. */
  admitted.until=now;publish();assert(delivered.until==now&&delivered.at==admitted.at);
 }
 puts("PASS actual CONTROL publication preserves all COLD timing anchors, role transitions and original expiry without HOT/LoRa dependency");
}
