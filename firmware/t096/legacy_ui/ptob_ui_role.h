#pragma once
#ifndef PTOB_UI_ROLE_V1_INCLUDED
#define PTOB_UI_ROLE_V1_INCLUDED
#include <stdbool.h>
#include <stdint.h>
/* Production names, serving-aware role and intermediary-depth convention.
 * Candidate/elected status alone cannot produce a Grandmaster header. */
typedef struct {char header[24];uint8_t marker_depth;bool marker_solid,held,grandmaster;} ptob_ui_role_t;
static inline const char *ptob_ui_name(uint32_t id){return id==0xf6791470u?"TC-01":id==0xad2ead51u?"TC-02":id==0xa4ec4990u?"TC-03":id==0x76ad0668u?"C-1":id==0x76ae1778u?"C-2":"NONE";}
static inline ptob_ui_role_t ptob_ui_role(uint32_t local,uint32_t elected,uint32_t provider,bool authority_serving,bool relay_serving,bool route_available,bool route_fresh,uint8_t incoming_depth){
 ptob_ui_role_t r={{0},0,false,false,false};r.grandmaster=local && elected==local && authority_serving;
 r.held=route_available && !route_fresh;
 const char *name=r.grandmaster?"Grandmaster":ptob_ui_name(local);unsigned n=0;
 for(;*name && n<23;++name)r.header[n++]=*name;
 if(!r.grandmaster && route_available && provider && provider!=local){r.header[n++]='>';name=ptob_ui_name(provider);for(;*name && n<23;++name)r.header[n++]=*name;}r.header[n]=0;
 if(!r.grandmaster && route_fresh && route_available && provider && incoming_depth<=2){
  if(relay_serving && incoming_depth<2){r.marker_depth=(uint8_t)(incoming_depth+1);r.marker_solid=true;}
  else if(incoming_depth)r.marker_depth=incoming_depth;
 }
 return r;
}

#endif
