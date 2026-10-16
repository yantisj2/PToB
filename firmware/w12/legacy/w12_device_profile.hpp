#pragma once
#include <cstdint>
#include <cstring>
#include "ptob_device.h"
#ifndef PTOB_GNSS_INDICATORS
#define PTOB_GNSS_INDICATORS 0
#endif
#ifdef W12_GNSS_VERIFIED_INPUTS
static_assert(PTOB_GNSS_INDICATORS==W12_GNSS_VERIFIED_INPUTS,"GNSS capability differs from the bound device header");
#endif
namespace w12_device {
struct Profile {uint8_t factory[6];unsigned peer;const char* name;bool gps_inputs;};
inline const Profile* find(const uint8_t factory[6]) {
 static constexpr Profile device{PTOB_FACTORY_MAC,PTOB_LEGACY_WIRE_ID,PTOB_DEVICE_LABEL,bool(PTOB_GNSS_INDICATORS)};
 return !std::memcmp(factory,device.factory,6)?&device:nullptr;
}
}
