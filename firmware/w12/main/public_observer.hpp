#pragma once
#include "public_receiver.hpp"
namespace v12 {
struct PublicObserverStatus {PublicStatus records;uint32_t drops=0,peak=0;uint64_t work_max_us=0;};
bool public_observer_init();
bool public_observer_enqueue(const uint8_t* bytes,size_t length);
PublicObserverStatus public_observer_status();
}
