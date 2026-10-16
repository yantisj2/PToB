#pragma once
#include <cstdint>
#include "driver/usb_serial_jtag.h"
struct ble_npl_event {void(*fn)(ble_npl_event*)=nullptr;};
struct ble_npl_callout {};
inline void* nimble_port_get_dflt_eventq(){return nullptr;}
inline void ble_npl_event_init(ble_npl_event* e,void(*fn)(ble_npl_event*),void*){e->fn=fn;}
inline void ble_npl_eventq_put(void*,ble_npl_event*){}
inline void ble_npl_callout_init(ble_npl_callout*,void*,void(*)(ble_npl_event*),void*){}
inline void ble_npl_callout_reset(ble_npl_callout*,uint32_t){}
inline uint32_t ble_npl_time_ms_to_ticks32(uint32_t n){return n;}
inline int nimble_port_init(){return ESP_OK;}
inline void nimble_port_run(){}
