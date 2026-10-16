#pragma once
#include "board.hpp"
#include <cstring>
namespace sticks3 {
struct DirtyRect {unsigned x=Width,y=StripRows,width=0,height=0;};
inline DirtyRect changed_pixels(const uint8_t* current,const uint8_t* previous) {
 DirtyRect r{};unsigned last_x=0,last_y=0;
 for(unsigned y=0;y<StripRows;++y)for(unsigned x=0;x<Width;++x){
  const unsigned i=2*(y*Width+x);
  if(current[i]!=previous[i] || current[i+1]!=previous[i+1]){
   if(x<r.x)r.x=x;
   if(y<r.y)r.y=y;
   if(x>last_x)last_x=x;
   if(y>last_y)last_y=y;
   r.width=r.height=1;
  }
 }
 if(r.width){r.width=last_x-r.x+1;r.height=last_y-r.y+1;}
 return r;
}
inline unsigned pack_rectangle(uint8_t* strip,const DirtyRect& r) {
 if(!r.width || !r.height || r.x>=Width || r.width>Width-r.x ||
    r.y>=StripRows || r.height>StripRows-r.y)return 0;
 for(unsigned row=0;row<r.height;++row)
  std::memmove(strip+row*r.width*2,strip+((r.y+row)*Width+r.x)*2,r.width*2);
 return r.width*r.height*2;
}
}
