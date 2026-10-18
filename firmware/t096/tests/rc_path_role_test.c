/* MIT. Copyright (c) 2026 Jonathan Yantis. Actual layout/extension boundary. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
#include "rc_slots.h"
#define RC_FIELD_HOLD_TICKS UINT64_C(8000000)
static bool transmitter=true,running,phase_admitted,start_prepared,slot_selected,holdover_expired,fit_rms_valid;
static uint64_t self_identity=3001,local_boot=31,restart_at,discipline_at,phase_rx,phase_step_at,phase_log_at;
static uint32_t capture_epoch=4,train=1,sent_count;
static unsigned node,parent_index;
static uint8_t retained_slot,current_proof[16];
static struct rc5_routes routes;
static struct rc5_rate_fit fit;
static struct rc5_clock local_clock;
static struct rc5_origin current_origin;
static struct rc7_path upstream_path,publication_path;
static struct rc_slots slot_state;
static int64_t slot_target_ns,parent_slot_delta_ns;
static bool stop(void){running=false;return true;}
static void check(bool yes,const char *why){(void)why;assert(yes);}
static void r2_shadow_normal_withdraw(void){}
#include "rc_path_role_under_test.h"
static struct rc7_path parent(unsigned depth,unsigned slot){
 struct rc7_path p={.depth=depth,.source_epoch=1};
 p.node[0]=(struct rc7_node){.id=1001,.boot=11,.train=1,.epoch=1,.slot=0,.role=0};
 p.node[1]=(struct rc7_node){.id=2001,.boot=21,.train=1,.epoch=2,.slot=depth==1?slot:1,.role=1};
 if(depth==2)p.node[2]=(struct rc7_node){.id=2501,.boot=25,.train=1,.epoch=3,.slot=slot,.role=1};
 assert(rc7_path_valid(&p));return p;
}
static void reset(void){
 memset(&routes,0,sizeof routes);memset(&upstream_path,0,sizeof upstream_path);node=0;retained_slot=0;slot_selected=false;running=false;transmitter=true;self_identity=3001;
}
int main(void){
 for(unsigned first_slot=1;first_slot<=2;first_slot++){
  reset();struct rc7_path p=parent(1,first_slot);
  assert(parent_path_usable(&p)&&layout_path(&p));
  assert(routes.node[node].transmitter&&routes.node[node].depth==2&&routes.node[node].slot==first_slot+1);
  assert(make_publication_path()&&publication_path.depth==2&&publication_path.node[2].id==self_identity);
  /* The final client accepts the RC2 publisher and cannot add another hop. */
  struct rc7_path rc2=publication_path;reset();self_identity=4001;
  assert(parent_path_usable(&rc2)&&layout_path(&rc2));
  assert(!routes.node[node].transmitter&&routes.node[node].depth==2&&routes.node[node].slot==255);
  assert(!make_publication_path());
 }
 reset();struct rc7_path terminal=parent(1,3);
 assert(parent_path_usable(&terminal)&&layout_path(&terminal));
 assert(!routes.node[node].transmitter&&routes.node[node].slot==255&&!make_publication_path());
 reset();terminal=parent(2,3);assert(parent_path_usable(&terminal)&&layout_path(&terminal));
 assert(!routes.node[node].transmitter&&!make_publication_path());
 reset();terminal=parent(1,1);terminal.node[1].id=self_identity;
 assert(!parent_path_usable(&terminal)&&!layout_path(&terminal));
 puts("PASS actual RC1->RC2 publisher->leaf layouts, both legal RC2 slots, terminal receive-only and unchanged loop refusal");
}
