#pragma once
#include <cstdint>
namespace v12 {
// Persistent client ownership is distinct from a finite diagnostic lease.
// Queued admission and actual controller drain still have original 2s bounds.
// A repeated stop cannot extend the deadline; this ledger never renews RF.
struct R2ClientService {
  uint32_t generation=0,stop_generation=0,stop_completed=0;
  uint64_t request_id=0,admitted_us=0,start_deadline_us=0,started_us=0;
  uint64_t stop_requested_us=0,stop_deadline_us=0,completed_us=0;
  bool pending=false,running=false,stopping=false,deadline_exceeded=false;
  bool admit(uint64_t now,uint64_t id,bool explicit_restart=false){
    if(!id||pending||running||stopping||deadline_exceeded||now>UINT64_MAX-2000000)return false;
    if(generation&&(!explicit_restart||!off()||id==request_id||now<completed_us))return false;
    if(generation==UINT32_MAX)return false;
    ++generation;stop_generation=stop_completed=0;started_us=stop_requested_us=stop_deadline_us=completed_us=0;request_id=id;admitted_us=now;start_deadline_us=now+2000000;pending=true;return true;
  }
  bool start(uint64_t now){
    if(!pending||stopping)return false;
    pending=false;
    if(now<admitted_us||now>=start_deadline_us){deadline_exceeded=true;return false;}
    running=true;started_us=now;return true;
  }
  void stop(uint64_t now){
    if(!generation||completed_us||stopping)return;
    pending=false;stopping=true;stop_generation=generation;stop_requested_us=now;
    if(now>UINT64_MAX-2000000){deadline_exceeded=true;stop_deadline_us=now;}
    else stop_deadline_us=now+2000000;
  }
  bool observe(uint64_t now){
    if(pending&&now>=start_deadline_us){deadline_exceeded=true;stop(now);return true;}
    if(stopping&&!completed_us&&now>=stop_deadline_us)deadline_exceeded=true;
    return false;
  }
  void stopped(uint64_t now){
    if(!generation||completed_us)return;
    if(!stopping)stop(now);
    if(now>stop_deadline_us)deadline_exceeded=true;
    pending=running=stopping=false;completed_us=now;stop_completed=stop_generation;
  }
  bool off()const{return !pending&&!running&&!stopping&&stop_generation==stop_completed&&!deadline_exceeded&&(!generation||completed_us);}
};
}
