// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
namespace v12 {struct ConsoleFrame;bool frame_submit_corese(const ConsoleFrame&);}
bool corese_display_command(const char*);
bool corese_display_response_pending();
void corese_display_response(v12::ConsoleFrame&);

bool corese_display_take_sleep_request();
void corese_display_sleep_drain(bool ready,bool fault);
