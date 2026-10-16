#pragma once
#include <cstdint>
#ifndef W12_COPY_BOARD_PROFILE
#define W12_COPY_BOARD_PROFILE 2
#endif
namespace w12_board {
// 0 leaves display pins alone; 2 preserves established legacy45/47 behavior;
// 8 records the historical Heltec R8 experiment;12 is Meshnology W12/C-3.
static constexpr int profile=W12_COPY_BOARD_PROFILE;
static_assert(profile==0 || profile==2 || profile==8 || profile==12,"Unreviewed W12 board profile");
static constexpr int vext=profile==8?40:(profile==2 || profile==12)?45:-1;
static constexpr int reset=profile==8?21:(profile==2 || profile==12)?47:-1;
static constexpr int vext_on=profile==8?1:0;
static constexpr bool display_verified=profile!=0;
static constexpr uint64_t display_pin_mask=!display_verified?0:profile==8?((1ULL<<40)|(1ULL<<21)):((1ULL<<45)|(1ULL<<47));
}
