/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/ui_render.c"
static unsigned samples;
static uint32_t words[8]={0x80053c17,0x12345678,0x90abcdef,0x000003ff,0x01020304,0x87654321,0x00010001,3};
void r2_usb_debug_snapshot(uint32_t out[8]){memcpy(out,words,sizeof words);++samples;}
static uint8_t fb[R2_UI_FRAME_BYTES];
#include R2_USB_SAVER_BODY
static void text_at(int x,int y,const char *word){
 uint8_t expected[R2_UI_FRAME_BYTES]={0};label(expected,x,y,word,1,C_WHITE);unsigned n=0;
 for(unsigned i=0;i<sizeof expected;i++)if(expected[i]==C_WHITE){assert(fb[i]==C_WHITE);++n;}assert(n);
}
int main(int argc,char **argv){assert(argc==2);
 for(unsigned page=0;page<R2_UI_PAGES;page++){
  assert(actual_saver(page,300000,true,false,false,false)==(page!=4));
  assert(!actual_saver(page,299999,true,false,false,false));
  assert(!actual_saver(page,300000,false,false,false,false));
  assert(!actual_saver(page,300000,true,true,false,false));
  assert(!actual_saver(page,300000,true,false,true,false));
  assert(!actual_saver(page,300000,true,false,false,true));
 }
 struct r2_ui_model m={.sampled_ms=1000,.platform_ready=true,.unit="TC-01"};
 struct r2_ui_status s={.page=4};r2_ui_render(fb,&m,&s,1000);assert(samples==1);
 for(char c='A';c<='F';c++){const uint8_t *g=glyph(c);unsigned ink=0;for(unsigned j=0;j<5;j++)ink|=g[j];assert(ink);}
 for(unsigned i=0;i<8;i++){
  char text[16];snprintf(text,sizeof text,"%c%08X","FPGNCIHV"[i],(unsigned)words[i]);text_at(5,50+12*i,text);
  /* Every raw digit cell must contain pixels. This catches unsupported
   * lowercase b/c even when an expected-text helper shares the font. */
  for(unsigned cell=0;cell<9;cell++){unsigned ink=0;for(unsigned y=0;y<7;y++)for(unsigned x=0;x<5;x++)if(fb[(50+12*i+y)*80+5+6*cell+x]==C_WHITE)++ink;assert(ink);}
 }
 char path[1024];snprintf(path,sizeof path,"%s/system-usb.ppm",argv[1]);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n80 160\n255\n");
 uint8_t rgb[R2_UI_BYTES];assert(r2_ui_expand_rgb565(rgb,sizeof rgb,fb,sizeof fb));
 for(unsigned i=0;i<sizeof rgb;i+=2){unsigned p=((unsigned)rgb[i]<<8)|rgb[i+1];fputc((p>>11)*255/31,f);fputc(((p>>5)&63)*255/63,f);fputc((p&31)*255/31,f);}assert(!fclose(f));
 /* The normal front, RF, TX, help/menu and clock-quality paths do not add
  * observation work. Physical System access changes no timing model. */
 struct r2_ui_model before=m;
 for(unsigned page=0;page<R2_UI_PAGES;page++)if(page!=4){s.page=page;r2_ui_render(fb,&m,&s,1000);assert(samples==1);}
 s.page=4;s.menu.open=true;r2_ui_render(fb,&m,&s,1000);assert(samples==1);
 s.menu.open=false;s.help=true;r2_ui_render(fb,&m,&s,1000);assert(samples==1);
 assert(!memcmp(&before,&m,sizeof m));
 puts("PASS actual System USB words/pixels/no saver; other pages retain idle/input/menu policy and no new sampling or model mutation");return 0;
}
