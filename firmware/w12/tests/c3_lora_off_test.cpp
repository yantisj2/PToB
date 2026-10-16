#define V12_LORA_FORCED_OFF 1
#include "../main/native_lora.cpp"
#include <cassert>
int main(){assert(v12::lora_start_worker());auto s=v12::lora_status();assert(!s.enabled&&!s.initialized&&!s.requested&&!s.worker_busy&&s.off_acknowledged);assert(!v12::lora_request(true,1000));assert(v12::lora_request(false,0));s=v12::lora_status();assert(s.request_generation==1&&s.off_ack_generation==1&&s.off_acknowledged&&!s.failed);}
