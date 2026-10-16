#pragma once
#include <cstdint>
#include "ptob_device.h"
namespace v12 {
struct NativeDeviceBinding {uint8_t factory[6];const char* label;uint8_t wire_id;};
inline const NativeDeviceBinding* native_device_binding(){
 static constexpr NativeDeviceBinding device{PTOB_FACTORY_MAC,PTOB_DEVICE_LABEL,PTOB_LEGACY_WIRE_ID};return &device;
}
}
