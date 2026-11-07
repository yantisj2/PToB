/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual UI getter and due predicate, synthetic main-owned state. */
#include <stdio.h>
static unsigned projected;
#include "ui_under_test.c"
#include "baseline_due.h"
static uint32_t random_state=0x12345abc;
static uint32_t next(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static uint64_t wide(void){return((uint64_t)next()<<32)|next();}
int main(void){
 uint64_t edges[]={0,1,19,20,21,999,1000,(UINT64_MAX-1000)/1000,UINT64_MAX/1000,UINT64_MAX};
 for(unsigned i=0;i<100000;i++){
  uint64_t now=i<10?edges[i]:wide(),owner_until=wide(),due=wide();
  bool valid=(next()&1)!=0;
  model_context=next();model_hint=next();
  atomic_set(&context_revision,(int)next());atomic_set(&source_hint,(int)next());
  if(i%3)atomic_set(&context_revision,(int)model_context);
  if(i%5)atomic_set(&source_hint,(int)model_hint);
  if(i%11==0)atomic_set(&context_revision,-1);
  model.ble_valid_until_us=wide();model.profile_rc=(next()&1)!=0;model.ble_local_holdover=(next()&1)!=0;
  if(i%7==0){now=i;model.ble_valid_until_us=now*1000+1000;owner_until=model.ble_valid_until_us+(i%3);due=now+20;}
  if(i%13==0){now=i;model.ble_valid_until_us=now*1000;owner_until=model.ble_valid_until_us-1;due=now+1;}
  logic_due_ms=due;unsigned before=ui_mutex_calls;
  bool expected=baseline_logic_due(now,owner_until,valid);uint64_t after=logic_due_ms;
  assert(ui_mutex_calls==before+1);
  logic_due_ms=due;before=ui_mutex_calls;
  assert(r2_ui_logic_due(now,owner_until,valid)==expected&&logic_due_ms==after);
  assert(ui_mutex_calls==before);
  status=(struct r2_ui_status){.error=(int32_t)next(),.max_frame_ms=next(),.missed=next()};
  fake_now=i;model.sampled_ms=i;model_serial=1;logic_revision=1;
  int error;uint32_t maximum,missed;
  unsigned old_projected=projected;before=ui_mutex_calls;
  r2_ui_diag_snapshot(&error,&maximum,&missed);
  assert(ui_mutex_calls==before+1&&projected==old_projected);
  struct r2_ui_status full;r2_ui_snapshot(&full);
  assert(error==full.error&&maximum==full.max_frame_ms&&missed==full.missed);
  assert(projected==old_projected+1);
 }
 puts("PASS100000 exact due/cadence/change/expiry/HOLD cases with0mutex acquisitions");
 puts("PASS100000 narrow getter values equal full snapshots, same1lock,0logical projections");
 return 0;
}
