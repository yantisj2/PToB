#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
struct r2_radio_scope {uint64_t until_us;uint32_t source_epoch;bool gm,follower;};
static struct {struct r2_radio_scope grant;bool stopped,source_ready;uint32_t source_epoch;
 struct {bool passive,source;uint64_t at,uncertainty_us;} client;} pt_input;
static unsigned pt_lock,depth,steps;
typedef unsigned k_spinlock_key_t;
static unsigned k_spin_lock(unsigned *p){(void)p;assert(!depth);return depth++;}
static void k_spin_unlock(unsigned *p,unsigned k){(void)p;depth=k;}
#define K_FOREVER -1
static int k_mutex_lock(unsigned *p,int wait){(void)p;(void)wait;return 0;}
static int k_mutex_unlock(unsigned *p){(void)p;return 0;}
static uint64_t r2_group_anchor_deadline(uint64_t at,uint64_t uncertainty){return at+1000-uncertainty;}
static void precision_step(uint64_t now){(void)now;assert(!depth);++steps;}
#include "precision_gate_under_test.h"
int main(void){
 bool running=false;
 for(unsigned i=0;i<100;i++)precision_poll(i,&running);
 assert(!steps&&!running);
 pt_input.grant=(struct r2_radio_scope){.gm=true,.until_us=1000,.source_epoch=4};
 precision_poll(100,&running);assert(!steps);
 pt_input.source_ready=true;pt_input.source_epoch=3;precision_poll(100,&running);assert(!steps);
 pt_input.source_epoch=4;precision_poll(100,&running);assert(steps==1&&running);
 precision_poll(1000,&running);assert(steps==2&&!running);
 precision_poll(1001,&running);assert(steps==2);
 pt_input.grant=(struct r2_radio_scope){.follower=true,.until_us=2000};pt_input.source_ready=false;
 precision_poll(1002,&running);assert(steps==3&&running);
 pt_input.stopped=true;precision_poll(1003,&running);assert(steps==4&&!running);
 precision_poll(1004,&running);assert(steps==4);
 pt_input.stopped=false;pt_input.client.passive=true;pt_input.client.at=2000;
 precision_poll(1999,&running);assert(steps==4);
 pt_input.client.source=true;precision_poll(1999,&running);assert(steps==4);
 precision_poll(2000,&running);assert(steps==5&&running);
 precision_poll(3000,&running);assert(steps==6&&!running);
 precision_poll(3001,&running);assert(steps==6);
 puts("precision gate: no premature work; valid GM/follower/acquisition; epoch/expiry/stop; exactly one withdrawal PASS");
}
