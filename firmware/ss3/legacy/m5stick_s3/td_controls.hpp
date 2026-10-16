#pragma once
#include <cstdint>
namespace sticks3 {
enum class HoldAction { None, Mode, Chart, Request };
constexpr HoldAction hold_action(bool td,uint8_t page){
 return page==0?HoldAction::Mode:!td?HoldAction::None:page==1?HoldAction::Chart:page==2?HoldAction::Request:HoldAction::None;
}
constexpr uint8_t next_chart(uint8_t chart){return (chart+1)%3;}
constexpr uint8_t PanelMadctl=0x08; // Stick ST7789 BGR order; portrait geometry unchanged.
}
