#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
static uint64_t clock_now;
static uint64_t service_now(void);
static bool advance_after_copy;static unsigned snapshot_reads;
static unsigned pt_lock;
typedef unsigned k_spinlock_key_t;
static unsigned k_spin_lock(unsigned *p){return 0;}
static void k_spin_unlock(unsigned *p,unsigned k){}
#define K_FOREVER -1
static int k_mutex_lock(unsigned *p,int wait){(void)p;(void)wait;++snapshot_reads;return 0;}
static int k_mutex_unlock(unsigned *p){(void)p;return 0;}
struct r2_capture{uint64_t epoch,tick;};
static bool r2_capture_now(struct r2_capture *c){*c=(struct r2_capture){1,110};return true;}
struct r2_radio_precision_view{bool authorized,map_valid,locked;};
struct r2_group_client{bool source;uint64_t utc_us;};
struct pt_output{
 struct {bool gm,follower;uint64_t until_us;uint32_t source_epoch;} scope;
 struct r2_radio_precision_view view;
 struct r2_group_client context,bootstrap;
 uint64_t source_frame;
 uint64_t sampled,valid_until,capture_epoch,capture_tick,capture_until;
 bool utc_valid,acquisition;
};
static struct pt_output pt_output;
static uint64_t service_now(void){if(advance_after_copy){pt_output.context.utc_us=999;pt_output.source_frame=77;}return clock_now;}
static struct {bool stopped,source_ready;uint32_t source_epoch;} pt_input;
#include "precision_snapshot_under_test.h"
int main(void){
 pt_input.source_ready=true;pt_input.source_epoch=3;
 pt_output=(struct pt_output){.scope={true,false,200,3},.view={true,true,true},.context={.source=true},.sampled=110,.valid_until=200,.capture_epoch=1,.capture_tick=100,.capture_until=200,.utc_valid=true};
 struct pt_output out;clock_now=120;output_read(&out,100);assert(out.scope.gm&&out.view.authorized&&out.utc_valid);
 clock_now=200;output_read(&out,100);assert(!out.scope.gm&&!out.view.authorized&&!out.utc_valid);
 clock_now=120;pt_input.stopped=true;output_read(&out,100);assert(!out.scope.gm&&!out.utc_valid);
 pt_input.stopped=false;pt_input.source_epoch=4;output_read(&out,100);assert(!out.scope.gm&&!out.view.authorized);
 pt_input.source_epoch=3;pt_output.scope.gm=false;pt_output.scope.follower=true;
 pt_output.context.utc_us=123;pt_output.bootstrap.source=true;pt_output.source_frame=42;
 struct r2_radio_precision_view view;struct r2_group_client time,coarse;uint64_t frame;
 snapshot_reads=0;advance_after_copy=true;
 r2_radio_service_snapshot(&view,&time,&coarse,&frame,100);
 assert(snapshot_reads==1&&time.source&&time.utc_us==123&&frame==42&&coarse.source);
 assert(pt_output.context.utc_us==999&&pt_output.source_frame==77);
 puts("Coherent UI/status publication survives writer advance after copy PASS");
 puts("post-snapshot time rejects real expiry/stop/epoch loss, preserves concurrently advanced live snapshot PASS");
}
