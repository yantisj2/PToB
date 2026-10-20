// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace n30 {
// Every synchronous driver call uses the original frame deadline. Reserve
// wire time plus1ms and round the timeout DOWN, leaving1ms for retirement.
inline int transfer_timeout(uint64_t deadline,uint64_t now,size_t bytes){
 if(now>=deadline||bytes>129)return 0;
 const uint64_t left=deadline-now,wire=(uint64_t(bytes)*9*1000000+399999)/400000;
 if(left<wire+2000)return 0;
 const auto ms=std::min<uint64_t>(10,(left-1000)/1000);
 return ms*1000>=wire+1000?int(ms):0;
}
}
