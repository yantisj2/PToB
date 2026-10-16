#include "console_protocol.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
int main(){
  using namespace v12;uint64_t id=0;
  assert(hex_id("0123456789abcdef",id)&&id==0x0123456789abcdefULL);
  assert(!hex_id("0000000000000000",id));assert(!hex_id("0123456789abcdeF",id));assert(!hex_id("1",id));
  assert(!hex_id("0123456789abcdef0",id));
  ConsoleTicket t;const auto first=t.issue(10);assert(first==1);
  assert(!t.consume(2,11));assert(!t.consume(1,9));assert(t.consume(1,11));assert(!t.consume(1,12));
  auto next=t.issue(100);assert(!t.consume(next,100+ConsoleTicketUs));
  auto third=t.issue(200);assert(!t.consume(next,201));assert(t.consume(third,201));
  t.generation=UINT64_MAX;assert(!t.issue(300)&&t.exhausted);
  OwnerLease q;assert(q.admit(1000,8000,0x55));assert(q.deadline_us==8001000&&q.request_id==0x55);
  assert(!q.admit(1001,8000,0x56));assert(q.start(4000000));assert(q.deadline_us==8001000);assert(!q.start(4000001));assert(q.started_us==4000000);
  q.stop_request();assert(q.stop_completed!=q.stop_generation);q.stopped(7000000);assert(q.stop_completed==q.stop_generation&&!q.deadline_exceeded);q.stop_request();q.stopped(8500000);assert(q.completed_us==7000000&&!q.deadline_exceeded);
  assert(q.admit(9000000,1000,1));assert(!q.start(10000000));assert(q.deadline_exceeded);
  q.stop_request();q.stopped(10000001);assert(q.deadline_exceeded);
  OwnerLease exhausted;exhausted.generation=UINT32_MAX;assert(!exhausted.admit(1,1000,1));
  OwnerLease clock;assert(!clock.admit(UINT64_MAX-1,1000,1));
  assert(console_crc("123456789",9)==0xcbf43926u);
  assert(console_crc("56789",5,console_crc("1234",4))==0xcbf43926u);
  std::puts("PASS real ticket replay/expiry/incarnation counters and original queued owner deadline fixtures");
}
