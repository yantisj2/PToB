#pragma once
#include <cstddef>
#include <cstdint>

// Read-only diagnostic association, never an RF timestamp/channel provider.
// Every collect-path helper is inlined into the caller's IRAM function. Access
// implementations and expected bytes must also be available without flash.
namespace v12::selected_tx {
#define V12_SELECTED_INLINE inline __attribute__((always_inline))
// ROM arrays contain ten pointers already. Only the existing6-activity and
// separately reviewed10-activity controller configurations are admitted.
constexpr unsigned DataBytes = 190, WireBytes = 222, MaxActivities = 10;
constexpr uintptr_t RamLow = 0x3fc88000, RamHigh = 0x3fd00000;
enum class Reason : uint8_t {
  None, Rom, Expected, Scope, Environment, ControlRange, DescriptorRange,
  Cookie, State, Format, FirstPointer, Descriptor, Length, DataPointer,
  Page, DataAddress, Event, MetadataChanged, DataMismatch, DataChanged,
  Epoch, Duplicate, CounterOrder, Count
};
constexpr unsigned ReasonCount = unsigned(Reason::Count);
struct Selection {
  uint32_t context_cookie = 0;
  uint16_t event = 0, cs_event = 0, data_offset = 0, data_length = 0;
  uint16_t cs_first = 0, cs_format = 0, descriptor[7]{};
  uint8_t flags = 0, reason = 0, data[DataBytes]{};
};
static_assert(offsetof(Selection, data) == 32 && sizeof(Selection) <= 224);
struct Scope {
  uint32_t cookie = 0, epoch = 0;
  uint8_t activity = 255, status = 255, max_activities = 0;
  bool rom_verified = false;
  bool serial_event_wrap = false;
};
struct Expected {
  const uint8_t* bytes = nullptr;
  uint16_t length = 0;
  bool validated = false;
};
// No implicit zero initialization: the ISR fills every field explicitly from
// DRAM banks, avoiding an otherwise redundant compiler-generated memset.
struct KnownExpected { const uint8_t* bytes; uint16_t length; bool validated; };
struct EventFence {
  uint32_t epoch = 0;
  uint16_t event = 0;
  bool seen = false;
};
// Optional radio inputs copied with the same metadata snapshot. These describe
// the programmed PA train, not a measured RF channel or edge.
struct Radio { uint32_t access_address; uint16_t map[3]; };

// Worker-only validation of the parent's diagnostic_pack output. This signature
// and deterministic filler identify F0 diagnostics, not RC7 authentication.
inline bool validate_expected(const uint8_t* p, unsigned n) {
  if (!p || n != DataBytes || p[0] != 189 || p[1] != 255 ||
      p[2] != 255 || p[3] != 255 || p[4] != 12 || p[5] != 0xf0 ||
      p[6] != 'V' || p[7] != '1' || p[8] != '2' || p[9] != 'D' ||
      p[10] != 'I' || p[11] != 'A' || p[12] != 'G' ||
      (p[13] != 2 && p[13] != 5) || p[84] != 190 || p[85] != 0)
    return false;
  unsigned boot = 0, run = 0, factory = 0;
  for (unsigned i = 14; i < 20; ++i) factory |= p[i];
  for (unsigned i = 20; i < 28; ++i) boot |= p[i];
  for (unsigned i = 80; i < 84; ++i) run |= p[i];
  if (!factory || !boot || !run) return false;
  for (unsigned i = 86; i < DataBytes; ++i)
    if (p[i] != uint8_t(i ^ 0x5a)) return false;
  return true;
}

namespace detail {
V12_SELECTED_INLINE bool range(uintptr_t address, unsigned bytes) {
  return bytes <= RamHigh - RamLow && address >= RamLow &&
         address <= RamHigh - bytes;
}
V12_SELECTED_INLINE void clear(Selection& s) {
  // Volatile byte stores prevent an out-of-line memset in the IRAM path.
  auto* p = reinterpret_cast<volatile uint8_t*>(&s);
  for (unsigned i = 0; i < sizeof(s); ++i) p[i] = 0;
}
V12_SELECTED_INLINE bool refuse(Selection& s, Reason reason) {
  s.flags = 0;
  s.reason = uint8_t(reason);
  auto* p = static_cast<volatile uint8_t*>(s.data);
  for (unsigned i = 0; i < DataBytes; ++i) p[i] = 0;
  return false;
}
struct Metadata {
  uintptr_t environment, control, descriptor_address;
  uint32_t cookie;
  uint16_t event, offset, length, first, format;
  uint16_t cs_event, descriptor[7];
  uint8_t activity, state;
  Radio radio;
};
// Access supplies environment_address(activity,out), em_address(offset,out),
// read8(address), read16(address), read32(address). Address getters return bool.
// The environment getter additionally guards the actual environment array.
template<class Access>
V12_SELECTED_INLINE Reason metadata(Access& a, uint8_t activity, Metadata& m) {
  uintptr_t environment = 0, control = 0, descriptor = 0;
  if (!a.environment_address(activity, environment) ||
      (environment & 3) || !range(environment, 84)) return Reason::Environment;
  m.environment = environment;
  // ROM event-start copies the full scheduling cookie at +4 into the frame
  // record. +0 is the scheduler-link pointer, not the callback cookie.
  m.cookie = a.read32(environment + 4);
  m.event = a.read16(environment + 76);
  m.offset = a.read16(environment + 78);
  m.length = a.read16(environment + 80);
  m.activity = a.read8(environment + 82);
  m.state = a.read8(environment + 83);
  if (!a.em_address(0x400, control) || (control & 1) ||
      !range(control, unsigned(activity + 1) * 90)) return Reason::ControlRange;
  m.control = control + unsigned(activity) * 90;
  m.format = a.read16(m.control);
  m.first = a.read16(m.control + 52);
  m.cs_event = a.read16(m.control + 80);
  m.radio.access_address = uint32_t(a.read16(m.control + 12)) |
                           (uint32_t(a.read16(m.control + 14)) << 16);
  auto* map = static_cast<volatile uint16_t*>(m.radio.map);
  for (unsigned i=0;i<3;++i) map[i]=a.read16(m.control+34+2*i);
  if (!a.em_address(0x1400, descriptor) || (descriptor & 1) ||
      !range(descriptor, unsigned(activity) * 126 + 14))
    return Reason::DescriptorRange;
  m.descriptor_address = descriptor + unsigned(activity) * 126;
  auto* words = static_cast<volatile uint16_t*>(m.descriptor);
  for (unsigned i = 0; i < 7; ++i)
    words[i] = a.read16(m.descriptor_address + 2 * i);
  return Reason::None;
}
V12_SELECTED_INLINE bool same(const Metadata& a, const Metadata& b) {
  bool equal = a.environment == b.environment && a.control == b.control &&
    a.descriptor_address == b.descriptor_address && a.cookie == b.cookie &&
    a.event == b.event && a.offset == b.offset && a.length == b.length &&
    a.activity == b.activity && a.state == b.state && a.first == b.first &&
    a.format == b.format && a.cs_event == b.cs_event &&
    a.radio.access_address == b.radio.access_address;
  for (unsigned i=0;i<3;++i) equal=equal && a.radio.map[i]==b.radio.map[i];
  for (unsigned i = 0; i < 7; ++i)
    equal = equal && a.descriptor[i] == b.descriptor[i];
  return equal;
}
V12_SELECTED_INLINE void retain(Selection& s, const Metadata& m) {
  s.context_cookie = m.cookie;
  s.event = m.event; s.cs_event = m.cs_event;
  s.data_offset = m.offset; s.data_length = m.length;
  s.cs_first = m.first; s.cs_format = m.format;
  auto* descriptor = static_cast<volatile uint16_t*>(s.descriptor);
  for (unsigned i = 0; i < 7; ++i) descriptor[i] = m.descriptor[i];
}
V12_SELECTED_INLINE Reason candidate(const Metadata& m, const Scope& scope) {
  if (m.cookie != scope.cookie) return Reason::Cookie;
  if (m.activity != scope.activity || m.state != 1) return Reason::State;
  if ((m.format & 31) != 6) return Reason::Format;
  if (m.first != 0x1400 + 126 * unsigned(scope.activity))
    return Reason::FirstPointer;
  if (m.descriptor[0] != 0x8000) return Reason::Descriptor;
  const bool plain=m.descriptor[1]==0xbf27&&m.descriptor[3]==0;
  const bool chm=scope.serial_event_wrap&&m.descriptor[1]==0xc927&&
    m.descriptor[3]==0x000a&&m.descriptor[6]==0x2000+75*unsigned(scope.activity);
  if(!plain&&!chm)return Reason::Descriptor;
  if (m.length != DataBytes) return Reason::Length;
  if (m.descriptor[2] != m.offset || m.offset < 0x1400 || m.offset >= 0xcc00)
    return Reason::DataPointer;
  if ((m.offset & 1023) + DataBytes > 1024) return Reason::Page;
  if (m.event != m.cs_event) return Reason::Event;
  return Reason::None;
}
// ROM CHM announcement: flags are zero in descriptor+6 high byte;
// descriptor+12 points to9 ACAD bytes (08,28,map5,instant2). Hardware emits
// the zero flags byte separately; ExtHdrLen10 gives PDUlen190+1+10=201.
V12_SELECTED_INLINE bool chm_header(const Metadata& m){
  return m.descriptor[1]==0xc927&&m.descriptor[3]==0x000a;
}
V12_SELECTED_INLINE bool valid_chm(const uint8_t* h,uint16_t event){
  if(h[0]!=8||h[1]!=0x28||(h[6]&0xe0))return false;
  unsigned used=0;for(unsigned i=2;i<7;++i)for(unsigned bit=0;bit<8;++bit)used+=(h[i]>>bit)&1;
  const uint16_t instant=uint16_t(h[7])|(uint16_t(h[8])<<8);
  const uint16_t future=uint16_t(instant-event);
  return used>=2&&future>=1&&future<=10;
}
V12_SELECTED_INLINE Reason event_reason(const EventFence& f, const Scope& s,
                                       uint16_t event) {
  if (!s.epoch || (f.seen && s.epoch < f.epoch)) return Reason::Epoch;
  if (f.seen && s.epoch == f.epoch) {
    if (event == f.event) return Reason::Duplicate;
    if (s.serial_event_wrap ? uint16_t(event-f.event)>=0x8000 : event<f.event)
      return Reason::CounterOrder;
  }
  return Reason::None;
}
}

template<class Access,class Known>
V12_SELECTED_INLINE bool collect_known(Selection& out, const Scope& scope,
                                const Known* expected, unsigned count,
                                uint8_t& matched, Radio& radio, Access& access,
                                EventFence& fence) {
  detail::clear(out);
  matched=255;
  if (!scope.rom_verified) return detail::refuse(out, Reason::Rom);
  if (!expected || !count || count>4)
    return detail::refuse(out, Reason::Expected);
  unsigned possible=0;
  for(unsigned j=0;j<count;++j)
    if(expected[j].validated && expected[j].bytes && expected[j].length==DataBytes)
      possible|=1u<<j;
  if(!possible)return detail::refuse(out,Reason::Expected);
  if ((scope.max_activities != 6 && scope.max_activities != MaxActivities) ||
      scope.activity >= scope.max_activities || scope.status != 0)
    return detail::refuse(out, Reason::Scope);
  if (!scope.epoch) return detail::refuse(out, Reason::Epoch);
  detail::Metadata before, after;
  Reason reason = detail::metadata(access, scope.activity, before);
  if (reason != Reason::None) return detail::refuse(out, reason);
  detail::retain(out, before);
  reason = detail::candidate(before, scope);
  if (reason != Reason::None) return detail::refuse(out, reason);
  reason = detail::event_reason(fence, scope, before.event);
  if (reason != Reason::None) return detail::refuse(out, reason);
  // Reuse the second metadata snapshot storage until its final read. The
  // initial header address and9 byte ACAD snapshot must remain stable across
  // both full190 byte payload reads. No extra ISR scratch buffer is needed.
  const bool chm=detail::chm_header(before);
  static_assert(sizeof(after)-offsetof(detail::Metadata,cookie)>=9);
  auto* header=reinterpret_cast<uint8_t*>(&after)+offsetof(detail::Metadata,cookie);
  if(chm){
    after.environment=0;
    if(!access.em_address(before.descriptor[6],after.environment)||!detail::range(after.environment,9))
      return detail::refuse(out,Reason::DataAddress);
    for(unsigned i=0;i<9;++i)static_cast<volatile uint8_t*>(header)[i]=access.read8(after.environment+i);
    if(!detail::valid_chm(header,before.event))return detail::refuse(out,Reason::Descriptor);
  }
  uintptr_t data = 0, rechecked_data = 0;
  if (!access.em_address(before.offset, data) || !detail::range(data, DataBytes))
    return detail::refuse(out, Reason::DataAddress);
  for (unsigned i = 0; i < DataBytes; ++i) {
    const uint8_t byte = access.read8(data + i);
    static_cast<volatile uint8_t*>(out.data)[i] = byte;
    for(unsigned j=0;j<count;++j)
      if((possible&(1u<<j)) && byte!=expected[j].bytes[i])possible&=~(1u<<j);
    if(!possible)return detail::refuse(out,Reason::DataMismatch);
  }
  for(unsigned j=0;j<count;++j)if(possible&(1u<<j)){matched=uint8_t(j);break;}
  if (!access.em_address(before.offset, rechecked_data) || data != rechecked_data ||
      !detail::range(rechecked_data, DataBytes))
    return detail::refuse(out, Reason::MetadataChanged);
  for (unsigned i = 0; i < DataBytes; ++i)
    if (access.read8(rechecked_data + i) != out.data[i] ||
        out.data[i] != expected[matched].bytes[i])
      return detail::refuse(out, Reason::DataChanged);
  if(chm){
    rechecked_data=0;
    if(!access.em_address(before.descriptor[6],rechecked_data)||rechecked_data!=after.environment||!detail::range(rechecked_data,9))
      return detail::refuse(out,Reason::MetadataChanged);
    for(unsigned i=0;i<9;++i)if(access.read8(rechecked_data+i)!=header[i])
      return detail::refuse(out,Reason::DataChanged);
  }
  // This final metadata pass encloses both complete payload/header reads.
  reason = detail::metadata(access, scope.activity, after);
  if (reason != Reason::None || !detail::same(before, after))
    return detail::refuse(out, Reason::MetadataChanged);
  radio.access_address=before.radio.access_address;
  auto* map=static_cast<volatile uint16_t*>(radio.map);
  for(unsigned i=0;i<3;++i)map[i]=before.radio.map[i];
  // Both metadata passes compare the whole packed CS+38 word. Only its low5
  // bits are channel-map bits32..36; upper controller fields stay out of CSA2.
  map[2]=uint16_t(before.radio.map[2]&31);
  fence.epoch = scope.epoch; fence.event = before.event; fence.seen = true;
  out.reason = uint8_t(Reason::None); out.flags = 1;
  return true;
}

template<class Access>
V12_SELECTED_INLINE bool collect(Selection& out,const Scope& scope,
                                const Expected& expected,Access& access,
                                EventFence& fence){
  uint8_t matched;Radio radio;
  return collect_known(out,scope,&expected,1,matched,radio,access,fence);
}

inline bool payload_shape(const Selection& s) {
  if (s.flags != 1 || s.reason != uint8_t(Reason::None) ||
      s.data_length != DataBytes ||
      s.event != s.cs_event || (s.cs_format & 31) != 6 ||
      s.cs_first < 0x1400 || (s.cs_first - 0x1400) % 126 ||
      (s.cs_first - 0x1400) / 126 >= MaxActivities ||
      s.descriptor[0] != 0x8000 ||
      !((s.descriptor[1]==0xbf27&&s.descriptor[3]==0)||
        (s.descriptor[1]==0xc927&&s.descriptor[3]==0x000a&&
         s.descriptor[6]==0x2000+75*((s.cs_first-0x1400)/126))) ||
      s.descriptor[2] != s.data_offset ||
      s.data_offset < 0x1400 || s.data_offset >= 0xcc00 ||
      (s.data_offset & 1023) + DataBytes > 1024) return false;
  return true;
}
inline bool payload_verified(const Selection& s,const uint8_t* expected,unsigned length){
  if(s.descriptor[1]!=0xbf27||s.descriptor[3]||
     !payload_shape(s)||!validate_expected(expected,length))return false;
  for (unsigned i = 0; i < DataBytes; ++i)
    if (s.data[i] != expected[i]) return false;
  return true;
}
inline uint8_t wire_byte(const Selection& s, unsigned index, bool verified) {
  if (index >= WireBytes) return 0;
  if (index >= 32) return verified && s.flags == 1 &&
    s.reason == uint8_t(Reason::None) ? s.data[index - 32] : 0;
  if (index < 4) return uint8_t(s.context_cookie >> (8 * index));
  if (index < 6) return uint8_t(s.event >> (8 * (index - 4)));
  if (index < 8) return uint8_t(s.cs_event >> (8 * (index - 6)));
  if (index < 10) return uint8_t(s.data_offset >> (8 * (index - 8)));
  if (index < 12) return uint8_t(s.data_length >> (8 * (index - 10)));
  if (index < 14) return uint8_t(s.cs_first >> (8 * (index - 12)));
  if (index < 16) return uint8_t(s.cs_format >> (8 * (index - 14)));
  if (index < 30) return uint8_t(s.descriptor[(index - 16) / 2] >>
                              (8 * ((index - 16) & 1)));
  if (index == 30) return verified && s.flags == 1 ? 1 : 0;
  return s.reason;
}
#undef V12_SELECTED_INLINE
}
