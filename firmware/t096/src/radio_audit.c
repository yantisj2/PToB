#include "radio_audit.h"
#include <zephyr/kernel.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
/* About one original/s at16Hz, independent of witness errors or channel. */
bool r2_radio_audit_selected(uint16_t event){return !(event&15u);}
static struct k_spinlock audit_lock;
static struct r2_radio_audit_record audit[R2_RADIO_AUDIT_RECORDS];
/* Narrow bounded indices fund the lease without increasing mutable storage. */
static uint8_t audit_head,audit_count;
static uint32_t audit_capture_until;
static bool audit_capture_active;
static struct r2_radio_audit_status audit_stats;
static bool capturing(void){
 if(audit_capture_active&&(int32_t)(audit_capture_until-k_uptime_get_32())<=0)audit_capture_active=false;
 return audit_capture_active;
}
int r2_radio_audit_capture(uint32_t ms){
 if(ms>900000)return -EINVAL;
 k_spinlock_key_t key=k_spin_lock(&audit_lock);
 audit_capture_until=k_uptime_get_32()+ms;audit_capture_active=ms!=0;
 k_spin_unlock(&audit_lock,key);return 0;
}
bool r2_radio_audit_capturing(void){
 k_spinlock_key_t key=k_spin_lock(&audit_lock);bool active=capturing();k_spin_unlock(&audit_lock,key);return active;
}
void r2_radio_audit_put(const struct r2_radio_audit_record *record){
 if(!record||(record->kind!=R2_AUDIT_HOLD&&!r2_radio_audit_selected(record->event)))return;
 k_spinlock_key_t key=k_spin_lock(&audit_lock);
 if(record->kind==R2_AUDIT_HOLD&&!capturing()){k_spin_unlock(&audit_lock,key);return;}
 if(audit_stats.produced==UINT32_MAX){k_spin_unlock(&audit_lock,key);return;}
 if(audit_count==R2_RADIO_AUDIT_RECORDS){if(audit_stats.overwritten<UINT32_MAX)++audit_stats.overwritten;}else ++audit_count;
 audit[audit_head]=*record;audit[audit_head].sequence=++audit_stats.produced;
 audit_head=(audit_head+1)%R2_RADIO_AUDIT_RECORDS;k_spin_unlock(&audit_lock,key);
}
bool r2_radio_audit_read(uint32_t after,struct r2_radio_audit_record *record,struct r2_radio_audit_status *status){
 bool found=false;k_spinlock_key_t key=k_spin_lock(&audit_lock);*status=audit_stats;
 for(unsigned i=0;i<audit_count;i++){
  const struct r2_radio_audit_record *q=&audit[(audit_head+R2_RADIO_AUDIT_RECORDS-audit_count+i)%R2_RADIO_AUDIT_RECORDS];
  if(q->sequence>after){*record=*q;found=true;break;}
 }
 k_spin_unlock(&audit_lock,key);return found;
}
int r2_radio_audit_trace(char *out,size_t size,uint32_t after){
 struct r2_radio_audit_record q;struct r2_radio_audit_status st={0};uint32_t cursor=after,missed=0;unsigned rows=0;
 bool found=r2_radio_audit_read(after,&q,&st);
 if(after==UINT32_MAX){cursor=st.produced;found=false;}
 int n=snprintf(out,size,"{\"schema\":3,\"records\":[");
 for(unsigned i=0;i<R2_RADIO_AUDIT_RECORDS&&found&&n>0&&(size_t)n<size;i++){
  if(q.sequence>cursor+1)missed+=q.sequence-cursor-1;
  cursor=q.sequence;
  if(q.kind==R2_AUDIT_HOLD){
   int k=snprintf(out+n,size-(size_t)n,"%s[%u,%llu,%llu,%llu,%llu,%u,%u,%u,%u]",rows?",":"",q.sequence,
    (unsigned long long)q.generation,(unsigned long long)q.tick,(unsigned long long)q.ns,
    (unsigned long long)q.available,q.channel,q.raw,q.epoch,q.root_epoch);
   if(k<0)return -ENOSPC;
   n+=k;++rows;
  }
  found=r2_radio_audit_read(cursor,&q,&st);
 }
 if(n>0&&(size_t)n<size){
  int k=snprintf(out+n,size-(size_t)n,"],\"cursor\":%u,\"last\":%u,\"missed\":%u,\"capturing\":%s}",cursor,st.produced,missed,r2_radio_audit_capturing()?"true":"false");
  if(k<0)return -ENOSPC;
  n+=k;
 }
 return n<0||(size_t)n>=size?-ENOSPC:n;
}
