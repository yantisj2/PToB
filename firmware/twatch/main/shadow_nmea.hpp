// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "shadow_model.hpp"
#include <cstdlib>
#include <cstring>
namespace watch {
// Bounded checksum-verified NMEA receiver. No coordinates or UART bytes are
// persisted; GNSS is a witness and cannot enter source selection or training.
class ShadowNmea {
 char line_[160]{};unsigned size_=0;bool collecting_=false;
 uint64_t packet_=0,fix_at_=0,rmc_at_=0,rmc20_=0,gsa_at_=0;
 uint32_t second_=0,coherent_=0,errors_=0,bytes_=0,sentences_=0;bool rmc_valid_=false,gga_valid_=false;
 public:
 struct PpsReference {uint64_t rmc20=0;uint32_t second=0;bool ready=false;};
 private:
 PpsReference references_[3]{};unsigned reference_head_=0;
 uint16_t used_=0;uint8_t dimension_=0;
 struct Sky {Satellite sats[16]{};uint64_t at=0;uint16_t total=0;uint8_t count=0,next=0,messages=0;};
 Sky skies_[6]{},pending_[6]{};
 static unsigned number(const char* s,unsigned limit){if(!*s)return 0;unsigned n=0;for(;*s;++s){if(*s<'0'||*s>'9')return 0;n=n*10+unsigned(*s-'0');if(n>limit)return 0;}return n;}
 static bool coordinate(const char* s,unsigned limit){
  if(!*s)return false;
  unsigned n=0;for(;*s;++s){if(*s<'0'||*s>'9')return false;n=n*10+unsigned(*s-'0');if(n>limit)return false;}return true;
 }
 static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;}
 static unsigned sky(const char* s){return !std::strncmp(s,"GP",2)?1:!std::strncmp(s,"GL",2)?2:!std::strncmp(s,"GA",2)?3:(!std::strncmp(s,"GB",2)||!std::strncmp(s,"BD",2))?4:!std::strncmp(s,"GQ",2)?5:0;}
 void parse(uint64_t now,uint64_t tick20){
  char* end=std::strchr(line_,'*');if(!end||std::strlen(end)!=3){++errors_;return;}
  unsigned check=0;for(char* p=line_+1;p<end;++p)check^=uint8_t(*p);
  const int h=hex(end[1]),l=hex(end[2]);if(h<0||l<0||check!=unsigned(h*16+l)){++errors_;return;}*end=0;
  char* fields[32]{};unsigned count=1;fields[0]=line_+1;
  for(char* p=line_+1;*p;++p)if(*p==','){*p=0;if(count>=32)return;fields[count++]=p+1;}
  if(std::strlen(fields[0])!=5)return;
  const char* type=fields[0]+2;packet_=now;if(sentences_<UINT32_MAX)++sentences_;
  if(!std::strcmp(type,"GGA")&&count>=10){const auto quality=number(fields[6],8);gga_valid_=quality>0&&quality<=5;fix_at_=now;used_=uint16_t(number(fields[7],99));}
  if(!std::strcmp(type,"GSA")&&count>=3){dimension_=uint8_t(number(fields[2],3));gsa_at_=now;}
  if(!std::strcmp(type,"RMC")&&count>=10){
   const auto n=std::strlen(fields[1]);bool time=n>=6;
   for(unsigned i=0;time&&i<6;++i)time=fields[1][i]>='0'&&fields[1][i]<='9';
   unsigned hh=0,mm=0,ss=0;if(time){hh=unsigned(fields[1][0]-'0')*10+fields[1][1]-'0';mm=unsigned(fields[1][2]-'0')*10+fields[1][3]-'0';ss=unsigned(fields[1][4]-'0')*10+fields[1][5]-'0';time=hh<24&&mm<60&&ss<60;}
   // Only integer-second RMC observations advance reference coherence.
   if(time&&n>6){time=fields[1][6]=='.';for(unsigned i=7;time&&i<n;++i)time=fields[1][i]=='0';}
   const bool valid=time&&!std::strcmp(fields[2],"A");const auto second=hh*3600+mm*60+ss;
   if(valid&&rmc_valid_&&second==second_)return; // Multiple talkers, same epoch.
   const bool next=valid&&rmc_valid_&&second==(second_+1)%86400&&now>=rmc_at_&&now-rmc_at_<2000000;
   coherent_=valid?(next?std::min<uint32_t>(3,coherent_+1):1):0;rmc_valid_=valid;second_=second;rmc_at_=now;rmc20_=tick20;
   if(!next){for(auto& r:references_)r={};reference_head_=0;}
   if(valid){references_[reference_head_]={tick20,second,coherent_>=3};reference_head_=(reference_head_+1)%3;}
  }
  if(!std::strcmp(type,"GSV")&&count>=4){
   const auto slot=sky(fields[0]),messages=number(fields[1],9),part=number(fields[2],9);
   if(!messages||!part||part>messages)return;
   auto& p=pending_[slot];
   if(part==1){p={};p.messages=uint8_t(messages);p.next=1;p.total=uint16_t(number(fields[3],99));}
   if(p.next!=part||p.messages!=messages){p={};return;}
   for(unsigned i=4;i+3<count;i+=4){const auto id=number(fields[i],999);if(!id)continue;
    Satellite sat{uint16_t(id),uint16_t(number(fields[i+2],359)),uint8_t(number(fields[i+1],90)),uint8_t(number(fields[i+3],99)),fields[0][1],coordinate(fields[i+1],90)&&coordinate(fields[i+2],359)};
    if(p.count<16)p.sats[p.count++]=sat;else {unsigned weakest=0;for(unsigned j=1;j<16;++j)if(p.sats[j].cn0<p.sats[weakest].cn0)weakest=j;if(sat.cn0>p.sats[weakest].cn0)p.sats[weakest]=sat;}
   }
   ++p.next;if(part==messages){p.at=now;skies_[slot]=p;p={};}
  }
 }
 public:
 void feed(const uint8_t* bytes,unsigned count,uint64_t now,uint64_t tick20){
  bytes_=uint32_t(std::min<uint64_t>(UINT32_MAX,uint64_t(bytes_)+count));
  for(unsigned i=0;i<count;++i){const char c=char(bytes[i]);
   if(c=='$'){collecting_=true;size_=0;line_[size_++]=c;}
   else if(collecting_&&(c=='\r'||c=='\n')){line_[size_]=0;parse(now,tick20);collecting_=false;size_=0;}
   else if(collecting_){if(size_+1>=sizeof line_){collecting_=false;size_=0;++errors_;}else if(c>=32&&c<=126)line_[size_++]=c;else{collecting_=false;size_=0;++errors_;}}
  }
 }
 bool packets(uint64_t now)const{return packet_&&now>=packet_&&now-packet_<3000000;}
 bool fix(uint64_t now)const{return gga_valid_&&fix_at_&&now>=fix_at_&&now-fix_at_<2500000&&rmc_valid_&&rmc_at_&&now>=rmc_at_&&now-rmc_at_<1500000;}
 bool reference(uint64_t now)const{return fix(now)&&coherent_>=3;}
 PpsReference reference_at(uint64_t edge20,uint64_t now)const{
  if(!reference(now))return {};
  // UART may deliver this second's RMC after PPS but before the main owner
  // consumes its queue. Use the coherent RMC actually available before PPS.
  for(unsigned i=0;i<3;++i){const auto& r=references_[(reference_head_+2-i)%3];
   if(r.ready&&r.rmc20&&r.rmc20<edge20&&edge20-r.rmc20<30000000)return r;
  }
  return {};
 }
 uint64_t rmc20()const{return rmc20_;}uint32_t second()const{return second_;}uint32_t errors()const{return errors_;}uint32_t bytes()const{return bytes_;}uint32_t sentences()const{return sentences_;}
 void apply(ShadowView& v,uint64_t now)const{
  v.fix=fix(now);v.used=fix_at_&&now>=fix_at_&&now-fix_at_<2500000?used_:0;
  v.fix_dimension=gsa_at_&&now>=gsa_at_&&now-gsa_at_<3000000?dimension_:0;
  v.satellite_count=0;v.in_view=0;
  const bool combined=skies_[0].at&&now>=skies_[0].at&&now-skies_[0].at<5000000;
  for(unsigned i=combined?0:1;i<(combined?1:6);++i){const auto& sky=skies_[i];if(!sky.at||now<sky.at||now-sky.at>=5000000)continue;v.in_view+=sky.total;
   for(unsigned j=0;j<sky.count;++j){if(v.satellite_count<16)v.satellites[v.satellite_count++]=sky.sats[j];
    else {unsigned weakest=0;for(unsigned k=1;k<16;++k)if(v.satellites[k].cn0<v.satellites[weakest].cn0)weakest=k;if(sky.sats[j].cn0>v.satellites[weakest].cn0)v.satellites[weakest]=sky.sats[j];}}
  }
  std::sort(v.satellites,v.satellites+v.satellite_count,[](const Satellite& a,const Satellite& b){return a.cn0>b.cn0;});
 }
};
}
