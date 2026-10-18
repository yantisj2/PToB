/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis. Actual RC7 receiver/codec regression. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
#include "rc_admission_policy.h"
static void gm_renewal_retry_headroom(void) {
  const uint64_t ticks_per_us=16;
  const uint64_t nonces[]={0,20000000,1926000,8921000};
  for(unsigned run=0;run<sizeof nonces/sizeof nonces[0];++run){
    struct rc5_state clock={0};
    struct rc5_admission a={.parent_boot=2,.root_boot=2,.root_epoch=3,
      .parent_train=1,.parent_epoch=4,.now_tick=100000,
      .root_lo_ns=999900000,.root_hi_ns=1000100000,
      .root_expires_ns=2500000000};
    assert(rc5_bind(&clock,&a));
    const uint64_t original_until=clock.context_until;
    uint64_t now=a.now_tick+rc_admission_gm_renew_ticks(),starts[2]={0};
    /* Two failed original2s transactions, including their500ms cancel drain.
     * Apply the production warm backoff and rolling-start limiter. Neither
     * failure, polling nor the scheduled third start renews the old context. */
    for(unsigned failure=0;failure<2;++failure){
      while(!rc_admission_start_allowed(starts,now))now+=16000;
      assert(now<original_until);
      starts[0]=starts[1];starts[1]=now;
      now+=UINT64_C(2500000)*ticks_per_us;
      now+=rc_admission_warm_retry_us(nonces[run])*ticks_per_us;
      assert(clock.context_until==original_until);
    }
    while(!rc_admission_start_allowed(starts,now))now+=16000;
    /* The next complete2s challenge can finish BEFORE the unmodified expiry.
     * The former300s renewal misses this boundary even at minimum backoff. */
    assert(now+UINT64_C(2000000)*ticks_per_us<original_until);
    assert(starts[0]&&starts[1]&&now-starts[0]>=UINT64_C(960000000));
    assert(now-starts[1]>=UINT64_C(320000000));
    assert(clock.context_until==original_until);
  }
}
int main(void) {
  gm_renewal_retry_headroom();
  struct rc7_path path={.source_epoch=3,.node={{.id=1,.boot=2,.train=1,.epoch=4}}};
  struct rc5_admission a={.parent_boot=2,.root_boot=2,.root_epoch=3,
    .parent_train=1,.parent_epoch=4,.now_tick=100000,
    .root_lo_ns=999900000,.root_hi_ns=1000100000,.root_expires_ns=2500000000};
  struct rc7_receiver s={0};
  assert(rc7_bind(&s,&path,5,true,1,&a));
  struct r2_group_config g={.psk={1},.id=7,.epoch=1};
  struct rc7_packet p={.origin={.root=1,.boot=2,.epoch=3,.issued=1000000000,
    .expires=2500000000},.sender=1,.boot=2,.train=1,.epoch=4,.publication=1};
  assert(!rc7_origin_sign(p.proof,&p.origin,&g));
  uint8_t wire[190];assert(!rc7_encode(wire,&p,&path,&g));
  struct rc7_packet decoded;assert(!rc7_decode(&decoded,wire,sizeof wire,&path,&g));
  assert(!rc7_receive(&s,&decoded,100000,10,2,100001));
  struct rc7_receiver saved=s;
  /* Invalid new scope previously cold-reset the receiver before refusal. */
  struct rc7_path other=path;other.node[0].boot=8;
  struct rc5_admission bad=a;bad.parent_boot=bad.root_boot=8;
  bad.root_expires_ns=bad.root_hi_ns;
  assert(!rc7_bind(&s,&other,5,true,1,&bad)&&!memcmp(&s,&saved,sizeof s));
  bad=a;bad.now_tick=UINT64_MAX;
  assert(!rc7_bind(&s,&path,5,true,1,&bad)&&!memcmp(&s,&saved,sizeof s));
  /* A higher renewal floor fences new originals, not the pending original. */
  a.now_tick=110000;a.root_lo_ns+=625000;a.root_hi_ns+=625000;a.publication_floor=2;
  assert(rc7_bind(&s,&path,5,true,1,&a));
  assert(s.clock.pending[0].used&&s.clock.pending[0].rx==100000);
  p.publication=2;p.count=1;p.correction[0]=(struct rc5_correction){
    .publication=1,.event=10,.channel=2,.generation=1,.tx_ns=1000000000,.bound=RC5_UNKNOWN};
  assert(rc7_receive(&s,&p,120000,11,2,120001));
  assert(s.clock.reference.rx==100000&&s.clock.accepted==1);
  assert(!rc7_receive(&s,&p,120000,11,2,120002)&&s.clock.accepted==1);
  uint64_t until=s.clock.context_until;
  assert(!rc7_receive(&s,&p,until,12,2,until)); /* No polling lease. */
  saved=s;bad=a;bad.publication_floor=1;
  assert(!rc7_bind(&s,&path,5,true,1,&bad)&&!memcmp(&s,&saved,sizeof s));
  assert(!rc7_encode(wire,&p,&path,&g));wire[174]^=1;
  assert(rc7_decode(&decoded,wire,sizeof wire,&path,&g));
  /* Real boot/capture/train replacement still fences old pending tuples. */
  other=path;++other.node[0].epoch;bad=a;++bad.parent_epoch;
  assert(rc7_bind(&s,&other,5,true,1,&bad)&&!s.clock.reference.valid);
  assert(!rc7_receive(&s,&p,130000,13,2,130001));
  puts("PASS actual RC7 renewal: invalid-scope transaction, pending pairing/replay, original deadline, MAC and capture fences");
}
