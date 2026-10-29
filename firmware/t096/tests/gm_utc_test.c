/* Actual production source/legacy JOIN/origin paths; synthetic UTC is unqualified. */
#include "precision.h"
#include "group_admission.h"
#include "radio_service.h"
#include "group_precision.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static struct r2_join_exchange exchange;
static bool gm7_server_only;
static struct r2_gm7_admission_source gm7_source;
static struct r2_group_client timing_context,coarse_context,client;
static struct r2_local_demand demand;
static uint64_t sequence;
static bool poor_direct,received_request,relay_allowed,relay_ready;
static struct r2_radio_precision precision;
static struct r2_relay_origin generic_origin;
static uint64_t generic_frame=1,updated_at;
static uint8_t generic_proof[32];
static struct r2_enrollment enrollment;
static unsigned own;
static uint32_t publication,train=1,last_published_capture;
static struct r2_radio_scope active_scope;
struct pt_output {uint8_t payload[190];size_t length;uint64_t payload_until;uint32_t payload_sequence;};
#include "gm_utc_under_test.h"
int main(void){
 struct r2_source_clock s;struct r2_pps_precision p;
 r2_source_clock_init(&s);r2_pps_precision_init(&p);
 struct r2_clock c;r2_clock_init(&c,0,R2_ASSOC_UNKNOWN);
 const int64_t base=INT64_C(1791504000000000000);uint64_t last_origin=0;unsigned renewals=0;
 enrollment.group=(struct r2_group_config){.id=7,.epoch=2,.psk={1,2,3}};
 exchange=(struct r2_join_exchange){.server=true,.peer=200,.peer_boot=600,.nonce=12,.remote_nonce=13};
 enrollment.identity[0]=100;enrollment.identity[1]=200;
 struct r2_capture at={0};int64_t utc=0;uint64_t ns=0,gen=0;
 for(unsigned sec=0;sec<40;sec++){
  uint64_t tick=100+(uint64_t)sec*16000000;
  assert(r2_pps_precision_add(&p,(struct r2_capture){tick,1,R2_CAPTURE_PPS}));
  at=(struct r2_capture){tick+80000,1,R2_CAPTURE_NOW};
  if(!p.rate_valid)continue;
  assert(r2_source_clock_publish(&s,&p,at));assert(r2_source_clock_read(&s,at,&ns,&gen));
  int64_t jitter=sec==1?0:sec%2?40000000:-40000000;
  assert(r2_source_utc_bind(&s,at,base+(int64_t)ns+jitter,c.view.source_epoch,R2_ASSOC_UNKNOWN)==1);
  assert(s.utc_offset_ns==base&&s.utc_association==R2_ASSOC_UNKNOWN);
  assert(r2_source_utc_read(&s,at,c.view.source_epoch,&utc)&&utc==base+(int64_t)ns);
  uint64_t now=(uint64_t)sec*1000000+5000;
  c.view.valid=c.view.fix_active=c.view.pps_present=true;c.last_rmc=c.last_pps=(int64_t)now-5000;
  c.last_rmc_utc=(base+(int64_t)ns+jitter)/1000-5000;
  c.translated_epoch=c.view.source_epoch;c.translated_utc_us=utc/1000;c.translated_at_us=(int64_t)now;
  struct r2_network n={.relative_source=true,.identity=100,.boot=500,.root_boot=500,.self=0,.choice=0,.count=2,.ready=true,.name="UTC test",.publication=1,.map_bound_us=40000};
  n.view=local_source(&n,&c,now);n.service_until=n.view.valid_until_us;
  assert(n.view.available&&n.view.relative_ready&&!n.view.qualified);
  assert(n.service_until==(uint64_t)c.last_pps+R2_PPS_FRESH_US);
  struct r2_join_record join=message(&n,R2_JOIN_ACCEPT,now);
  assert(join.source_flags==1&&join.utc_us==(uint64_t)utc/1000);
  struct r2_join_time anchor;assert(r2_group_join_time(&anchor,&join,now-10000,now+10000,40000));
  assert(anchor.until<n.service_until&&anchor.utc_us==join.utc_us+10000);
  active_scope=(struct r2_radio_scope){.boot=500,.source_epoch=1234567,.local_source_epoch=c.view.source_epoch,.until_us=n.service_until,.gm=true};
  struct pt_output out={0};assert(!prepare_data(&s,at,now,&out));assert(out.length==190);
  struct r2_relay_packet decoded;assert(!r2_group_relay_decode3(&decoded,out.payload,out.length,&enrollment.group,enrollment.identity));
  assert(decoded.origin.utc_offset_ns==base&&decoded.origin.source_epoch==1234567);
  if(decoded.origin.created_ns!=last_origin){++renewals;last_origin=decoded.origin.created_ns;}
  /* Retries and fresh JOIN do not modify the authenticated offset. */
  assert(!prepare_data(&s,at,now,&out));assert(generic_origin.utc_offset_ns==base);
 }
 assert(renewals>=19);
 uint32_t old=c.view.source_epoch;int64_t offset=s.utc_offset_ns;
 assert(r2_source_utc_bind(&s,at,-1,old,R2_ASSOC_UNKNOWN)==0&&s.utc_offset_ns==offset);
 assert(!r2_source_utc_read(&s,(struct r2_capture){p.last+24000000,1,R2_CAPTURE_NOW},old,&utc));
 assert(!r2_source_utc_read(&s,(struct r2_capture){at.tick,2,R2_CAPTURE_NOW},old,&utc));
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns+750000000,old,R2_ASSOC_UNKNOWN)==1&&s.utc_offset_ns==offset);
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns+2000000000,old,R2_ASSOC_UNKNOWN)==1&&s.utc_offset_ns==offset);
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns+2000000001,old,R2_ASSOC_UNKNOWN)==-1);
 assert(!r2_source_utc_read(&s,at,old,&utc));r2_clock_source_fence(&c);assert(c.view.source_epoch==old+1);
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns+2100000000,c.view.source_epoch,R2_ASSOC_UNKNOWN)==1);
 assert(!r2_source_utc_read(&s,at,old,&utc));
 struct pt_output fenced={0};assert(prepare_data(&s,at,40005000,&fenced)==-ESTALE);
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns+2100000000,c.view.source_epoch,R2_ASSOC_PRECEDING_UTC_SECOND)==-1);
 r2_clock_source_fence(&c);
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns,c.view.source_epoch,R2_ASSOC_UNKNOWN)==1);
 /* Restart within the SAME hardware epoch resets relative zero, not identity. */
 r2_source_clock_invalidate(&s);assert(!s.utc_bound);
 assert(r2_pps_precision_add(&p,(struct r2_capture){p.last+16000000,1,R2_CAPTURE_PPS}));
 at=(struct r2_capture){p.last+80000,1,R2_CAPTURE_NOW};assert(r2_source_clock_publish(&s,&p,at));
 assert(r2_source_clock_read(&s,at,&ns,&gen)&&ns==5000000);
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns,c.view.source_epoch,R2_ASSOC_UNKNOWN)==-1);
 old=c.view.source_epoch;r2_clock_source_fence(&c);
 assert(r2_source_utc_bind(&s,at,base+(int64_t)ns,c.view.source_epoch,R2_ASSOC_UNKNOWN)==1);
 assert(!r2_source_utc_read(&s,at,old,&utc));
 /* Epoch exhaustion cannot silently reuse an interpretation. */
 c.view.source_epoch=UINT32_MAX;r2_clock_source_fence(&c);assert(c.fatal&&c.translated_epoch==0);
 puts("PASS 39 map revisions, >=19 signed origin renewals/retries, coherent legacy JOIN, unknown association, exact expiry, contradiction/association/same-capture restart fences");
}
