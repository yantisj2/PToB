// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ui.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace n30;
struct Button {
 Input input;View view;uint64_t at=1000000;
 Action start(){auto a=input.update(true,at,view);input.update(true,at+40000,view);return a;}
 Action release(uint64_t duration){input.update(false,at+duration,view);auto a=input.update(false,at+duration+40000,view);at+=duration+100000;return a;}
 Action tap(uint64_t duration=100000){start();return release(duration);}
 Action hold(){start();auto a=input.update(true,at+MenuHoldUs,view);assert(input.update(true,at+MenuHoldUs+10000000,view)==Action::None);release(MenuHoldUs+10100000);return a;}
};
int main(){
 static_assert(MenuHoldUs==2500000&&MenuIdleUs==5000000);
 // Every duration below2500ms is short on every page, even when debounce
 // crosses the threshold. Exact2500ms is long, including release-only sampling.
 for(unsigned page=0;page<PageCount;page++)for(uint64_t duration:{40000ULL,100000ULL,1500000ULL,2499999ULL}){
  Button b;b.view.page=page;assert(b.tap(duration)==Action::None&&!b.view.menu.open&&b.view.page==(page+1)%PageCount);
 }
 for(unsigned page=0;page<PageCount;page++){
  Button b;b.view.page=page;b.start();assert(b.input.update(true,b.at+MenuHoldUs-1,b.view)==Action::None&&!b.view.menu.open);
  assert(b.input.update(true,b.at+MenuHoldUs,b.view)==Action::None&&b.view.menu.open&&b.view.menu.item==0);
  // The opening hold never selects, even across a very long hold/idle timeout.
  assert(b.input.update(true,b.at+20000000,b.view)==Action::None&&b.view.menu.open&&b.view.page==page);
  b.release(20100000);assert(b.view.menu.open&&b.view.menu.item==0);
  for(uint64_t duration:{100000ULL,1500000ULL,2499999ULL}){auto item=b.view.menu.item;assert(b.tap(duration)==Action::None&&b.view.menu.item==(item+1)%unsigned(MenuItem::Count));}
  b.view.menu.item=unsigned(MenuItem::Parent);assert(b.hold()==Action::ToggleRelayOnly&&b.view.menu.open);
  b.view.menu.item=unsigned(MenuItem::Ble);assert(b.hold()==Action::ToggleBle&&b.view.menu.open);
  b.view.menu.item=unsigned(MenuItem::LoRa);assert(b.hold()==Action::ToggleLoRa&&b.view.menu.open);
  b.view.menu.item=unsigned(MenuItem::Exit);assert(b.hold()==Action::None&&!b.view.menu.open&&b.view.page==page);
  Button exact;exact.view.page=page;exact.start();assert(exact.release(MenuHoldUs)==Action::None&&exact.view.menu.open&&exact.view.page==page);
  exact.view.menu.item=unsigned(MenuItem::Power);exact.start();assert(exact.release(MenuHoldUs)==Action::PowerOff&&exact.view.blank&&!exact.view.menu.open);
 }
 Button idle;idle.view.page=PrecisionPage;idle.hold();const auto released=idle.at-60000;
 idle.input.update(false,released+MenuIdleUs-1,idle.view);assert(idle.view.menu.open);
 idle.input.update(false,released+MenuIdleUs,idle.view);assert(!idle.view.menu.open&&idle.view.page==PrecisionPage);
 Button skip;skip.view.menu.enabled=(1u<<unsigned(MenuItem::Power))|(1u<<unsigned(MenuItem::Ble))|(1u<<unsigned(MenuItem::Exit));skip.hold();assert(skip.view.menu.item==unsigned(MenuItem::Power));skip.tap();assert(skip.view.menu.item==unsigned(MenuItem::Ble));skip.tap();assert(skip.view.menu.item==unsigned(MenuItem::Exit));skip.tap();assert(skip.view.menu.item==unsigned(MenuItem::Power));
 // Releasing the boot-held button arms input without acting. A short saver
 // press only wakes; continuing a fresh saver press2500ms opens the menu.
 Input boot(true);View v;boot.update(true,9000000,v);boot.update(false,9100000,v);boot.update(false,9140000,v);assert(!v.menu.open&&!v.blank&&v.page==0);
 Button wake;wake.view.saver=wake.view.minimal=true;wake.tap(2499999);assert(!wake.view.saver&&!wake.view.menu.open&&wake.view.page==0);
 Button long_wake;long_wake.view.saver=true;long_wake.hold();assert(long_wake.view.menu.open&&!long_wake.view.saver);
 Button cancel;cancel.start();cancel.view.page=1;cancel.input.update(true,cancel.at+100000,cancel.view);cancel.view.page=0;cancel.release(3000000);assert(!cancel.view.menu.open&&!cancel.view.blank&&cancel.view.page==0);
 std::puts("PASS N30 menu: exact2500ms raw-release boundaries, one action per hold, consumed opening, five-second idle, all pages, enabled-row wrap, power, boot/wake/cancellation");
}
