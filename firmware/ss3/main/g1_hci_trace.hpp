#pragma once
#include <cstdint>
namespace v12 {
struct G1HciTrace {uint64_t at_us=0;uint32_t sequence=0,duration_us=0;int32_t result=0;uint16_t opcode=0;uint8_t kind=0,event=0;};
struct G1HciStatus {uint64_t pending_at_us=0;uint32_t commands=0,acks=0,sync_events=0,dropped=0,max_us=0;uint16_t pending_opcode=0;};
bool g1_hci_pop(G1HciTrace&);
G1HciStatus g1_hci_status();
}
