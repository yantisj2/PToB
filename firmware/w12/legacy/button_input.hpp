#pragma once
#include <cstdint>

// GPIO0 is sampled by a small task so OLED transfers cannot hide a short tap.
class ButtonInput {
public:
    enum Action : uint8_t { None, NextPage, Sleep, ToggleTx, ToggleBleRx, TogglePts, Home, ToggleHoldPercentile, ToggleRelayOnly };
    static constexpr uint64_t DebounceTicks=20000000ULL/25; // 40 ms
    static constexpr uint64_t SleepTicks=20000000ULL*3;
    static constexpr uint64_t ToggleTicks=20000000ULL*3/2;

    explicit ButtonInput(bool initially_pressed):raw_(initially_pressed),
        stable_(initially_pressed),armed_(!initially_pressed) {}

    Action update(bool pressed,uint64_t now,bool lora_page=false,
                  bool ble_page=false,bool pts_page=false,bool home_page=false,bool dashboard_page=false,bool relay_dashboard=false) {
        if(pressed!=raw_){raw_=pressed;changed_=now;}
        if(raw_==stable_ || now<changed_ || now-changed_<DebounceTicks)return None;
        stable_=raw_;
        if(stable_){if(armed_)down_=changed_;return None;}
        if(!armed_){armed_=true;down_=0;return None;}
        if(down_ && !slept_ && now-down_>=
           ((lora_page||ble_page||pts_page||home_page||dashboard_page||relay_dashboard)?ToggleTicks:SleepTicks)){
            down_=0;slept_=false;
            return relay_dashboard?ToggleRelayOnly:dashboard_page?ToggleHoldPercentile:lora_page?ToggleTx:ble_page?ToggleBleRx:pts_page?TogglePts:home_page?Home:Sleep;
        }
        const bool tap=down_ && now-down_<SleepTicks && !slept_;
        down_=0;slept_=false;
        return tap?NextPage:None;
    }
    Action held(uint64_t now,bool lora_page=false,bool ble_page=false,bool pts_page=false,bool home_page=false,bool dashboard_page=false,bool relay_dashboard=false) {
        if((lora_page||ble_page||pts_page||home_page||dashboard_page||relay_dashboard) && armed_ && stable_ && down_ && !slept_ &&
           now>=down_ && now-down_>=ToggleTicks){
            slept_=true;return relay_dashboard?ToggleRelayOnly:dashboard_page?ToggleHoldPercentile:lora_page?ToggleTx:ble_page?ToggleBleRx:pts_page?TogglePts:Home;
        }
        if(armed_ && stable_ && down_ && !slept_ && now>=down_ &&
           now-down_>=SleepTicks){slept_=true;
            return lora_page?ToggleTx:ble_page?ToggleBleRx:pts_page?TogglePts:home_page?Home:Sleep;}
        return None;
    }
private:
    bool raw_=false,stable_=false,armed_=true,slept_=false;
    uint64_t changed_=0,down_=0;
};
