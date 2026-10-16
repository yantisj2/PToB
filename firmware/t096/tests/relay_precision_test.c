/* Public keys and synthetic physical events; portable causal engine only. */
#include "relay_precision.h"
#include <assert.h>
#include <stdio.h>
static struct r2_relay_precision root,relay,leaf;
static uint8_t keys[4][4][16];
static const int64_t offset=INT64_C(1791500000000000000);
static struct r2_capture cap(uint64_t tick,enum r2_capture_kind kind){return(struct r2_capture){tick,1,kind};}
static uint64_t ns(uint64_t tick){return tick*125/2;}
static void authenticate(struct r2_relay_packet *p,unsigned recipient,uint8_t proof[32]){
 uint8_t ad[190];struct r2_relay_packet decoded;
 assert(!r2_relay_encode(ad,p,proof,keys[p->route.transmitter]));
 assert(!r2_relay_decode(&decoded,ad,190,recipient,keys[p->origin.root][recipient],keys[p->route.transmitter][recipient]));
 *p=decoded;
}
/* Fixture models a fresh actuator delivery, not just receipt of a sample. */
static void delivered(struct r2_relay_map_record *m,uint64_t tick){
 int64_t value;assert(r2_servo_mapped(&m->map,tick,&value));
 m->map.anchor=value;m->map.phase=0;m->map.delivery=tick;m->accepted=tick;
}
static bool root_event(uint64_t tick,uint16_t event,bool deliver){
 assert(r2_relay_precision_root(&root,cap(tick-16000,R2_CAPTURE_NOW),7,ns(tick-16000),ns(tick-16000)+1000000000,offset+(int64_t)ns(tick-16000),tick/16000000+1,keys[0]));
 struct r2_relay_packet p;assert(r2_relay_precision_packet(&root,cap(tick-16000,R2_CAPTURE_NOW),tick/16,&p));
 authenticate(&p,1,root.proof);
 assert(r2_relay_precision_tx(&root,&p,cap(tick,R2_CAPTURE_RADIO),event,3,ns(tick),tick/16000000+1,ns(tick)+1000000000));
 if(!deliver)return false;
 return r2_relay_precision_rx(&relay,&p,root.proof,cap(tick+144,R2_CAPTURE_RADIO),event,3,cap(tick+16000,R2_CAPTURE_NOW),(tick+16000)/16,offset+(int64_t)ns(tick+16000));
}
static void root_envelope_continuity(void){
 struct r2_relay_precision s;r2_relay_precision_init(&s,0,0,1,11,1);
 assert(r2_relay_precision_train(&s,1));s.frame_ns=1000000000;s.source_epoch=7;
 assert(r2_relay_precision_root(&s,cap(16000000,R2_CAPTURE_NOW),7,1000000000,2500000000,offset+1000000000,1,keys[0]));
 s.sent_count=1;s.sent[0].publication=1;
 /* Fresh source observations do not refresh the old signed lifetime or erase
  * its still-authorized captured correction simply because187.5ms passed. */
 assert(r2_relay_precision_root(&s,cap(20000000,R2_CAPTURE_NOW),7,1250000000,2750000000,offset+1250000000,2,keys[0]));
 assert(s.origin.created_ns==1000000000&&s.origin.expires_ns==2500000000&&s.sent_count==1);
 /* Near the renewal boundary, the next RMC has not arrived yet. Repeated
  * sightings of the same immutable deadline must retain captured corrections. */
 assert(r2_relay_precision_root(&s,cap(32000000,R2_CAPTURE_NOW),7,2000000000,2500000000,offset+2000000000,3,keys[0]));
 assert(s.origin.created_ns==1000000000&&s.origin.expires_ns==2500000000&&s.sent_count==1);
 assert(r2_relay_precision_root(&s,cap(32800000,R2_CAPTURE_NOW),7,2050000000,2500000000,offset+2050000000,3,keys[0]));
 assert(s.origin.created_ns==1000000000&&s.sent_count==1);
 /* Proactive renewal inside500ms with a genuinely newer permit. */
 assert(r2_relay_precision_root(&s,cap(33600000,R2_CAPTURE_NOW),7,2100000000,3100000000,offset+2100000000,3,keys[0]));
 assert(s.origin.created_ns==2100000000&&s.origin.expires_ns==3100000000&&!s.sent_count);
 s.sent_count=1;s.sent_origin[0]=s.origin;s.sent[0].ticks=34000000;
 assert(r2_relay_precision_root(&s,cap(34400000,R2_CAPTURE_NOW),7,2150000000,3000000000,offset+2150000000,4,keys[0]));
 assert(s.origin.created_ns==2100000000&&s.origin.expires_ns==3000000000&&s.sent_count==1);
 struct r2_relay_prepared restricted;
 assert(r2_relay_precision_prepare(&s,cap(34400000,R2_CAPTURE_NOW),2150000,false,&restricted));
 assert(restricted.packet.count==1&&restricted.packet.origin.expires_ns==3000000000);

}
int main(void){
 for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++)for(unsigned b=0;b<16;b++)keys[r][c][b]=1+r*31+c*7+b*13;
 root_envelope_continuity();
 uint32_t retry_delay;
 assert(r2_relay_phase_retry(110760000,110720000,28297000,&retry_delay));
 assert(retry_delay>=2000&&retry_delay<64500);
 assert((110760000+retry_delay)%62500==(110720000+49828)%62500);
 assert(r2_relay_phase_retry(2000000,1900000,15625000,&retry_delay)&&retry_delay>=2000&&retry_delay<64500);
 assert(!r2_relay_phase_retry(1,2,15625000,&retry_delay));
 assert(!r2_relay_phase_retry(2,1,-1,&retry_delay));
 assert(!r2_relay_phase_retry(2,1,62500000,&retry_delay));

 r2_relay_precision_init(&root,0,0,1,11,1);r2_relay_precision_init(&relay,1,0,1,12,1);r2_relay_precision_init(&leaf,2,0,1,13,1);
 assert(r2_relay_precision_train(&root,1));assert(r2_relay_precision_train(&relay,1));
 uint64_t first=16000000;int64_t mapped,utc;
 assert(!root_event(first,0,true));assert(relay.bootstrap==1);
 assert(!r2_relay_precision_read(&relay,cap(first+32000,R2_CAPTURE_NOW),(first+32000)/16,&mapped,&utc));
 assert(root.frame_ns==ns(first)&&root.sent_count==0);
 /* Existing unchanged servo needs causal multi-second quality before delivery.
  * Native integration must not skip that dwell to make a quick fixture pass. */
 for(unsigned i=1;i<999;i++){
  uint64_t tick=first+i*1000000;root_event(tick,i,true);
  if(!r2_relay_precision_read(&relay,cap(tick+16000,R2_CAPTURE_NOW),(tick+16000)/16,&mapped,&utc))continue;
  struct r2_relay_packet p;if(!r2_relay_precision_packet(&relay,cap(tick+180000,R2_CAPTURE_NOW),(tick+180000)/16,&p))continue;
  authenticate(&p,2,relay.packet_proof);
  bool tx=r2_relay_precision_tx(&relay,&p,cap(tick+250144,R2_CAPTURE_RADIO),i,8,0,0,0);
  if(tx)assert(relay.phase_admitted&&relay.bound==UINT32_MAX);
  else assert(!p.count); /* Newly renewed envelope still acquiring its map. */
  r2_relay_precision_rx(&leaf,&p,relay.packet_proof,cap(tick+250288,R2_CAPTURE_RADIO),i,8,cap(tick+270000,R2_CAPTURE_NOW),(tick+270000)/16,offset+(int64_t)ns(tick+270000));
 }
 assert(relay.upstream.accepted>=4&&leaf.upstream.accepted>=3);
 fprintf(stderr,"ENGINE relay accepted=%u output=%u fit=%u rms=%u leaf accepted=%u output=%u fit=%u rms=%u origin=%llu maps=%u\n",
 relay.upstream.accepted,relay.upstream.servo.output.valid,relay.upstream.servo.fit_valid,r2_servo_output_quality_rms(&relay.upstream.servo),
 leaf.upstream.accepted,leaf.upstream.servo.output.valid,leaf.upstream.servo.fit_valid,r2_servo_output_quality_rms(&leaf.upstream.servo),
 (unsigned long long)leaf.origin.created_ns,leaf.map_head);
 uint64_t now=first+998*1000000+290000;
 assert(r2_relay_precision_read(&leaf,cap(now,R2_CAPTURE_NOW),now/16,&mapped,&utc));
 assert(utc==offset+mapped&&leaf.bound==UINT32_MAX);
 /* A signed same-domain restriction preserves existing physical maps while
  * immediately narrowing every matching proof; wider replay is refused. */
 struct r2_relay_precision narrowed=relay,root_copy=root;struct r2_relay_packet narrow,wide;uint8_t narrow_proof[32];
 assert(r2_relay_precision_packet(&root_copy,cap(now,R2_CAPTURE_NOW),now/16,&wide));
 narrow=wide;narrow.origin.expires_ns-=1000;
 assert(!r2_relay_origin_sign(narrow_proof,&narrow.origin,keys[0]));authenticate(&narrow,1,narrow_proof);
 (void)r2_relay_precision_rx(&narrowed,&narrow,narrow_proof,cap(now+10,R2_CAPTURE_RADIO),1001,3,cap(now+1000,R2_CAPTURE_NOW),(now+1000)/16,offset+(int64_t)ns(now+1000));
 assert(narrowed.origin.expires_ns==narrow.origin.expires_ns);
 unsigned restricted_maps=0;
 for(unsigned j=0;j<R2_RELAY_MAPS;j++)if(narrowed.map[j].used&&narrowed.map[j].origin.created_ns==narrow.origin.created_ns){
  ++restricted_maps;assert(narrowed.map[j].origin.expires_ns==narrow.origin.expires_ns);assert(!memcmp(narrowed.map[j].proof,narrow_proof,32));
 }
 assert(restricted_maps);
 wide.route.publication++;
 assert(!r2_relay_precision_rx(&narrowed,&wide,root_copy.packet_proof,cap(now+2000,R2_CAPTURE_RADIO),1002,3,cap(now+3000,R2_CAPTURE_NOW),(now+3000)/16,offset+(int64_t)ns(now+3000)));
 assert(narrowed.origin.expires_ns==narrow.origin.expires_ns);

 struct r2_relay_origin selected;assert(r2_relay_precision_selected_origin(&leaf,cap(now,R2_CAPTURE_NOW),now/16,&selected));
 assert(selected.boot==11&&selected.source_epoch==7&&selected.expires_ns>(uint64_t)mapped);
 assert(r2_relay_precision_valid_for(&leaf,cap(now,R2_CAPTURE_NOW),now/16,50000)==50000);
 struct r2_relay_precision deadline=leaf;
 for(unsigned j=0;j<R2_RELAY_MAPS;j++)if(deadline.map[j].used){
  int64_t relative;assert(r2_servo_mapped(&deadline.map[j].map,now,&relative));
  deadline.map[j].origin.expires_ns=(uint64_t)relative+1000000;
 }
 uint32_t lifetime=r2_relay_precision_valid_for(&deadline,cap(now,R2_CAPTURE_NOW),now/16,50000);
 assert(lifetime>0&&lifetime<1000); /* Exact expiry cannot become a50ms lease. */
 /* Admission scheduling must read the actually selected map at the proposed
  * window end. A newer unbacked envelope cannot make an expiring map cover it. */
 assert(r2_relay_precision_read(&leaf,cap(now+6400000,R2_CAPTURE_NOW),(now+6400000)/16,&mapped,&utc));
 deadline.origin=leaf.origin;deadline.origin.expires_ns+=1000000000;
 assert(!r2_relay_precision_read(&deadline,cap(now+6400000,R2_CAPTURE_NOW),(now+6400000)/16,&mapped,&utc));

 /* Pure preparation consumes neither train nor estimator history. Delayed
  * commit must recheck the original capture deadline and generation. */
 struct r2_relay_precision prepared_before=relay;
 struct r2_relay_prepared prepared;
 assert(r2_relay_precision_prepare(&relay,cap(now,R2_CAPTURE_NOW),now/16,true,&prepared));
 assert(!memcmp(&prepared_before,&relay,sizeof relay));
 assert(prepared.packet.route.train==relay.train+1&&!prepared.packet.count);
 assert(!r2_relay_precision_commit(&relay,&prepared,cap(prepared.valid_until_tick,R2_CAPTURE_NOW)));
 assert(!memcmp(&prepared_before,&relay,sizeof relay));
 struct r2_relay_precision committed=relay;
 assert(r2_relay_precision_commit(&committed,&prepared,cap(now,R2_CAPTURE_NOW)));
 assert(committed.train==relay.train+1&&committed.upstream.accepted==relay.upstream.accepted);
 assert(!r2_relay_precision_commit(&committed,&prepared,cap(now,R2_CAPTURE_NOW)));
 assert(root.revision>1&&root.map_generation==1); /* PPS revision != incarnation. */
 assert(r2_relay_precision_read(&leaf,cap(now+9600000,R2_CAPTURE_NOW),(now+9600000)/16,&mapped,&utc));
 assert(!r2_relay_precision_read(&relay,cap(now+9600000,R2_CAPTURE_NOW),(now+9600000)/16,&mapped,&utc));
 /* Accepted input without new actuation cannot freshen an old map. */
 struct r2_relay_precision unactuated=relay;
 uint64_t late=now+4800000;
 for(unsigned j=0;j<R2_RELAY_MAPS;j++)if(unactuated.map[j].used)unactuated.map[j].accepted=late;
 assert(!r2_relay_precision_read(&unactuated,cap(late,R2_CAPTURE_NOW),late/16,&mapped,&utc));
 /* A same-clock-domain transport restart retires tuples/output, not fit.
  * No fresh correction means no exposed map; old trains remain fenced. */
 struct r2_relay_precision saved_root=root,saved_relay=relay;
 unsigned resets=relay.upstream.servo.resets;
 assert(r2_relay_precision_train(&root,1));
 uint64_t restart=now+R2_CAPTURE_HZ;
 root_event(restart,1000,true);
 assert(relay.upstream.servo.fit_valid&&relay.upstream.servo.resets==resets);
 assert(!relay.upstream.last_available&&!r2_relay_precision_read(&relay,cap(restart+32000,R2_CAPTURE_NOW),(restart+32000)/16,&mapped,&utc));
 root_event(restart+1000000,1001,true);
 assert(relay.upstream.servo.resets==resets&&r2_relay_precision_read(&relay,cap(restart+1032000,R2_CAPTURE_NOW),(restart+1032000)/16,&mapped,&utc));
 assert(!r2_radio_precision_rebind_train(&relay.upstream,relay.upstream.train,1));
 assert(!r2_radio_precision_rebind_train(&relay.upstream,relay.upstream.train+1,2));
 struct r2_relay_packet replay;assert(r2_relay_precision_packet(&root,cap(restart+1100000,R2_CAPTURE_NOW),(restart+1100000)/16,&replay));
 --replay.route.train;authenticate(&replay,1,root.proof);
 assert(!r2_relay_precision_rx(&relay,&replay,root.proof,cap(restart+1110000,R2_CAPTURE_RADIO),1002,3,cap(restart+1120000,R2_CAPTURE_NOW),(restart+1120000)/16,offset+(int64_t)ns(restart+1120000)));
 uint64_t expired=relay.origin_until_tick+2*R2_CAPTURE_HZ;
 assert(!r2_relay_precision_read(&relay,cap(expired,R2_CAPTURE_NOW),expired/16,&mapped,&utc));
 unsigned points=relay.upstream.servo.count;r2_relay_precision_pause(&relay);
 assert(relay.upstream.servo.count==points&&relay.upstream.servo.fit_valid);
 root=saved_root;relay=saved_relay;
 /* A temporary fresh-map gap keeps authenticated count0 discovery on the
  * same train, never an old correction, renewed expiry, or estimator reset. */
 struct r2_relay_precision acquiring=relay;struct r2_relay_packet acquisition;
 uint64_t expiry=acquiring.origin.expires_ns;uint32_t prior_accepted=acquiring.upstream.accepted;
 assert(r2_relay_precision_packet(&acquiring,cap(now+4800000,R2_CAPTURE_NOW),(now+4800000)/16,&acquisition));
 authenticate(&acquisition,2,acquiring.packet_proof);
 assert(!acquisition.count&&!acquiring.phase_admitted&&!acquiring.sent_count&&acquisition.route.train==relay.train);
 assert(acquisition.origin.expires_ns==expiry&&acquiring.upstream.accepted==prior_accepted&&acquiring.upstream.servo.output.valid);
 assert(!r2_relay_precision_tx(&acquiring,&acquisition,cap(now+4900000,R2_CAPTURE_RADIO),22,8,0,0,0));
 assert(!acquiring.phase_rejected); /* Missing map is not measured off-phase. */
 /* A merely heard newer origin cannot poison publication highwater while
  * a same-scope older mapped origin is still live. Count0 retains its proof
  * and original expiry; a later mapped update resumes without rollback. */
 struct r2_relay_precision publication=relay;struct r2_relay_packet before,gap,after;
 assert(r2_relay_precision_packet(&publication,cap(now,R2_CAPTURE_NOW),now/16,&before));
 uint64_t frozen=publication.published_until_tick;
 struct r2_relay_precision restricted_gap=publication;restricted_gap.origin=before.origin;restricted_gap.origin.expires_ns-=1000;
 restricted_gap.origin_until_tick=publication.published_until_tick-16;
 assert(!r2_relay_origin_sign(restricted_gap.proof,&restricted_gap.origin,keys[0]));memset(restricted_gap.map,0,sizeof restricted_gap.map);
 struct r2_relay_prepared restricted_publish;
 assert(r2_relay_precision_prepare(&restricted_gap,cap(now,R2_CAPTURE_NOW),now/16,false,&restricted_publish));
 assert(!restricted_publish.packet.count&&restricted_publish.packet.origin.created_ns==before.origin.created_ns&&restricted_publish.packet.origin.expires_ns==restricted_gap.origin.expires_ns);
 assert(restricted_publish.published_until_tick==restricted_gap.origin_until_tick);
 authenticate(&restricted_publish.packet,2,restricted_publish.proof);

 publication.origin.created_ns+=1000000;publication.origin.expires_ns+=1000000;
 struct r2_relay_map_record saved[R2_RELAY_MAPS];memcpy(saved,publication.map,sizeof saved);
 memset(publication.map,0,sizeof publication.map);
 assert(r2_relay_precision_packet(&publication,cap(now+1000,R2_CAPTURE_NOW),(now+1000)/16,&gap));
 assert(!gap.count&&gap.origin.created_ns==before.origin.created_ns&&publication.published_until_tick==frozen);
 authenticate(&gap,2,publication.packet_proof);
 memcpy(publication.map,saved,sizeof saved);
 assert(r2_relay_precision_packet(&publication,cap(now+2000,R2_CAPTURE_NOW),(now+2000)/16,&after));
 assert(after.origin.created_ns==before.origin.created_ns&&publication.published_until_tick<=frozen);
 memset(publication.map,0,sizeof publication.map);
 assert(!r2_relay_precision_packet(&publication,cap(frozen,R2_CAPTURE_NOW),frozen/16,&gap));
 /* A newer authenticated envelope advances before the OLD deadline with
  * no map, same train, pure prepare and a separately bounded new deadline. */
 publication.origin.created_ns=before.origin.created_ns+500000000;
 publication.origin.expires_ns=before.origin.expires_ns+500000000;
 publication.origin_until_tick=frozen+R2_CAPTURE_HZ/2;
 assert(!r2_relay_origin_sign(publication.proof,&publication.origin,keys[0]));
 uint64_t advance_at=frozen-R2_CAPTURE_HZ/16;
 struct r2_relay_precision unchanged=publication;struct r2_relay_prepared next;
 struct r2_relay_precision old_ready=publication;memcpy(old_ready.map,saved,sizeof saved);
 for(unsigned j=0;j<R2_RELAY_MAPS;j++)if(old_ready.map[j].used)delivered(&old_ready.map[j],advance_at);
 assert(r2_relay_precision_prepare(&old_ready,cap(advance_at,R2_CAPTURE_NOW),advance_at/16,false,&next));
 assert(!next.mapped&&!next.packet.count&&next.packet.origin.created_ns==publication.origin.created_ns);
 assert(r2_relay_precision_prepare(&publication,cap(advance_at,R2_CAPTURE_NOW),advance_at/16,false,&next));
 assert(!memcmp(&unchanged,&publication,sizeof publication));
 assert(!next.mapped&&!next.packet.count&&next.packet.origin.created_ns==publication.origin.created_ns);
 assert(next.published_until_tick==publication.origin_until_tick&&next.packet.route.train==publication.train);
 assert(r2_relay_precision_commit(&publication,&next,cap(advance_at,R2_CAPTURE_NOW)));
 authenticate(&next.packet,2,publication.packet_proof);
 assert(publication.published_origin_valid&&publication.published_origin.created_ns==next.packet.origin.created_ns);
 assert(publication.upstream.accepted==unchanged.upstream.accepted);
 memcpy(publication.map,saved,sizeof saved);
 assert(r2_relay_precision_packet(&publication,cap(advance_at+1000,R2_CAPTURE_NOW),(advance_at+1000)/16,&gap));
 assert(!gap.count&&gap.origin.created_ns==next.packet.origin.created_ns); /* no older-map rollback */
 assert(publication.published_until_tick==next.published_until_tick);
 /* A genuinely refreshed map for the new origin restores mapped publication
  * without a transport generation change or old-origin correction. */
 for(unsigned j=0;j<R2_RELAY_MAPS;j++)if(publication.map[j].used){
  publication.map[j].origin=publication.origin;delivered(&publication.map[j],advance_at+2000);
  memcpy(publication.map[j].proof,publication.proof,32);
 }
 assert(r2_relay_precision_prepare(&publication,cap(advance_at+2000,R2_CAPTURE_NOW),(advance_at+2000)/16,false,&next));
 assert(next.mapped&&!next.packet.count&&next.packet.route.train==publication.train);
 assert(r2_relay_precision_commit(&publication,&next,cap(advance_at+2000,R2_CAPTURE_NOW)));
 uint64_t tx_tick=advance_at+3000;int64_t mapped_ns;
 assert(r2_servo_mapped(&publication.upstream.servo.output,tx_tick,&mapped_ns));
 uint64_t phase=((uint64_t)mapped_ns-publication.origin.frame_ns)%R2_RELAY_FRAME_NS;
 tx_tick+=((R2_RELAY_SLOT_NS+R2_RELAY_FRAME_NS-phase)%R2_RELAY_FRAME_NS)*2/125;
 assert(r2_relay_precision_tx(&publication,&next.packet,cap(tx_tick,R2_CAPTURE_RADIO),23,8,0,0,0));
 assert(r2_relay_precision_packet(&publication,cap(tx_tick+1600,R2_CAPTURE_NOW),(tx_tick+1600)/16,&after));
 assert(after.count==1&&after.route.train==unchanged.train&&after.origin.created_ns==publication.origin.created_ns);
 authenticate(&after,2,publication.packet_proof);
 /* No new envelope cannot rescue an already-expired installed identity. */
 struct r2_relay_precision no_new=unchanged;
 no_new.origin=no_new.published_origin;no_new.origin_until_tick=frozen+R2_CAPTURE_HZ;
 assert(!r2_relay_precision_prepare(&no_new,cap(frozen,R2_CAPTURE_NOW),frozen/16,false,&next));
 /* After the old PA has drained, a NEW train may acquire using the fresh
  * authenticated origin despite expired downstream envelope bookkeeping.
  * Same-train expiry is still a refusal; count0 grants no correction/map. */
 struct r2_relay_precision restarted=unchanged;memset(restarted.map,0,sizeof restarted.map);
 restarted.origin=publication.origin;restarted.origin_until_tick=frozen+R2_CAPTURE_HZ/2;
 assert(!r2_relay_origin_sign(restarted.proof,&restarted.origin,keys[0]));
 uint64_t resumed_at=frozen+16000;
 assert(!r2_relay_precision_prepare(&restarted,cap(resumed_at,R2_CAPTURE_NOW),resumed_at/16,false,&next));
 unsigned saved_accepts=restarted.upstream.accepted;
 assert(r2_relay_precision_prepare(&restarted,cap(resumed_at,R2_CAPTURE_NOW),resumed_at/16,true,&next));
 assert(!next.mapped&&!next.packet.count&&next.packet.route.train==restarted.train+1);
 assert(next.packet.origin.created_ns==restarted.origin.created_ns&&next.published_until_tick==restarted.origin_until_tick);
 assert(r2_relay_precision_commit(&restarted,&next,cap(resumed_at,R2_CAPTURE_NOW)));
 authenticate(&next.packet,2,restarted.packet_proof);
 assert(restarted.upstream.accepted==saved_accepts&&restarted.upstream.servo.fit_valid);
 assert(!r2_relay_precision_tx(&restarted,&next.packet,cap(resumed_at+1000,R2_CAPTURE_RADIO),24,8,0,0,0));
 restarted.origin_until_tick=resumed_at;
 assert(!r2_relay_precision_prepare(&restarted,cap(resumed_at,R2_CAPTURE_NOW),resumed_at/16,true,&next));
 r2_relay_precision_withdraw(&publication);
 assert(!publication.published_origin_valid&&!publication.published_until_tick&&!publication.published_origin.boot);
 for(unsigned change=0;change<4;change++){
  struct r2_relay_precision reset=relay;struct r2_relay_packet incoming={.origin=relay.origin,.route=relay.remote};
  reset.published_origin_valid=true;reset.published_origin=relay.origin;reset.published_until_tick=now+100000;
  if(change==0){incoming.origin.boot++;incoming.route.boot=incoming.origin.boot;}
  if(change==1)incoming.origin.source_epoch++;
  if(change>=2)incoming.route.train++;
  if(change==3)incoming.route.capture_epoch++;
  incoming.route.publication+=100;
  uint8_t proof[32];assert(!r2_relay_origin_sign(proof,&incoming.origin,keys[0]));authenticate(&incoming,1,proof);
  (void)r2_relay_precision_rx(&reset,&incoming,proof,cap(now+3000,R2_CAPTURE_RADIO),22,3,cap(now+4000,R2_CAPTURE_NOW),(now+4000)/16,offset+(int64_t)ns(now+4000));
  if(change==2){
   assert(reset.published_origin_valid&&reset.published_until_tick==now+100000);
   assert(reset.upstream.servo.fit_valid&&!reset.upstream.last_available&&!reset.map_head);
   assert(!r2_relay_precision_read(&reset,cap(now+5000,R2_CAPTURE_NOW),(now+5000)/16,&mapped,&utc));
  }else assert(!reset.published_origin_valid&&!reset.published_until_tick&&!reset.published_origin.boot);
 }
 /* Same correction across distinct publications cannot train twice. */
 uint32_t accepted=leaf.upstream.accepted;
 struct r2_relay_packet p;assert(r2_relay_precision_packet(&relay,cap(now,R2_CAPTURE_NOW),now/16,&p));
 authenticate(&p,2,relay.packet_proof);
 r2_relay_precision_rx(&leaf,&p,relay.packet_proof,cap(now+20000,R2_CAPTURE_RADIO),20,8,cap(now+30000,R2_CAPTURE_NOW),(now+30000)/16,offset+(int64_t)ns(now+30000));
 ++p.route.publication;authenticate(&p,2,relay.packet_proof);
 r2_relay_precision_rx(&leaf,&p,relay.packet_proof,cap(now+40000,R2_CAPTURE_RADIO),21,8,cap(now+50000,R2_CAPTURE_NOW),(now+50000)/16,offset+(int64_t)ns(now+50000));
 assert(leaf.upstream.accepted<=accepted+1&&leaf.duplicates>0);
 /* A future delivered map cannot retroactively project an earlier TX. */
 struct r2_relay_precision causal=relay;memset(causal.map,0,sizeof causal.map);
 causal.map[0]=(struct r2_relay_map_record){.map=relay.upstream.servo.output,.origin=relay.origin,.accepted=now+100000,.bound=UINT32_MAX,.used=true};
 causal.map[0].map.delivery=now+100000;
 assert(!r2_relay_precision_tx(&causal,&p,cap(now+80000,R2_CAPTURE_RADIO),22,8,0,0,0));
 /* No new correction outside the predeclared ONE-relay phase cell. */
 assert(!r2_relay_precision_tx(&relay,&p,cap(first+999*1000000+481144,R2_CAPTURE_RADIO),22,8,0,0,0));
 assert(!relay.phase_admitted&&relay.sent_count==0);
 /* Actual timer reset on new root boot, followed by an unseen old-boot train,
  * cannot roll back source scope or refresh a map. */
 struct r2_relay_packet old=p,new=p;new.origin.boot=99;new.origin.source_epoch=1;
 new.origin.created_ns=1000000;new.origin.expires_ns=1001000000;new.origin.frame_ns=1000000;
 new.origin.utc_offset_ns=offset+(int64_t)ns(now)-1000000;new.count=0;memset(new.correction,0,sizeof new.correction);
 new.route.publication+=2;assert(!r2_relay_origin_sign(relay.proof,&new.origin,keys[0]));authenticate(&new,2,relay.proof);
 assert(r2_relay_precision_rx(&leaf,&new,relay.proof,cap(now+60000,R2_CAPTURE_RADIO),23,8,cap(now+70000,R2_CAPTURE_NOW),(now+70000)/16,offset+(int64_t)ns(now+70000)));
 old.origin.source_epoch=99;old.route.train=999;old.route.capture_epoch=999;old.count=0;memset(old.correction,0,sizeof old.correction);
 assert(!r2_relay_origin_sign(relay.proof,&old.origin,keys[0]));authenticate(&old,2,relay.proof);
 assert(!r2_relay_precision_rx(&leaf,&old,relay.proof,cap(now+80000,R2_CAPTURE_RADIO),24,8,cap(now+90000,R2_CAPTURE_NOW),(now+90000)/16,offset+(int64_t)ns(now+90000)));
 assert(leaf.root_boot==99&&!leaf.upstream.servo.output.valid);
 /* A retired relay boot cannot return under an unseen train either. */
 new.route.boot=121;new.route.publication+=3;
 assert(!r2_relay_origin_sign(relay.proof,&new.origin,keys[0]));authenticate(&new,2,relay.proof);
 assert(r2_relay_precision_rx(&leaf,&new,relay.proof,cap(now+100000,R2_CAPTURE_RADIO),25,8,cap(now+110000,R2_CAPTURE_NOW),(now+110000)/16,offset+(int64_t)ns(now+110000)));
 old=new;old.route.boot=12;old.route.train=1111;old.route.publication+=3;authenticate(&old,2,relay.proof);
 assert(!r2_relay_precision_rx(&leaf,&old,relay.proof,cap(now+120000,R2_CAPTURE_RADIO),26,8,cap(now+130000,R2_CAPTURE_NOW),(now+130000)/16,offset+(int64_t)ns(now+130000)));
 assert(leaf.remote.boot==121);
 /* Bounded retired-root history refuses further scope replacement at capacity. */
 for(unsigned i=0;i<15;i++){
  new.origin.boot=200+i;new.route.publication+=2;
  assert(!r2_relay_origin_sign(relay.proof,&new.origin,keys[0]));authenticate(&new,2,relay.proof);
  assert(r2_relay_precision_rx(&leaf,&new,relay.proof,cap(now+140000+20000*i,R2_CAPTURE_RADIO),30+i,8,
         cap(now+150000+20000*i,R2_CAPTURE_NOW),(now+150000+20000*i)/16,offset+(int64_t)ns(now+150000+20000*i)));
 }
 new.origin.boot=999;assert(!r2_relay_origin_sign(relay.proof,&new.origin,keys[0]));authenticate(&new,2,relay.proof);
 assert(!r2_relay_precision_rx(&leaf,&new,relay.proof,cap(now+500000,R2_CAPTURE_RADIO),60,8,
        cap(now+510000,R2_CAPTURE_NOW),(now+510000)/16,offset+(int64_t)ns(now+510000))&&leaf.failed);
 relay.train=UINT32_MAX;assert(!r2_relay_precision_train(&relay,1)&&relay.failed);
 puts("PASS native2 bootstrap, causal relay/leaf, UNKNOWN, repeat correction, phase, reset/retired unseen root scope, wrap refusal");
 return 0;
}
