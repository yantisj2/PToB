// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "periodic_rx_model.hpp"
namespace v12::n30_probe {
// N30 worker-context observers only. Installed once before BLE starts. They
// never alter captures, authentication, the delivered model or permissions.
struct Pair {
 uint64_t root=0,boot=0,local20=0,available20=0,remote_ns=0;
 int64_t predicted_ns=0;uint32_t source=0,train=0,epoch=0,publication=0;
 uint16_t event=0;uint8_t channel=255;bool causal=false;
 uint64_t created_ns=0,expires_ns=0;
};
using CaptureSink=void(*)(const periodic_rx::Stamp&,const adv_rx::BleAnchor&,int8_t);
using PairSink=void(*)(const Pair&);
inline CaptureSink capture_sink=nullptr;
inline PairSink pair_sink=nullptr;
inline int8_t packet_rssi=127; // Set by this same host callback before match.
}
