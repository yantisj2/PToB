// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "touch_capture.hpp"
#include <cassert>
int main(){
 using namespace watch;TouchCapture input;TouchStroke out;View v;v.settings.face=2;
 // A complete swipe occurs while a slow frame is drawing. Compressed capture
 // survives until the UI consumes it and cycles Solar -> Shadow.
 assert(!input.update(true,true,330,250,7,100000,out));
 assert(!input.update(true,true,210,253,7,180000,out));
 assert(!input.update(true,false,0,0,7,190000,out)); // inter-report gap
 assert(!input.update(true,true,70,251,7,230000,out));
 assert(input.update(true,false,0,0,7,270000,out));
 assert(out.apply(v,7,650000)&&v.settings.face==3&&v.screen==Screen::Face);
 // Right swipe escapes Solar to Instrument as well.
 input={};v.settings.face=2;
 input.update(true,true,60,250,8,700000,out);input.update(true,true,320,252,8,850000,out);
 assert(input.update(true,false,0,0,8,900000,out));assert(out.apply(v,8,1100000)&&v.settings.face==1);
 // A tap begun on another screen or stale queued action cannot navigate.
 assert(!out.apply(v,9,1100000));assert(!out.apply(v,8,3000000));
 // Wake consumes the whole first gesture; it never also changes face.
 v.settings.face=2;v.asleep=true;assert(out.apply(v,8,1100000));assert(!v.asleep&&v.settings.face==2);
 // Bus errors cannot fabricate a release/tap. Prolonged failure cancels and
 // requires release before accepting another touch.
 input={};input.update(true,true,200,450,4,100000,out);
 assert(!input.update(false,false,0,0,4,130000,out));
 assert(!input.update(false,false,0,0,4,210001,out));
 assert(!input.update(true,true,200,450,4,220000,out));
 assert(!input.update(true,false,0,0,4,240000,out));
 input.update(true,true,200,450,4,300000,out);
 assert(input.update(true,false,0,0,4,350000,out));
 v.screen=Screen::Face;assert(out.apply(v,4,400000)&&v.screen==Screen::Launcher);
 // View-context changes during contact cancel the gesture.
 input={};input.update(true,true,330,250,1,100000,out);
 assert(!input.update(true,true,60,250,2,200000,out));
 assert(!input.update(true,false,0,0,2,300000,out));
 // Initial contact can wake immediately, before release. Publishing the awake
 // context cancels the rest of that stroke, so it cannot also swipe or tap.
 input={};v.screen=Screen::Face;v.settings.face=2;v.asleep=true;
 assert(!input.update(true,true,330,250,10,100000,out));
 v.touch(0,0,100000);assert(!v.asleep&&v.settings.face==2);
 assert(!input.update(true,true,60,250,11,200000,out));
 assert(!input.update(true,false,0,0,11,300000,out));
 assert(v.screen==Screen::Face&&v.settings.face==2);
}
