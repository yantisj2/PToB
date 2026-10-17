/* SPDX-License-Identifier: Apache-2.0 */
#define main publication_cases
#include "usb_source_test.c"
#undef main
static struct r2_uc_clock replacement;
static void publish_new(void){r2_usb_source_publish(&replacement);}
static void begin_new(void){r2_usb_source_begin(sampled.tick,sampled.epoch);}
static void prepare_same(void){r2_usb_source_prepare(sampled.tick,sampled.epoch,&replacement);}
int main(void){
 publication_cases();sequence=0;
 struct usb_setup_packet s={0xc1,0x57,0,2,256};int32_t length;uint8_t *p;
 sampled=(struct r2_capture){.tick=200000,.epoch=1};
 replacement=(struct r2_uc_clock){.map={.effective=100000,.anchor_tick=100000,.anchor_ns=UINT64_C(1700000000000000000),.generation=60,.bound=UINT32_MAX,.rate_bound=UINT32_MAX,.capture_bound=UINT32_MAX},.origin={.root=17,.boot=18,.epoch=19,.expires=UINT64_C(1700000010000000000)},.until_tick=UINT64_C(90000000),.session_until=UINT64_C(900000000),.discipline_tick=100000,.capture_epoch=1,.valid=true,.estimate=true};
 r2_usb_source_publish(&replacement);raw(201000,202000);
 assert(!r2_usb_source_request(&s,&length,&p)&&get(p+36,4)==31);
 project_hook=r2_usb_source_withdraw;
 assert(!r2_usb_source_request(&s,&length,&p)&&get(p+36,4)==4&&!get(p+64,8)&&get(p+220,4)==4);
 assert(!r2_usb_source_request(&s,&length,&p)&&get(p+36,4)==6); /* old local estimate only, withdrawn authority */
 r2_usb_source_publish(&replacement);replacement.map.generation++;replacement.origin.root=20;project_hook=publish_new;
 assert(!r2_usb_source_request(&s,&length,&p)&&get(p+36,4)==4&&!get(p+64,8));
 sampled.tick=202000;raw(203000,204000);
 assert(!r2_usb_source_request(&s,&length,&p)&&get(p+36,4)==31&&get(p+80,8)==20);
 project_hook=prepare_same;
 assert(!r2_usb_source_request(&s,&length,&p)&&get(p+36,4)==31);
 project_hook=begin_new;
 assert(!r2_usb_source_request(&s,&length,&p)&&get(p+36,4)==4&&!get(p+64,8));
 revision=UINT32_MAX;
 assert(!r2_usb_source_request(&s,&length,&p)&&get(p+36,4)==4&&!get(p+64,8));
 puts("PASS projection never inside spinlock; concurrent withdrawal, publication, begin and revision saturation fail closed; retained local estimate does not borrow authority");
}
