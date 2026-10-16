#include "ptob_l76k_aid.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
int main(){
 using namespace ptob_l76k;
 TimeAid a{true,UINT64_C(1791676800000000),1000000,995000,2000000,1,2,40000,3,4};
 uint8_t f[66];uint32_t bound=0;assert(aid_frame(f,a,1010000,bound));assert(bound==140021);
 assert(f[2]==56&&f[4]==11&&f[5]==1&&f[61]==0x42&&f[60]==0);
 uint64_t bits=0;for(unsigned i=0;i<8;i++)bits|=uint64_t(f[30+i])<<(8*i);
 double tow;std::memcpy(&tow,&bits,8);
 const uint64_t gps=a.utc_us+10000-UINT64_C(315964800000000)+18000000;
 assert(unsigned(f[58])+(unsigned(f[59])<<8)==gps/UINT64_C(604800000000));
 assert(std::abs(tow-double(gps%UINT64_C(604800000000))/1000000.)<1e-8);
 uint32_t fb=get32(f+46);float variance;std::memcpy(&variance,&fb,4);
 assert(std::sqrt(double(variance))/299792458.>=double(bound)/1000000.);
 uint32_t sum=0;for(unsigned i=2;i<62;i+=4)sum+=get32(f+i);assert(sum==get32(f+62));
 for(unsigned i=6;i<30;i++)assert(f[i]==0); // no fabricated position
 auto bad=a;bad.authenticated=false;assert(!aid_frame(f,bad,1010000,bound));
 bad=a;bad.uncertainty_us=0;assert(!aid_frame(f,bad,1010000,bound));
 bad=a;bad.uncertainty_us=250000;assert(!aid_frame(f,bad,1010000,bound));
 assert(!aid_frame(f,a,999999,bound));assert(!aid_frame(f,a,1145000,bound));
 bad=a;bad.until_us=1110000;assert(!aid_frame(f,bad,1010000,bound));
 bad=a;bad.utc_us=UINT64_C(1814400000000000);assert(!aid_frame(f,bad,1010000,bound));
 bad=a;bad.utc_us=UINT64_MAX;assert(!aid_frame(f,bad,1010000,bound));
 AidAck ack;uint8_t ok[]={0xba,0xce,4,0,5,1,11,1,0,0,15,1,5,1};
 for(unsigned i=0;i<13;i++)assert(!ack.feed(ok[i]));assert(ack.feed(ok[13])==1);
 ok[5]=0;ok[13]=0;for(unsigned i=0;i<13;i++)assert(!ack.feed(ok[i]));assert(ack.feed(ok[13])==-1);
 ok[6]=12;for(auto c:ok)assert(!ack.feed(c));
}
