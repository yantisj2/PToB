/* SPDX-License-Identifier: Apache-2.0 */
#include "clock.h"
#include <string.h>
static int hex(char c) { if(c>='0'&&c<='9')return c-'0'; if(c>='A'&&c<='F')return c-'A'+10; if(c>='a'&&c<='f')return c-'a'+10; return -1; }
static int number(const char *s,size_t n) { int v=0; if(!n||n>6)return -1; for(size_t i=0;i<n;i++){if(s[i]<'0'||s[i]>'9')return -1;v=v*10+s[i]-'0';}return v; }
int r2_nmea(const char *line,int64_t *utc_us,unsigned *satellites)
{
  size_t n=strlen(line); if(n<6||n>=128||line[0]!='$')return -R2_INPUT_SYNTAX;
  const char *star=strchr(line,'*'); if(!star||star!=line+n-3)return -R2_INPUT_SYNTAX;
  unsigned sum=0; for(const char *p=line+1;p<star;p++)sum^=(uint8_t)*p;
  int a=hex(star[1]),b=hex(star[2]); if(a<0||b<0||sum!=(unsigned)(16*a+b))return -R2_INPUT_CHECKSUM;
  /* UC6580 aiding acknowledgement is transport feedback, never clock data. */
  if(star==line+3&&!memcmp(line+1,"OK",2))return 0;
  if(n<10)return -R2_INPUT_SYNTAX;
  if(line[6]!=','||!( !memcmp(line+1,"GP",2)||!memcmp(line+1,"GN",2)||!memcmp(line+1,"GA",2)||
       !memcmp(line+1,"GB",2)||!memcmp(line+1,"BD",2)||!memcmp(line+1,"GL",2)))return 0;
  const char *field[20]={0}; size_t len[20]={0}; unsigned fields=0;
  const char *start=line+7;
  for(const char *p=start;p<=star;p++)if(*p==','||p==star){
    if(fields==20)return -R2_INPUT_SYNTAX;
    field[fields]=start;len[fields++]=(size_t)(p-start);start=p+1;
  }
  if(!memcmp(line+3,"GGA",3)) {
    if(fields<7)return -R2_INPUT_SYNTAX;
    int sats=number(field[6],len[6]); if(sats<0||sats>99)return -R2_INPUT_SYNTAX;
    *satellites=(unsigned)sats;return 3;
  }
  if(memcmp(line+3,"RMC",3))return 0;
  if(fields<9||len[1]!=1)return -R2_INPUT_SYNTAX;
  if(field[1][0]=='V')return 2;
  if(field[1][0]!='A')return -R2_INPUT_SYNTAX;
  if(fields>11&&len[11] && (len[11]!=1||!strchr("ADFR",field[11][0])))return 2;
  if(len[0]<6||len[0]>13||len[8]!=6)return -R2_INPUT_SYNTAX;
  int hour=number(field[0],2),min=number(field[0]+2,2),sec=number(field[0]+4,2);
  int day=number(field[8],2),month=number(field[8]+2,2),yy=number(field[8]+4,2);
  if(hour<0||hour>23||min<0||min>59||sec<0||sec>59||day<1||month<1||month>12||yy<0)return -R2_INPUT_SYNTAX;
  int fraction=0;
  if(len[0]>6){ if(field[0][6]!='.'||len[0]<8)return -R2_INPUT_SYNTAX;
    fraction=number(field[0]+7,len[0]-7); if(fraction<0)return -R2_INPUT_SYNTAX;
    for(size_t i=len[0]-7;i<6;i++)fraction*=10;
  }
  /* R2 deliberately supports 2000..2099. No rolling-century or week inference. */
  static const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
  unsigned maximum=days[month-1]+(month==2&&yy%4==0);
  if((unsigned)day>maximum)return -R2_INPUT_SYNTAX;
  int64_t d=10957;for(int y=0;y<yy;y++)d+=365+(y%4==0);
  for(int m=1;m<month;m++)d+=days[m-1]+(m==2&&yy%4==0);
  *utc_us=((d+day-1)*86400+hour*3600+min*60+sec)*INT64_C(1000000)+fraction;
  return 1;
}
uint16_t r2_nmea_gga_hdop(const char *line)
{
  if(!line)return R2_GGA_HDOP_UNKNOWN;
  size_t n=0;while(n<128&&line[n])++n;
  if(n==128)return R2_GGA_HDOP_UNKNOWN;
  int64_t utc=0;unsigned satellites=0;
  if(r2_nmea(line,&utc,&satellites)!=3)return R2_GGA_HDOP_UNKNOWN;
  const char *start=line+7,*end=NULL;unsigned field=0;
  for(const char *p=start;*p;p++)if(*p==','||*p=='*'){
    if(field==7){end=p;break;}
    if(*p=='*')break;
    ++field;start=p+1;
  }
  if(!end||start==end)return R2_GGA_HDOP_UNKNOWN;
  uint32_t whole=0,fraction=0;unsigned decimals=0;bool point=false,digits=false;
  for(const char *p=start;p<end;p++){
    if(*p=='.'){
      if(point||!digits)return R2_GGA_HDOP_UNKNOWN;
      point=true;continue;
    }
    if(*p<'0'||*p>'9')return R2_GGA_HDOP_UNKNOWN;
    digits=true;
    if(point){if(++decimals>2)return R2_GGA_HDOP_UNKNOWN;fraction=fraction*10+(unsigned)(*p-'0');}
    else {whole=whole*10+(unsigned)(*p-'0');if(whole>655)return R2_GGA_HDOP_UNKNOWN;}
  }
  if(point&&!decimals)return R2_GGA_HDOP_UNKNOWN;
  if(decimals==1)fraction*=10;
  uint32_t value=whole*100+fraction;
  return value>=R2_GGA_HDOP_UNKNOWN?R2_GGA_HDOP_UNKNOWN:(uint16_t)value;
}

