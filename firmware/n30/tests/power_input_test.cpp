// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ui.hpp"
#include <cassert>
#include <cstdio>
int main(){
 for(unsigned page=1;page<n30::PageCount;page++){
  n30::Input input;n30::View v;v.page=page;
  input.update(true,1000000,v);input.update(true,1040000,v);
  const auto first=input.update(true,2500000,v);
  assert(first==(page==n30::LoRaPage?n30::Action::ToggleLoRa:n30::Action::None));
  assert(input.update(true,4999999,v)==n30::Action::None&&!v.blank);
  assert(input.update(true,5000000,v)==n30::Action::PowerOff&&v.blank);
  assert(input.update(true,6000000,v)==n30::Action::None);
  input.update(false,6100000,v);assert(input.update(false,6140000,v)==n30::Action::None&&v.page==page);
  n30::Input short_press;n30::View s;s.page=page;short_press.update(true,1000000,s);short_press.update(true,1040000,s);
  short_press.update(false,4999999,s);assert(short_press.update(false,5039999,s)!=n30::Action::PowerOff&&!s.blank);
  n30::Input release;n30::View r;r.page=page;release.update(true,1000000,r);release.update(true,1040000,r);release.update(false,5000000,r);
  assert(release.update(false,5040000,r)==n30::Action::PowerOff&&r.blank);
  n30::Input boot(true);n30::View b;b.page=page;assert(boot.update(true,9000000,b)==n30::Action::None&&!b.blank);
  n30::Input cancel;n30::View c;c.page=page;cancel.update(true,1000000,c);cancel.update(true,1040000,c);c.page=0;cancel.update(true,2000000,c);c.page=page;
  assert(cancel.update(true,6000000,c)==n30::Action::None&&!c.blank);
 }
 n30::Input main;n30::View m;main.update(true,1000000,m);main.update(true,1040000,m);
 assert(main.update(true,2500000,m)==n30::Action::ToggleRelayOnly);assert(main.update(true,8000000,m)==n30::Action::None&&!m.blank);
 n30::Input wake;n30::View w;w.page=2;w.saver=true;wake.update(true,1000000,w);wake.update(true,1040000,w);
 assert(wake.update(true,8000000,w)==n30::Action::None&&!w.blank);
 std::puts("PASS N30 real-off gestures all non-clock pages, boundaries, release, boot, page fences, main toggle and saver wake");
}
