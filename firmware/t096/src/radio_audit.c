#include "radio_audit.h"
#include <zephyr/kernel.h>
#include <limits.h>
/* About one original/s at16Hz, independent of witness errors or channel. */
bool r2_radio_audit_selected(uint16_t event){return !(event&15u);}
static struct k_spinlock audit_lock;
static struct r2_radio_audit_record audit[R2_RADIO_AUDIT_RECORDS];
static unsigned audit_head,audit_count;
static struct r2_radio_audit_status audit_stats;
void r2_radio_audit_put(const struct r2_radio_audit_record *record){
 if(!record||!r2_radio_audit_selected(record->event))return;
 k_spinlock_key_t key=k_spin_lock(&audit_lock);
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
