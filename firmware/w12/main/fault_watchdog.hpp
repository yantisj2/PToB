#pragma once
#include <cstdint>
namespace v12 {
enum ClockFault:uint32_t {
  FaultRx=1,FaultTx=2,FaultDirect=4,FaultLoRa=8,FaultR2=16,FaultUpdate=32,
  FaultInit=64,FaultMainStalled=65536,FaultHostStalled=131072,FaultTest=262144
};
struct FaultWatchdogStatus {
  uint64_t since_us=0,elapsed_us=0,previous_boot=0,previous_elapsed_us=0,parent=0;
  uint32_t mask=0,first_mask=0,previous_mask=0,restarts=0;
  uint8_t previous_api=0;int32_t previous_rc=0;
  bool enabled=false,parent_relay=false,parent_pending=false,test=false;
};
bool fault_watchdog_start(const uint8_t factory[6],uint64_t boot);
void fault_watchdog_fatal();
void fault_watchdog_main_progress(uint32_t mask,uint8_t api,int32_t rc,
                                  uint64_t applied_parent,bool relay,bool parent_applied);
void fault_watchdog_host_expected();
#ifdef ESP_PLATFORM
void fault_watchdog_host_progress();
#else
inline void fault_watchdog_host_progress(){} // synthetic owner fixtures have no supervisor
#endif
FaultWatchdogStatus fault_watchdog_status();
bool fault_watchdog_test();
void fault_watchdog_parent_restored();
}
