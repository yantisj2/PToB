#pragma once
#include <cstdint>
namespace display_pages {
enum Page : uint8_t { Front, Relative, Hold, Clock, BleRx, PtsRx,
                     Packets, Quality, Network, LoraRx, Sources, Diagnostics, PtsFtm, System, Gps, Count };
// Keep diagnostic IDs stable, but retire the old Bluetooth network/C4 pages
// from normal navigation. The production SYSTEM page carries local identity and route capability.
#if defined(V12_R2_ENABLED) && V12_R2_ENABLED && V12_R2_CONFIG == 13
constexpr Page Visible[]={Front,Relative,Hold,Clock,BleRx,Packets,Quality,System,Gps};
#elif defined(W12_M5STICK_S3)
// Eight Go pages; no absent peripheral controls. Gps is external PPS diagnostics.
constexpr Page Visible[]={Front,Relative,Hold,Clock,BleRx,Quality,System,Gps};
#else
constexpr Page Visible[]={Front,Relative,Hold,Clock,BleRx,PtsRx,PtsFtm,
                          Packets,Quality,LoraRx,Sources,Gps,System};
#endif
constexpr uint8_t VisibleCount=sizeof(Visible)/sizeof(Visible[0]);
constexpr Page next(uint8_t page){
 for(unsigned i=0;i<VisibleCount;++i)if(Visible[i]==page)return Visible[(i+1)%VisibleCount];
 return Front;
}
}
