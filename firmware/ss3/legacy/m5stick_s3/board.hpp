#pragma once
#include <cstdint>
// K150 / schematic V0.6 (2025-11-11), pinned reference hashes in build manifest.
namespace sticks3 {
constexpr int Width=135, Height=240, XOffset=52, YOffset=40;
constexpr int LcdMosi=39, LcdClock=40, LcdDc=45, LcdCs=41, LcdReset=21, Backlight=38;
constexpr int Key1=11, Key2=12, PowerSda=47, PowerScl=48, PowerAddress=0x6e;
constexpr int Pps=6; // Official Hat2-Bus position 6; schematic CN2 contact 14.
constexpr uint64_t bit(int pin) { return uint64_t{1}<<pin; }
constexpr uint64_t LcdMask=bit(LcdMosi)|bit(LcdClock)|bit(LcdDc)|bit(LcdCs)|bit(LcdReset)|bit(Backlight);
constexpr uint64_t KeyMask=bit(Key1)|bit(Key2), PowerMask=bit(PowerSda)|bit(PowerScl);
static_assert(!(LcdMask&KeyMask) && !(LcdMask&PowerMask) && !(KeyMask&PowerMask));
static_assert(!(bit(Pps)&(LcdMask|KeyMask|PowerMask|bit(0)|bit(19)|bit(20))));
constexpr unsigned FrameBytes=Width*Height*2, StripRows=16, StripBytes=Width*StripRows*2;
constexpr unsigned FramePeriodMs=20, KeySampleMs=5;
static_assert(Height%StripRows==0 && StripBytes==4320);
struct Window {uint8_t x[4],y[4];bool valid;};
constexpr Window address_window(unsigned first,unsigned rows) {
 const unsigned x0=XOffset,x1=XOffset+Width-1,y0=YOffset+first,y1=y0+rows-1;
 return {{uint8_t(x0>>8),uint8_t(x0),uint8_t(x1>>8),uint8_t(x1)},
         {uint8_t(y0>>8),uint8_t(y0),uint8_t(y1>>8),uint8_t(y1)},
         rows && first<Height && rows<=Height-first};
}
}
