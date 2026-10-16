#include "election_policy.h"
#include <stddef.h>
int r2_quality_compare(const struct r2_clock_quality *a,const struct r2_clock_quality *b)
{
#define FIELD(f) do{if(a->f!=b->f)return a->f<b->f?-1:1;}while(0)
 FIELD(priority1);FIELD(clock_class);FIELD(accuracy);FIELD(variance);FIELD(priority2);FIELD(identity);
#undef FIELD
 return 0;
}
void r2_admission_init(struct r2_source_admission *s,uint64_t now)
{*s=(struct r2_source_admission){.boot_at=now,.now=now};}
bool r2_admission_update(struct r2_source_admission *s,bool gm,bool gnss,
 bool independent,uint32_t epoch,uint64_t expiry,uint64_t now)
{
 if(s->failed||now<s->now){s->failed=true;s->eligible=s->observing=false;return false;}
 s->now=now;
 if(!gm||!gnss||!independent||!epoch||expiry<=now){s->eligible=s->observing=false;return false;}
 if(!s->observing||epoch!=s->source_epoch||now>=s->last_expiry){s->stable_at=now;s->source_epoch=epoch;s->observing=true;}
 s->last_expiry=expiry;
 s->eligible=now-s->boot_at>=R2_ELECTION_STABLE_US&&now-s->stable_at>=R2_ELECTION_STABLE_US;
 return s->eligible;
}
struct r2_elected_roles r2_election_rank(const struct r2_ranked_clock *c,unsigned count)
{
 struct r2_elected_roles result={R2_ELECTION_NONE,R2_ELECTION_NONE,R2_ELECTION_NONE};
 if(!c||count>4)return result;
 const struct r2_ranked_clock *first=NULL,*second=NULL;
 for(unsigned i=0;i<count;i++){
  if(!c[i].gm_capable||!c[i].gnss_capable||!c[i].independent_ready||!c[i].authenticated||!c[i].boot||!c[i].quality.identity||c[i].wire==R2_ELECTION_NONE)continue;
  /* Ambiguous duplicate identities/slots fail closed, never choose by order. */
  for(unsigned j=0;j<i;j++)if(c[j].wire==c[i].wire||c[j].quality.identity==c[i].quality.identity)return result;
  if(!first||r2_quality_compare(&c[i].quality,&first->quality)<0){second=first;first=&c[i];}
  else if(!second||r2_quality_compare(&c[i].quality,&second->quality)<0)second=&c[i];
 }
 if(first)result.gm=first->wire;
 if(second)result.coordinator=result.standby=second->wire;
 return result;
}
