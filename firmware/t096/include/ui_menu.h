/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#ifndef R2_UI_MENU_H
#define R2_UI_MENU_H
#include <stdbool.h>
#include <stdint.h>
#define R2_UI_MENU_HOLD_MS UINT64_C(2000)
#define R2_UI_MENU_IDLE_MS UINT64_C(5000)
enum r2_ui_menu_item {R2_MENU_MODE,R2_MENU_POWER,R2_MENU_BLE,R2_MENU_LORA,R2_MENU_EXIT,R2_MENU_ITEMS};
enum r2_ui_menu_action {R2_MENU_NONE,R2_MENU_OPEN,R2_MENU_NEXT,R2_MENU_CLOSE,R2_MENU_PROFILE,R2_MENU_OFF,R2_MENU_BLE_TOGGLE,R2_MENU_LORA_TOGGLE,R2_MENU_PAGE,R2_MENU_WAKE};
struct r2_ui_menu {
 uint64_t activity_ms;
 uint8_t item,hold_percent,seconds;
 bool open;
};
struct r2_ui_button_state {uint64_t pressed_ms;bool down,held,wake;};
static inline unsigned r2_ui_menu_next(unsigned item,uint8_t enabled){
 for(unsigned n=0;n<R2_MENU_ITEMS;n++){item=(item+1)%R2_MENU_ITEMS;if(enabled&(1u<<item))return item;}
 return R2_MENU_EXIT;
}
/* Debounced physical and bounded console presses share this exact transition
 * function. It returns at most one action per uninterrupted hold. */
static inline enum r2_ui_menu_action r2_ui_menu_step(struct r2_ui_menu *m,
 struct r2_ui_button_state *b,bool down,bool saver,uint8_t enabled,uint64_t now){
 enum r2_ui_menu_action action=R2_MENU_NONE;
 if(down&&!b->down){
  b->down=true;m->activity_ms=now;
  b->pressed_ms=now;b->held=false;b->wake=saver;
 }
 /* Classify the release edge too: a release at exactly2000ms is long even
  * when no held sample happened at that instant. */
 if(b->down){
  m->activity_ms=now;
  uint64_t held=now>=b->pressed_ms?now-b->pressed_ms:0;
  m->hold_percent=m->open&&!b->held?(uint8_t)(held>=R2_UI_MENU_HOLD_MS?100:held*100/R2_UI_MENU_HOLD_MS):0;
  if(!b->held&&held>=R2_UI_MENU_HOLD_MS){
   b->held=true;m->hold_percent=0;
   if(!m->open){m->open=true;m->item=(uint8_t)r2_ui_menu_next(R2_MENU_ITEMS-1,enabled);action=R2_MENU_OPEN;}
   else if(enabled&(1u<<m->item)){
    switch(m->item){
     case R2_MENU_MODE:action=R2_MENU_PROFILE;break;
     case R2_MENU_POWER:action=R2_MENU_OFF;break;
     case R2_MENU_BLE:action=R2_MENU_BLE_TOGGLE;break;
     case R2_MENU_LORA:action=R2_MENU_LORA_TOGGLE;break;
     case R2_MENU_EXIT:m->open=false;action=R2_MENU_CLOSE;break;
    }
   }
  }
 }
 if(!down&&b->down){
  b->down=false;m->activity_ms=now;m->hold_percent=0;
  if(!b->held){
   if(b->wake)action=R2_MENU_WAKE;
   else if(m->open){m->item=(uint8_t)r2_ui_menu_next(m->item,enabled);action=R2_MENU_NEXT;}
   else action=R2_MENU_PAGE;
  }
  b->wake=false;
 }
 if(m->open&&!(enabled&(1u<<m->item)))m->item=(uint8_t)r2_ui_menu_next(m->item,enabled);
 uint64_t idle=now>=m->activity_ms?now-m->activity_ms:0;
 if(m->open&&!down&&idle>=R2_UI_MENU_IDLE_MS){m->open=false;action=R2_MENU_CLOSE;}
 m->seconds=(uint8_t)(idle>=R2_UI_MENU_IDLE_MS?0:(R2_UI_MENU_IDLE_MS-idle+999)/1000);
 return action;
}
/* Use the observed raw edges for duration, after50ms stable admission. Freeze
 * elapsed hold time during release debounce; a1999ms release must stay short. */
static inline enum r2_ui_menu_action r2_ui_menu_sample(struct r2_ui_menu *m,
 struct r2_ui_button_state *b,bool raw,bool stable,bool saver,uint8_t enabled,
 uint64_t changed,uint64_t now){
 if(raw&&!stable&&!b->down)m->activity_ms=now;
 uint64_t edge=(!raw&&b->down)||(stable&&!b->down)?changed:now;
 return r2_ui_menu_step(m,b,stable,saver,enabled,edge);
}
#endif
