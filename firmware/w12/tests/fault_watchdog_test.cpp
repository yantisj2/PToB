#include "fault_watchdog_model.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

using Model=v12::FaultWatchdogModel;
constexpr uint64_t D=Model::DelayUs;
constexpr uint64_t Max=std::numeric_limits<uint64_t>::max();
unsigned cases=0;

int main(){
  static_assert(D==120000000);
  {
    Model m;assert(!m.active()&&!m.due(0)&&!m.take_restart(0));
    assert(!m.current_mask()&&!m.first_mask()&&!m.start_us());++cases;
  }
  {
    // Timestamp zero is a valid first fault, not an unset sentinel.
    Model m;m.observe(0,1);assert(m.active()&&m.start_us()==0&&m.first_mask()==1);
    assert(!m.due(D-1)&&!m.take_restart(D-1));
    assert(m.due(D)&&m.take_restart(D));++cases;
  }
  {
    Model m;m.observe(1234567,0x20);m.observe(1234567+D-1,0x20);
    assert(!m.due(1234567+D-1));assert(m.take_restart(1234567+D));++cases;
  }
  {
    // Multiple fault owners can change while the aggregate never recovers.
    Model m;m.observe(100,1);m.observe(40000000,3);m.observe(80000000,2);
    assert(m.first_mask()==1&&m.current_mask()==2&&m.start_us()==100);
    assert(!m.take_restart(100+D-1)&&m.take_restart(100+D));++cases;
  }
  {
    Model m;m.observe(10,1);m.observe(10+D-1,0);
    assert(!m.active()&&!m.first_mask()&&!m.current_mask()&&!m.start_us());
    assert(!m.due(10+D)&&!m.take_restart(10+D));++cases;
  }
  {
    // Recovery just before the boundary requires a new full fault interval.
    Model m;m.observe(0,1);m.observe(D-1,0);m.observe(D,4);
    assert(m.first_mask()==4&&m.start_us()==D);
    assert(!m.take_restart(2*D-1)&&m.take_restart(2*D));++cases;
  }
  {
    // A sampled healthy aggregate also cancels an already due, untaken timer.
    Model m;m.observe(20,1);assert(m.due(20+D));m.observe(20+D,0);
    assert(!m.take_restart(20+D));++cases;
  }
  {
    Model m;m.observe(0,0);m.observe(D*100,0);m.observe(Max,0);
    assert(!m.active()&&!m.timing_fault()&&!m.take_restart(Max));++cases;
  }
  {
    Model m;m.observe(0,0);m.observe(6000000000,8);
    assert(!m.take_restart(6000000000+D-1));assert(m.take_restart(6000000000+D));++cases;
  }
  {
    Model m;m.observe(1,1);assert(m.take_restart(1+D));
    assert(m.restart_taken()&&!m.due(1+D)&&!m.take_restart(1+D+1));
    m.observe(2+D,0);m.observe(3+D,2);
    assert(!m.take_restart(3+2*D));++cases;
  }
  {
    Model m;m.observe(100,1);m.observe(99,2);
    assert(m.timing_fault()&&m.first_mask()==1&&m.current_mask()==2);
    assert(!m.due(100+D)&&!m.take_restart(100+D));++cases;
  }
  {
    // A backwards restart query is checked even without an observe call.
    Model m;m.observe(100,1);assert(!m.take_restart(99));
    assert(m.timing_fault()&&!m.take_restart(100+D));++cases;
  }
  {
    Model m;m.observe(100,1);assert(!m.due(99)&&!m.timing_fault());
    assert(m.take_restart(100+D));++cases;
  }
  {
    // The highest valid boundary works without overflowing start+DelayUs.
    Model m;m.observe(Max-D,16);assert(!m.take_restart(Max-1));
    assert(m.take_restart(Max));++cases;
  }
  {
    Model m;m.observe(Max-D+1,16);assert(!m.take_restart(Max));
    m.observe(0,16);assert(m.timing_fault()&&!m.take_restart(D));++cases;
  }
  {
    Model m;m.observe(Max-1,1);m.observe(Max,1);m.observe(0,1);
    assert(m.timing_fault()&&!m.due(D)&&!m.take_restart(D));++cases;
  }
  {
    Model m;m.observe(500,1);m.observe(499,0);
    assert(!m.active()&&m.timing_fault()&&!m.first_mask());
    m.observe(600,2);assert(!m.take_restart(600+D));++cases;
  }
  {
    // Equal timestamps are legal; a recovery/re-fault at one timestamp is a
    // newly observed fault interval, not a clock regression.
    Model m;m.observe(500,1);m.observe(500,0);m.observe(500,2);
    assert(!m.timing_fault()&&m.first_mask()==2&&m.start_us()==500);
    assert(m.take_restart(500+D));++cases;
  }
  {
    Model m;m.observe(0,UINT32_MAX);m.observe(D/2,UINT32_C(0x80000000));
    assert(m.first_mask()==UINT32_MAX&&m.current_mask()==UINT32_C(0x80000000));
    assert(m.take_restart(D));++cases;
  }
  std::printf("{\"status\":\"PASSED\",\"cases\":%u,\"model_bytes\":%zu,\"delay_us\":%llu}\n",
    cases,sizeof(Model),static_cast<unsigned long long>(D));
}
