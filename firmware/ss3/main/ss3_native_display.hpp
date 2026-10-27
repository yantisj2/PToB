#pragma once
#include "display.hpp"
#include "native_r2.hpp"
namespace v12 {struct ConsoleFrame;}
namespace sticks3 {
// Value-owned native model; LCD projection never trains or renews its permission.
void present_native(const DisplayState&,const v12::R2Status&,uint64_t tick20,uint64_t now_us,const char* banner=nullptr,uint64_t banner_until20=0,uint8_t chart=0);
bool power_command(bool download);
void display_status();
void mesh_status(v12::ConsoleFrame&);
struct DisplayProgress {uint32_t frames,errors,worker_ms,paint_ms,io_timeouts,io_errors;bool pending;};
DisplayProgress display_progress();
}
