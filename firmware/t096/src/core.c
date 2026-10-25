/* SPDX-License-Identifier: Apache-2.0 */
#include "core.h"
#include "ui_pages.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static bool hex(const char *s, size_t n, uint64_t *value)
{
  if (strlen(s) != n) return false;
  uint64_t v = 0;
  for (size_t i = 0; i < n; ++i) {
    unsigned digit = s[i] >= '0' && s[i] <= '9' ? (unsigned)(s[i]-'0') :
                     s[i] >= 'a' && s[i] <= 'f' ? (unsigned)(s[i]-'a'+10) : 16;
    if (digit == 16) return false;
    if (n <= 16) v = (v << 4) | digit;
  }
  if (value) *value = v;
  return true;
}
int r2_parse(const char *line, struct r2_request *out)
{
  if (!line || !out || strlen(line) >= R2_COMMAND_BYTES) return -EINVAL;
  char copy[R2_COMMAND_BYTES], *parts[8];
  memcpy(copy, line, strlen(line)+1);
  size_t count = 0;
  char *p = copy;
  while (*p) {
    if (count == 8 || *p == ' ') return -EINVAL;
    parts[count++] = p;
    while (*p && *p != ' ') ++p;
    if (*p) { *p++ = 0; if (!*p) return -EINVAL; }
  }
  uint64_t id;
  if (count < 6 || strcmp(parts[0], "r2") || !hex(parts[1],8,&id) || !id ||
      !hex(parts[2],16,NULL) || !hex(parts[3],64,NULL) || !hex(parts[4],16,NULL)) return -EINVAL;
  memset(out, 0, sizeof(*out));
  out->id = (uint32_t)id;
  memcpy(out->silicon,parts[2],17); memcpy(out->source,parts[3],65);
  (void)hex(parts[4],16,&out->boot);
  if (!strcmp(parts[5],"status")) out->op = R2_STATUS;
  else if (!strcmp(parts[5],"diag")) out->op = R2_DIAG;
  else if (!strcmp(parts[5],"mark")) out->op = R2_MARK;
  else if (!strcmp(parts[5],"ble")) out->op = R2_BLE;
  else if (!strcmp(parts[5],"network")) out->op = R2_NETWORK;
  else if (!strcmp(parts[5],"ui")) out->op = R2_UI;
  else if (!strcmp(parts[5],"precision")) out->op = R2_PRECISION;
  else if (!strcmp(parts[5],"gm")) out->op = R2_GM;
  else if (!strcmp(parts[5],"join")) out->op = R2_JOIN;
  else if (!strcmp(parts[5],"name")) out->op = R2_NAME;
  else if (!strcmp(parts[5],"relay")) out->op = R2_RELAY;
  else if (!strcmp(parts[5],"shadow")) out->op = R2_SHADOW;
  else if (!strcmp(parts[5],"witness")) out->op = R2_WITNESS;
  else if (!strcmp(parts[5],"radio-audit")) out->op = R2_RADIO_AUDIT;
  else if (!strcmp(parts[5],"profile")) out->op = R2_PROFILE;
#ifdef R2_PC_PROFILE
  else if (!strcmp(parts[5],"pc")) out->op = R2_PC;
  else if (!strcmp(parts[5],"pc-run")) out->op = R2_PC_RUN;
  else if (!strcmp(parts[5],"pc-cycle")) out->op = R2_PC_CYCLE;
  else if (!strcmp(parts[5],"pc-align")) out->op = R2_PC_ALIGN;
#endif
#ifdef R2_RC_PROFILE
  else if (!strcmp(parts[5],"rc-parent")) out->op = R2_RC_PARENT;
#endif
  else if (!strcmp(parts[5],"route")) out->op = R2_ROUTE;
  else if (!strcmp(parts[5],"clock")) out->op = R2_CLOCK;
  else if (!strcmp(parts[5],"trace")) out->op = R2_TRACE;
#ifdef R2_LORA
  else if (!strcmp(parts[5],"lora")) out->op = R2_LORA_CONTROL;
#endif
  else if (!strcmp(parts[5],"events")) out->op = R2_EVENTS;
  else if (!strcmp(parts[5],"prepare")) out->op = R2_PREPARE;
  else if (!strcmp(parts[5],"commit")) out->op = R2_COMMIT;
  else return -ENOTSUP;
#ifdef R2_PC_PROFILE
  if(out->op==R2_PC)return count==6?0:count==7&&hex(parts[6],8,&out->nonce)?0:-EINVAL;
  if(out->op==R2_PC_RUN){uint64_t mode;if(count!=8||!hex(parts[6],8,&mode)||mode<1||mode>4||!hex(parts[7],16,&out->nonce))return -EINVAL;out->trace_page=(unsigned)mode;return 0;}
  if(out->op==R2_PC_CYCLE||out->op==R2_PC_ALIGN){uint64_t cycles;if(count!=8||!hex(parts[6],8,&cycles)||!cycles||cycles>128||!hex(parts[7],16,&out->nonce))return -EINVAL;out->seconds=(uint32_t)cycles;return 0;}
#endif
  if (out->op == R2_STATUS || out->op == R2_CLOCK || out->op == R2_NETWORK || out->op == R2_MARK || out->op == R2_PRECISION || out->op==R2_ROUTE || out->op==R2_JOIN) return count == 6 ? 0 : -EINVAL;
  if(out->op==R2_PROFILE){
   if(count!=7)return -EINVAL;
   if(!strcmp(parts[6],"gm"))out->nonce=0;else if(!strcmp(parts[6],"rc"))out->nonce=1;else if(!strcmp(parts[6],"toggle"))out->nonce=2;else return -EINVAL;
   return 0;
  }
#ifdef R2_LORA
  if(out->op==R2_LORA_CONTROL){
   out->seconds=0;if(count==6)return 0;
   if(count!=7||!hex(parts[6],2,&out->nonce)||out->nonce>1)return -EINVAL;
   out->seconds=1;return 0;
  }
#endif
  if(out->op==R2_GM)return count==7&&hex(parts[6],2,&out->nonce)&&out->nonce<=
#ifdef R2_GM7_NATIVE
   8
#else
   4
#endif
   ?0:-EINVAL;
  if(out->op==R2_EVENTS)return count==7&&hex(parts[6],8,&out->nonce)?0:-EINVAL;
#ifdef R2_RC_PROFILE
  if(out->op==R2_RC_PARENT){
   if(count==6){out->seconds=1;return 0;}
   if(count!=7)return -EINVAL;
   if(!strcmp(parts[6],"auto"))return 0;
   return hex(parts[6],16,&out->nonce)&&out->nonce?0:-EINVAL;
  }
#endif
  if(out->op==R2_NAME){
   if(count!=7){return -EINVAL;}
   size_t n=strlen(parts[6]);if(!n||n>48||n%2)return -EINVAL;
   for(size_t i=0;i<n/2;i++){char value[3]={parts[6][2*i],parts[6][2*i+1],0};uint64_t byte;if(!hex(value,2,&byte)||!byte)return -EINVAL;out->name[i]=(char)byte;}
   return 0;
  }
  if(out->op==R2_RELAY){uint64_t seconds;
   if(count!=8||!hex(parts[6],16,&out->nonce)||!out->nonce||!hex(parts[7],8,&seconds)||seconds>120)return -EINVAL;
   out->seconds=(uint32_t)seconds;return 0;
  }
  if(out->op==R2_SHADOW||out->op==R2_WITNESS||out->op==R2_RADIO_AUDIT){uint64_t value;if(count!=7||!hex(parts[6],8,&value)||value>UINT32_MAX||(out->op==R2_WITNESS&&value>1))return -EINVAL;out->nonce=value;return 0;}
  if (out->op == R2_DIAG) {uint64_t chunk=0;if(count!=7||!hex(parts[6],4,&chunk)||chunk>=264)return -EINVAL;out->trace_page=(unsigned)chunk;return 0;}
  if (out->op == R2_BLE) return count==7 && hex(parts[6],8,&out->nonce) && out->nonce<=120 ? 0 : -EINVAL;
  if (out->op == R2_UI && count==6) {out->trace_page=R2_UI_PAGES;return 0;}
  if (out->op == R2_TRACE || out->op == R2_UI) {
    uint64_t page=0;
    if(count==7 && (!hex(parts[6],2,&page) || page>=(out->op==R2_UI?R2_UI_PAGES:4))) return -EINVAL;
    out->trace_page=(unsigned)page;
    return count==6 || count==7 ? 0 : -EINVAL;
  }
  return count == 7 && hex(parts[6],16,&out->nonce) && out->nonce ? 0 : -EINVAL;
}
bool r2_bound(const struct r2_request *r, const char *silicon, const char *source, uint64_t boot)
{
  return !strcmp(r->silicon,silicon) && !strcmp(r->source,source) &&
    ((boot && r->boot == boot) || (r->boot == 0 && r->op == R2_STATUS));
}
void r2_expire(struct r2_maint *m, uint64_t now)
{
  if ((m->phase == R2_CLOSING || m->phase == R2_READY) && now >= m->deadline) {
    m->phase=R2_FAULT; m->error=-ETIMEDOUT;
  }
}
int r2_prepare(struct r2_maint *m, uint64_t nonce, uint64_t arrival, uint64_t now, bool safe)
{
  if (m->phase != R2_IDLE || !safe || !nonce) return -EACCES;
  if (arrival > now || now-arrival >= R2_MAINTENANCE_MS) return -ETIMEDOUT;
  m->phase=R2_CLOSING; m->nonce=nonce; m->deadline=arrival+R2_MAINTENANCE_MS;
  return 0;
}
int r2_closed(struct r2_maint *m, uint64_t now, int result)
{
  r2_expire(m,now);
  if (m->phase != R2_CLOSING) return -EACCES;
  if (result) { m->phase=R2_FAULT; m->error=result; return result; }
  m->phase=R2_READY; m->deadline=now+R2_MAINTENANCE_MS;
  return 0;
}
int r2_commit(struct r2_maint *m, uint64_t nonce, uint64_t arrival, uint64_t now)
{
  r2_expire(m,now);
  if (m->phase != R2_READY || nonce != m->nonce) return -EACCES;
  if (arrival > now || now-arrival >= R2_MAINTENANCE_MS) return -ETIMEDOUT;
  m->phase=R2_RESET;
  return 0;
}
uint32_t r2_crc32(const void *data, size_t n)
{
  const uint8_t *p=data; uint32_t c=UINT32_MAX;
  while(n--) { c ^= *p++; for(unsigned b=0;b<8;b++) c=(c>>1)^((c&1)?UINT32_C(0xedb88320):0); }
  return ~c;
}
int r2_frame(char *out, size_t capacity, uint32_t request, uint32_t sequence, const char *json)
{
  size_t size=strlen(json);
  char head[48],tail[32];
  if(size>UINT32_MAX)return -ENOSPC;
  int h=snprintf(head,sizeof head,"R2 %08x %08x %u %08x\n",
    (unsigned)request,(unsigned)sequence,(unsigned)size,(unsigned)r2_crc32(json,size));
  int t=snprintf(tail,sizeof tail,"\nR2 END %08x %08x\n",(unsigned)request,(unsigned)sequence);
  if(h<0||t<0||(size_t)h>=sizeof head||(size_t)t>=sizeof tail||
     capacity<=(size_t)h+(size_t)t||size>=capacity-(size_t)h-(size_t)t)return -ENOSPC;
  /* CRC and framing are complete before moving possibly overlapping JSON.
   * This also retains the ordinary disjoint-buffer API and exact wire bytes. */
  memmove(out+h,json,size);memcpy(out,head,(size_t)h);
  memcpy(out+h+size,tail,(size_t)t+1);
  return h+(int)size+t;
}

bool r2_factory_parse(const char *text,uint64_t *factory){
 if(!factory)return false;
 *factory=0;
 return text&&hex(text,16,factory)&&*factory;
}
