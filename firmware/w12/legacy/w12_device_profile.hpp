#pragma once
#include <cstdint>
#include <cstring>
#include "ptob_device.h"
namespace w12_device {
struct Profile {uint8_t factory[6];unsigned peer;const char* name;bool gps_inputs;};
inline const Profile* find(const uint8_t factory[6]) {
 static constexpr Profile device{PTOB_FACTORY_MAC,PTOB_LEGACY_WIRE_ID,PTOB_DEVICE_LABEL,false};
 return !std::memcmp(factory,device.factory,6)?&device:nullptr;
}
}
