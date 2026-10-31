/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "ui.h"
#include "core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct r2_ui_menu menu;
static struct r2_ui_button_state button;
static uint64_t now;
static uint8_t mask=31;
static enum r2_ui_menu_action step(bool down,uint64_t advance,bool saver){now+=advance;return r2_ui_menu_step(&menu,&button,down,saver,mask,now);}
static enum r2_ui_menu_action tap(void){assert(step(true,100,false)==R2_MENU_NONE);return step(false,120,false);}
static enum r2_ui_menu_action hold(void){assert(step(true,100,false)==R2_MENU_NONE);enum r2_ui_menu_action a=step(true,1500,false);assert(step(true,4500,false)==R2_MENU_NONE);assert(step(false,100,false)==R2_MENU_NONE);return a;}
static void save(const char *dir,const char *name,struct r2_ui_model *m,struct r2_ui_status *s){
 uint8_t fb[R2_UI_FRAME_BYTES],wire[R2_UI_BYTES];r2_ui_render(fb,m,s,1000);
 assert(r2_ui_expand_rgb565(wire,sizeof wire,fb,sizeof fb));char path[512];snprintf(path,sizeof path,"%s/%s.ppm",dir,name);
 FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 for(unsigned i=0;i<sizeof wire;i+=2){unsigned p=(wire[i]<<8)|wire[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}fclose(f);
}
int main(int argc,char **argv){
 assert(argc==2);
 assert(tap()==R2_MENU_PAGE);
 assert(hold()==R2_MENU_OPEN&&menu.open&&menu.item==R2_MENU_MODE);
 assert(hold()==R2_MENU_PROFILE&&menu.open);
 assert(tap()==R2_MENU_NEXT&&menu.item==R2_MENU_POWER);
 assert(hold()==R2_MENU_OFF);
 assert(tap()==R2_MENU_NEXT&&menu.item==R2_MENU_BLE);
 assert(hold()==R2_MENU_BLE_TOGGLE);
 assert(tap()==R2_MENU_NEXT&&menu.item==R2_MENU_LORA);
 assert(hold()==R2_MENU_LORA_TOGGLE);
 assert(tap()==R2_MENU_NEXT&&menu.item==R2_MENU_EXIT);
 assert(tap()==R2_MENU_NEXT&&menu.item==R2_MENU_MODE); /* Wrap. */
 assert(step(false,4999,false)==R2_MENU_NONE&&menu.open);
 assert(step(false,1,false)==R2_MENU_CLOSE&&!menu.open); /* Exactly5s. */
 assert(hold()==R2_MENU_OPEN);for(unsigned n=0;n<4;n++)assert(tap()==R2_MENU_NEXT);
 assert(hold()==R2_MENU_CLOSE&&!menu.open);
 /* Wake tap consumes navigation; wake long still opens and consumes hold. */
 assert(step(true,100,true)==R2_MENU_NONE);assert(step(false,120,false)==R2_MENU_WAKE);
 assert(step(true,100,true)==R2_MENU_NONE);assert(step(true,1500,false)==R2_MENU_OPEN);
 assert(step(true,10000,false)==R2_MENU_NONE&&menu.open);assert(step(false,100,false)==R2_MENU_NONE);
 assert(step(false,4999,false)==R2_MENU_NONE&&menu.open);assert(step(false,1,false)==R2_MENU_CLOSE);
 /* A disabled TX row is skipped in RC; capability omissions are bounded. */
 mask&=~(1u<<R2_MENU_LORA);assert(hold()==R2_MENU_OPEN);
 for(unsigned n=0;n<3;n++)assert(tap()==R2_MENU_NEXT);assert(menu.item==R2_MENU_EXIT);
 mask=(1u<<R2_MENU_POWER)|(1u<<R2_MENU_EXIT);menu.open=false;assert(hold()==R2_MENU_OPEN&&menu.item==R2_MENU_POWER);
 struct r2_request r;const char *base="r2 00000001 0123456789abcdef aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa 0000000000000001 ";char command[192];
 const char *ops[]={"menu","menu tap","menu hold","menu cancel"};
 for(unsigned n=0;n<4;n++){snprintf(command,sizeof command,"%s%s",base,ops[n]);assert(!r2_parse(command,&r)&&r.op==R2_MENU);assert(r.seconds==(n!=0));}
 const char *bad[]={"menu off","menu hold extra","menu 00","menu tap ","menu  hold"};
 for(unsigned n=0;n<5;n++){snprintf(command,sizeof command,"%s%s",base,bad[n]);assert(r2_parse(command,&r)<0);}
 struct r2_ui_model m={.unit="TC-04",.sampled_ms=1000,.platform_ready=true,.profile_supported=true,.profile_rc=true,.ble_rx_supported=true,.ble_rx_requested=true};
 struct r2_ui_status s={.menu={.open=true,.seconds=5}};
 for(unsigned i=0;i<5;i++){s.menu.item=(uint8_t)i;char name[24];snprintf(name,sizeof name,"menu-item-%u",i);save(argv[1],name,&m,&s);}
 s.menu.item=R2_MENU_POWER;s.menu.hold_percent=50;save(argv[1],"menu-power-hold",&m,&s);s.menu.hold_percent=0;
 s.menu.item=R2_MENU_BLE;s.menu_result_item=R2_MENU_BLE;s.menu_result_until=2000;m.ble_rx_requested=false;save(argv[1],"menu-ble-off",&m,&s);
 s.menu_result=-5;save(argv[1],"menu-failure",&m,&s);s.menu_result=0;s.menu_result_until=0;
 m.profile_switching=true;save(argv[1],"menu-switching",&m,&s);m.profile_switching=false;
#ifdef R2_LORA
 m.lora.transmitter=true;m.lora.requested=true;strcpy(m.unit,"TC-03");s.menu.item=R2_MENU_MODE;save(argv[1],"menu-tc03-rc",&m,&s);
 assert(!(r2_ui_menu_enabled(&m)&(1u<<R2_MENU_LORA)));
 m.profile_rc=false;s.menu.item=R2_MENU_LORA;m.lora.active=true;save(argv[1],"menu-tc03-gm",&m,&s);
 assert(r2_ui_menu_enabled(&m)&(1u<<R2_MENU_LORA));
 m.lora.requested=false;s.menu_result_item=R2_MENU_LORA;s.menu_result_until=2000;save(argv[1],"menu-tc03-off",&m,&s);
#endif
 puts("PASS menu: separate holds, all actions, wrap/skip, wake, exact idle/release deadlines, parser refusal and actual native render states");
}
