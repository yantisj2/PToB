/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis. Actual RC7 receiver/codec regression. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
int main(void) {
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
