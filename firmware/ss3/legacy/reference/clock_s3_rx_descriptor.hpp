#pragma once
#include <cstdint>
// Pinned ESP32-S3 ROM/controller layout, not the original ESP32 layout.
// ROM r_lld_con_rx_sync_time_update and r_lld_scan_process_pkt_rx_aux_adv_ind
// consume RX descriptor +8/+10 as a 28-bit half-slot clock, and +12 as a
// descending half-microsecond sync counter. Legacy-advertising validity is
// verified by the 2026-09-28 shadow trial (704/704 packet matches). Absolute
// RF front-end delay still requires an independent phase reference.
namespace adv_rx {
constexpr uint32_t BleClockMask=0x0fffffff;
struct BleTime {uint32_t half_slots=0,half_us=0;};
static_assert(sizeof(BleTime)==8,"S3 ROM returns the clock in a2/a3");
struct BleAnchor {uint64_t before=0,after=0;BleTime time{};};
inline bool descriptor_map(const BleAnchor& a,uint32_t coarse,uint16_t fine,
                           uint64_t& tick,uint32_t& width_us) {
    if(!a.before || a.after<a.before || a.after-a.before>400 ||
       a.time.half_slots>BleClockMask || a.time.half_us>624 ||
       coarse>BleClockMask || (fine&1023)>624)return false;
    const uint32_t wrap=(coarse-a.time.half_slots)&BleClockMask;
    const int64_t slots=wrap&(1u<<27)?int64_t(wrap)-(1LL<<28):wrap;
    const int64_t delta=slots*625+624-(fine&1023)-a.time.half_us;
    if(delta < -500000 || delta>500000)return false;
    const int64_t mapped=int64_t(a.before+(a.after-a.before)/2)+delta*10;
    if(mapped<=0)return false;
    tick=uint64_t(mapped);width_us=uint32_t((a.after-a.before+19)/20)+1;return true;
}
}
