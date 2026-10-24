// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "sleep_gate.hpp"
#include "utility/Button_Class.hpp"
#include <cassert>
#include <cstdio>
int main(){
 using m5::Button_Class;using corese::SleepGate;using corese::SleepStage;
 auto clicks=[](unsigned n,unsigned spacing){Button_Class b;b.setHoldThresh(500);for(unsigned i=0;i<n;++i){b.setState(1000+i*spacing,Button_Class::state_clicked);b.setState(1001+i*spacing,Button_Class::state_nochange);}b.setState(1601+(n-1)*spacing,Button_Class::state_nochange);return b.wasDoubleClicked();};
 assert(!clicks(1,200)&&clicks(2,200)&&!clicks(3,200));
 // Actual library decides separated singles before the next physical click.
 Button_Class single;single.setHoldThresh(500);single.setState(1000,Button_Class::state_clicked);single.setState(1100,Button_Class::state_nochange);single.setState(1600,Button_Class::state_nochange);assert(single.wasSingleClicked());single.setState(2000,Button_Class::state_clicked);single.setState(2100,Button_Class::state_nochange);single.setState(2600,Button_Class::state_nochange);assert(single.wasSingleClicked()&&!single.wasDoubleClicked());
 SleepGate g;assert(!g.take_shutdown());assert(g.request()&&!g.request());assert(!g.take_shutdown());assert(g.take_request()&&!g.take_request());g.drained(false,false);assert(g.stage()==SleepStage::Draining&&!g.take_shutdown());g.drained(true,false);assert(g.take_shutdown()&&!g.take_shutdown());
 SleepGate failed;assert(failed.request()&&failed.take_request());failed.drained(true,true);assert(failed.stage()==SleepStage::Failed&&!failed.take_shutdown());failed.drained(true,false);assert(!failed.take_shutdown());
 std::puts("PASS actual M5 button logic: single/double/triple/spaced clicks; one shutdown only after successful bounded drain; fault stays awake");
}
