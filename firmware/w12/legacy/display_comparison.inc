// Included beside the OLED primitives; preview builds use this exact renderer.
void invert_box(int x,int y,int w,int h){
 for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)
  if(xx>=0 && xx<128 && yy>=0 && yy<64)fb[(yy/8)*128+xx]^=uint8_t(1u<<(yy&7));
}
void panel_value(uint32_t ns,bool wide,char* value,size_t size,const char*& unit){
 if(ns==UINT32_MAX){std::snprintf(value,size,"--");unit="ns";return;}
 uint32_t divisor=1;unit="ns";
 if(ns>=1000000000){divisor=1000000000;unit="s";}
 else if(ns>=1000000){divisor=1000000;unit="ms";}
 else if(ns>=1000){divisor=1000;unit="us";}
 // Round upward: the compact label must not understate the precision estimate.
 const uint64_t tenths=(uint64_t(ns)*10+divisor-1)/divisor;
 if(wide && divisor>1 && tenths<1000)
  std::snprintf(value,size,"%lu.%lu",(unsigned long)(tenths/10),(unsigned long)(tenths%10));
 else {
  uint32_t rounded=uint32_t((uint64_t(ns)+divisor-1)/divisor);
  if(rounded>=1000){rounded=1;unit=divisor==1?"us":divisor==1000?"ms":"s";}
  std::snprintf(value,size,"%lu",(unsigned long)rounded);
 }
}
void panel_offset(char* out,size_t size,const DisplayState::SourcePanel& panel,unsigned id){
 if(!panel.offset_valid){std::snprintf(out,size,"D --");return;}
 if(id==0){std::snprintf(out,size,"D REF");return;}
 const int64_t ns=panel.offset_ns;
 const uint64_t magnitude=ns<0?uint64_t(-(ns+1))+1:uint64_t(ns);
 const char* unit="ns";uint64_t divisor=1;
 if(magnitude>=1000000000ULL){divisor=1000000000;unit="s";}
 else if(magnitude>=1000000){divisor=1000000;unit="ms";}
 else if(magnitude>=1000){divisor=1000;unit="us";}
 const uint64_t amount=(magnitude+divisor/2)/divisor;
 if(amount>999){std::snprintf(out,size,"D LARGE");return;}
 std::snprintf(out,size,"D%c%lu%s",ns<0?'-':'+',(unsigned long)amount,unit);
}
[[maybe_unused]] void page_comparison(const DisplayState& s){
 const char* names[3]={"BLE","PTS","LORA"};
 const DisplayState::TimingSource sources[3]={DisplayState::Ble,DisplayState::Pts,DisplayState::Lora};
 unsigned ids[3]{},count=0;
 for(unsigned i=0;i<3;++i)if(s.comparison[i].present)ids[count++]=i;
 if(!count){count=3;for(unsigned i=0;i<3;++i)ids[i]=i;}
 for(unsigned slot=0;slot<count;++slot){
  const unsigned id=ids[slot];const auto& p=s.comparison[id];
  const int x=int(slot)*128/int(count),end=int(slot+1)*128/int(count)-2,w=end-x+1;
  line(x+2,17,end-2,17);line(x+2,62,end-2,62);
  line(x,19,x,60);line(end,19,end,60);
  pixel(x+1,18);pixel(end-1,18);pixel(x+1,61);pixel(end-1,61);
  text(x+4,19,names[id]);
  const char* state=!p.enabled?"OFF":!p.present?"WAIT":p.hold?"HOLD":p.fit?"FIT":p.fresh?"LIVE":"ACQ";
  if(count<3){
   text(x+w-4-int(std::strlen(state))*6,19,state);
  }
  if(s.timing_source==sources[id])invert_box(x+2,18,w-4,9);
  char number[16],detail[24],age[10];const char* unit=nullptr;
  panel_value(p.present?p.precision_ns:UINT32_MAX,count<3,number,sizeof(number),unit);
  const int metric_width=int(std::strlen(number))*12-2;
  text(count==1?x+7:x+(w-metric_width)/2,29,number,2);
  if(p.age_s==UINT32_MAX)std::snprintf(age,sizeof(age),"A--");
  else if(p.age_s>999)std::snprintf(age,sizeof(age),"A999+");
  else std::snprintf(age,sizeof(age),"A%lus",(unsigned long)p.age_s);
  if(count==3){
   if(p.age_s==UINT32_MAX)std::snprintf(detail,sizeof(detail),"%s A--",unit);
   else std::snprintf(detail,sizeof(detail),"%s A%lu",unit,(unsigned long)std::min<uint32_t>(99,p.age_s));
   text(x+3,45,detail);text(x+4,54,state);
  }else if(count==2){
   std::snprintf(detail,sizeof(detail),"%s %s",unit,age);text(x+4,45,detail);
   panel_offset(detail,sizeof(detail),p,id);text(x+4,54,detail);
  }else{
   text(x+7,45,unit);text(x+70,30,age);
   panel_offset(detail,sizeof(detail),p,id);text(x+70,41,detail);
   text(x+7,54,"REL / UTC UNCAL");
  }
 }
}
