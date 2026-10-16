/* SPDX-License-Identifier: Apache-2.0 */
#include "radio_precision.h"
#include "rc_direct_calibration.h"
#include <assert.h>
#include <stdio.h>
static struct r2_radio_precision gm,client;
static void run(int ppm,bool tail) {
 memset(&gm,0,sizeof gm);memset(&client,0,sizeof client);
 struct r2_radio_scope g={.boot=9,.until_us=200000000,.source_epoch=3,.publication_epoch=1,.root=1,.gm=true}, f=g;f.gm=false;f.follower=true;
 r2_radio_precision_scope(&gm,g,1);r2_radio_precision_scope(&client,f,2);
 struct r2_precision_packet p={.boot=9,.source_epoch=3,.root=1,.train=1,.capture_epoch=1,.flags=1,.utc_offset_ns=1000000000000LL};
 struct r2_capture rx={0},now={0};
 for(unsigned i=0;i<(tail?2403u:2400u);i++) {
  if(i==120)continue; /* One missed physical event: retain event-delta arithmetic. */
  uint64_t ns=1000000000ULL+(uint64_t)i*R2_PRECISION_INTERVAL_NS, tx=ns*2/125;
  uint64_t local=tx+(int64_t)tx*ppm/1000000+32000000;
  if(tail&&i==2401)local+=1600; /*100us receive tail, never residual-filtered*/
  uint16_t event=(uint16_t)(65500+i);
  p.publication=i+1;p.created_ns=ns-1000000;p.expires_ns=ns+1000000000;
  p.count=gm.sent_count;memcpy(p.correction,gm.sent,sizeof p.correction);
  rx=(struct r2_capture){local,2,R2_CAPTURE_RADIO};now=(struct r2_capture){local+80000+(i%7)*16000,2,R2_CAPTURE_NOW};
  assert(r2_radio_precision_rx(&client,&p,rx,event,i%37,now,ns/1000,(int64_t)ns+p.utc_offset_ns));
  (void)r2_radio_precision_step(&client,now,ns/1000);
  assert(r2_radio_precision_tx(&gm,&p,(struct r2_capture){tx,1,R2_CAPTURE_RADIO},event,i%37,ns,1,p.expires_ns,ns/1000));
 }
 struct r2_radio_precision_view v;r2_radio_precision_view(&client,now,151000000,&v);
 printf("ppm=%d accepted=%u rejected=%u map=%d rms=%u samples=%u trim=%d\n",ppm,v.accepted,v.rejected,v.map_valid,v.rms_ns,v.samples,v.trim_ppb);
 assert(v.map_valid&&v.rms_valid&&v.accepted>1100);
 int64_t raw_ns,delivered_ns;assert(r2_servo_mapped(&client.servo.output,now.tick,&raw_ns));
 assert(r2_radio_precision_read(&client,now,151000000,&delivered_ns)&&delivered_ns-raw_ns==RC_DIRECT_PPS_CORRECTION_NS);
 r2_servo_map_t map;assert(r2_radio_precision_map(&client,&map)&&map.anchor-client.servo.output.anchor==RC_DIRECT_PPS_CORRECTION_NS);
 assert(!r2_radio_precision_map(&gm,&map));
 assert(v.fit_valid&&v.correction_age_valid&&v.correction_age_ms==(now.tick-client.last_accepted)/(R2_CAPTURE_HZ/1000));
 uint64_t accepted_at=client.last_accepted;
 struct r2_capture late=now;late.tick+=R2_RADIO_MAX_AGE_TICKS+1;
 assert(!r2_radio_precision_rx(&client,&p,rx,0,0,late,151000000,p.utc_offset_ns+p.created_ns));
 assert(client.last_accepted==accepted_at);
 struct r2_radio_precision rejected=client;
 /* An observation rejected by the servo may advance last_available, never last accepted. */
 memset(rejected.original,0,sizeof rejected.original);memset(rejected.correction,0,sizeof rejected.correction);
 uint64_t original_at=now.tick+16000, remote=16000000;
 rejected.original[0]=(struct r2_radio_original){.local=original_at,.created_ns=1,.expires_ns=2000000000,.publication=9000,.event=99,.channel=1,.used=true};
 rejected.correction[0]=(struct r2_radio_correction){.value={9000,99,1,remote,1,2000000000},.arrived=original_at+16000,.used=true};
 rejected.servo.session=rejected.train;rejected.servo.last_sequence=rejected.sequence+1;
 struct r2_capture reject_now={original_at+16000,2,R2_CAPTURE_NOW};
 assert(!r2_radio_precision_step(&rejected,reject_now,151000000));
 assert(rejected.last_accepted==accepted_at);
 r2_radio_precision_view(&rejected,reject_now,151000000,&v);assert(v.correction_age_valid&&v.correction_age_ms>0);
 r2_radio_precision_view(&client,now,151000000,&v);
 assert(tail?v.rms_ns>1000:v.rms_ns<1000);
 if(!tail)assert(v.drift_valid&&v.drift_ppb>=ppm*1000-2&&v.drift_ppb<=ppm*1000+2);
 r2_radio_precision_view(&gm,(struct r2_capture){gm.last_available+100,1,R2_CAPTURE_NOW},151000000,&v);
 assert(v.rms_valid&&v.rms_ns==0&&v.samples==32);
 g.boot++;r2_radio_precision_scope(&gm,g,1);
 struct r2_precise_correction empty[2]={0};assert(!gm.sent_count&&!memcmp(gm.sent,empty,sizeof empty));
 /* Delay/replay, wrong capture epoch, root scope and expiry cannot retain quality. */
 assert(!r2_radio_precision_rx(&client,&p,rx,0,0,now,151000000,p.utc_offset_ns));
 rx.epoch=1;assert(!r2_radio_precision_rx(&client,&p,rx,0,0,now,151000000,p.utc_offset_ns+p.created_ns));
 f.boot++;r2_radio_precision_scope(&client,f,2);r2_radio_precision_view(&client,now,151000000,&v);assert(!v.map_valid&&!v.rms_valid);
 f.until_us=151000000;r2_radio_precision_scope(&client,f,2);assert(!r2_radio_precision_step(&client,now,151000000));
}
static void immutable_origin(void){
 struct r2_radio_precision legacy={0},native={0};
 struct r2_radio_scope f={.boot=9,.until_us=2000000,.source_epoch=3,.publication_epoch=1,.root=1,.follower=true};
 r2_radio_precision_scope(&legacy,f,2);r2_radio_precision_scope(&native,f,2);
 struct r2_precision_packet p={.boot=9,.source_epoch=3,.root=1,.train=1,.capture_epoch=1,.flags=1,.publication=1,
  .created_ns=1000000000,.expires_ns=2500000000,.utc_offset_ns=1000000000000LL};
 struct r2_capture rx={16000000,2,R2_CAPTURE_RADIO},now={16016000,2,R2_CAPTURE_NOW};
 int64_t expected=p.utc_offset_ns+1350000000;
 assert(!r2_radio_precision_rx(&legacy,&p,rx,1,3,now,1001000,expected));
 assert(r2_radio_precision_rx_immutable_origin(&native,&p,rx,1,3,now,1001000,expected));
 assert(!native.accepted&&!native.servo.output.valid); /* Count0 is never usable. */
 uint64_t original_expiry=0;for(unsigned i=0;i<R2_RADIO_PENDING;i++)if(native.original[i].used)original_expiry=native.original[i].expires_ns;
 assert(original_expiry==p.expires_ns);
 ++p.publication;
 assert(!r2_radio_precision_rx_immutable_origin(&native,&p,rx,2,3,now,1001000,p.utc_offset_ns+(int64_t)p.expires_ns));
 struct r2_capture late=now;late.tick=rx.tick+R2_RADIO_MAX_AGE_TICKS+1;
 assert(!r2_radio_precision_rx_immutable_origin(&native,&p,rx,2,3,late,1001000,expected));
 assert(!r2_radio_precision_rx_immutable_origin(&native,&p,rx,2,3,now,1001000,p.utc_offset_ns+700000000));
 puts("PASS distinct P1 publication age / native immutable root expiry; unchanged250ms physical reference age");
}
int main(void){immutable_origin();run(0,false);run(20,false);run(-20,false);run(0,true);puts("PASS physical tuples, counter wrap, delayed processing, drift, causal RMS and scope fences");}
