#pragma once
#include <cstdint>
namespace v12 {
// Volatile diagnostic locator policy; never an authenticated path or lease.
struct RcParentDiagnostic {
  uint64_t target=0,deadline_us=0,completed_us=0;
  uint32_t generation=0;uint8_t state=0; // auto/queued/draining/applied/failed
  bool relay=true; // Named locator kind; auto retains the compiled policy.
  bool pending()const{return state==1||state==2;}
  bool admit(uint64_t id,uint64_t now,bool relay_target=true){
    if(pending()||state==4||generation==UINT32_MAX||now>UINT64_MAX-2000000)return false;
    target=id;relay=relay_target;deadline_us=now+2000000;completed_us=0;++generation;state=1;return true;
  }
  bool observe(uint64_t now){if(pending()&&now>=deadline_us){state=4;return true;}return false;}
};
}
