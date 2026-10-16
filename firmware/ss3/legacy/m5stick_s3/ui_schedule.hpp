#pragma once
#include <cstdint>
namespace sticks3 {
// Display phase only: never renews source/precision/authority.
inline unsigned ui_slot_delay_ms(int64_t ns) {
 constexpr int64_t Period=20000000;
 if(ns<0)return 20;
 return unsigned((Period-ns%Period+999999)/1000000); //1..20ms, no busy wait
}
}
