#include "control_state.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace v12;
static DirectBinding link(){DirectBinding b;b.authenticated=b.encrypted=b.direct=true;
  b.self=2;b.coordinator=4;b.role=LinkRole::GM;b.boot=11;b.coordinator_boot=12;b.challenge=13;b.config=14;
  for(unsigned i=0;i<16;++i){b.receive_key[i]=uint8_t(i+1);b.send_key[i]=uint8_t(i+17);}return b;}
static v12_control_record message(unsigned kind,uint64_t sequence,uint64_t decision){
  v12_control_record m{};m.kind=kind;m.sender=4;m.target=m.root=2;m.boot=m.coordinator_boot=12;
  m.challenge=13;m.config_epoch=14;m.root_boot=11;m.source_epoch=15;m.sequence=sequence;m.decision=decision;
  m.created_us=m.evidence_us=1000000;m.expires_us=1250000;m.permit_until_us=4000000;return m;}
static bool receive(ControlState& s,v12_control_record m,uint64_t remote=1100000,uint64_t local=9000000){
  uint8_t bytes[110];assert(!v12_control_encode(bytes,&m,link().receive_key));return s.receive(bytes,110,remote,local);}
int main(){
  ControlState s;auto b=link();b.direct=false;assert(!s.bind(b));assert(s.bind(link()));
  assert(!receive(s,message(2,1,1))); // no measured bounds, not eligible
  assert(s.refused==1&&s.replay==1);s.bounds={true,true,true,true,true,15,10,5000,20};
  assert(!receive(s,message(2,1,1)));assert(s.replays==1);
  assert(receive(s,message(2,2,1)));assert(s.phase==ControlPhase::Prepared);
  auto ready=message(3,1,1);assert(!s.queue(ready,1100000));
  assert(!s.preparation_complete(1,false)&&s.preparation_complete(1,true));assert(s.queue(ready,1100000));
  assert(receive(s,message(4,3,1)));assert(s.phase==ControlPhase::Committed);
  auto active=message(5,1,1);assert(!s.queue(active,1100000));assert(!s.started(1,true,false,9000000));
  assert(s.started(1,true,true,9000000));assert(s.queue(active,1100000));
  const uint64_t deadline=s.stop_at;assert(deadline<9000000+2900000&&s.original_until==4000000);
  s.poll(deadline);assert(s.phase==ControlPhase::Stopping&&s.retired==1);
  assert(!s.stopped(false,true)&&!s.stopped(true,false)&&s.stopped(true,true));
  auto stopped=message(7,1,1);assert(s.queue(stopped,1100000));assert(!receive(s,message(2,4,1)));
  assert(receive(s,message(2,5,2)));assert(s.preparation_complete(2,true));
  auto bad=message(4,6,2);bad.permit_until_us=4000001;assert(!receive(s,bad));
  bad=message(4,7,2);bad.root_boot=99;assert(!receive(s,bad));
  bad=message(4,8,2);bad.config_epoch=99;assert(!receive(s,bad));
  bad=message(4,9,2);assert(!receive(s,bad,1250000)); // TTL inclusive expiration
  assert(receive(s,message(4,10,2)));assert(s.phase==ControlPhase::Committed);
  assert(receive(s,message(6,11,2)));assert(s.phase==ControlPhase::Stopping);
  assert(s.stopped(true,true));ControlState::Queued q;assert(!s.pop(q,2000000)&&s.queue_expired==3);
  stopped.decision=2;assert(s.queue(stopped,2100000));s.fence();assert(!s.outbound.count&&!s.binding.authenticated);
  b=link();b.role=LinkRole::TimeRelay;assert(s.bind(b));assert(!receive(s,message(2,12,3)));
  assert(!s.queue(message(1,1,0),1000000));
  assert(s.bind(link()));s.bounds={true,true,true,true,true,15,10,5000,20};
  s.send_sequence=UINT64_MAX;assert(!s.queue(message(1,1,0),1000000));
  std::puts("PASS direct control110: unknown-bound refusal, role/boot/config/challenge/TTL/replay, explicit prepare/start/drain receipts before ACK, original permission3s ceiling/conservative stop, disconnect queue fence and sequence exhaustion");
}