/* Position is independent of RMC time acceptance. No floating point, implied
 * hemispheres, minutes>=60 or out-of-range endpoint coordinates. */
static bool coordinate(const char *p,size_t n,unsigned degrees,unsigned limit,int32_t *out){
 if(n<degrees+2 || n>degrees+10)return false;
 unsigned whole=0,minutes=0;
 for(unsigned i=0;i<degrees+2;i++){
  if(p[i]<'0'||p[i]>'9')return false;
  if(i<degrees)whole=whole*10+(unsigned)(p[i]-'0');
  else minutes=minutes*10+(unsigned)(p[i]-'0');
 }
 uint32_t fraction=0,scale=1;
 if(n>degrees+2){
  if(p[degrees+2]!='.'||n==degrees+3)return false;
  for(size_t i=degrees+3;i<n;i++){
   if(p[i]<'0'||p[i]>'9'||scale==10000000)return false;
   fraction=fraction*10+(unsigned)(p[i]-'0');scale*=10;
  }
 }
 if(whole>limit||minutes>=60||(whole==limit&&(minutes||fraction)))return false;
 *out=(int32_t)(whole*10000000+((uint64_t)minutes*scale+fraction)*10000000/(60*scale));
 return true;
}
bool r2_nmea_position(const char *line,struct r2_position *out,int64_t received){
 if(!line||!out||received<0)return false;
 size_t n=0;while(n<128&&line[n])++n;
 if(n==128)return false;
 int64_t utc=0;unsigned satellites=0;
 if(r2_nmea(line,&utc,&satellites)!=1)return false;
 const char *fields[6]={0};size_t lengths[6]={0};unsigned field=0;
 const char *start=line+7;
 for(const char *p=start;*p&&field<6;p++)if(*p==','||*p=='*'){
  fields[field]=start;lengths[field++]=(size_t)(p-start);start=p+1;
 }
 struct r2_position position={.received_us=received,.valid=true};
 if(field<6||lengths[3]!=1||lengths[5]!=1||
    !coordinate(fields[2],lengths[2],2,90,&position.latitude_e7)||
    !coordinate(fields[4],lengths[4],3,180,&position.longitude_e7))return false;
 if(*fields[3]=='S')position.latitude_e7=-position.latitude_e7;
 else if(*fields[3]!='N')return false;
 if(*fields[5]=='W')position.longitude_e7=-position.longitude_e7;
 else if(*fields[5]!='E')return false;
 if(position.longitude_e7==1800000000)position.longitude_e7=-1800000000;
 *out=position;return true;
}
