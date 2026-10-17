// SPDX-License-Identifier: Apache-2.0
// Diagnostic only: raw vendor IN; no UTC clock, configuration changes or seize.
#import <Foundation/Foundation.h>
#import <IOUSBHost/IOUSBHost.h>
#import <IOKit/IOKitLib.h>
#import <mach/mach_time.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>

static volatile sig_atomic_t interrupted;
static void onSignal(int sig) { (void)sig; interrupted = 1; }
static NSString *integer(__int128 value) {
    char text[48], *p = text + sizeof(text); *--p = 0;
    bool negative = value < 0;
    unsigned __int128 n = negative ? (unsigned __int128)(-value) : (unsigned __int128)value;
    do { *--p = '0' + n % 10; n /= 10; } while (n);
    if (negative) *--p = '-';
    return @(p);
}
static NSString *ns(__int128 ticks, mach_timebase_info_data_t tb) {
    return integer(ticks * tb.numer / tb.denom);
}
static uint64_t little(const uint8_t *p,unsigned count) {
    uint64_t value=0; for(unsigned i=0;i<count;i++) value|=(uint64_t)p[i]<<(8*i); return value;
}
static NSDictionary *rawSOF(NSData *data) {
    if (data.length!=80) return nil;
    const uint8_t *p=data.bytes;
    if (memcmp(p,"UFS1",4) || little(p+4,2)!=1 || little(p+6,2)!=80 || little(p+40,2)>2047 || little(p+42,2)>1) return nil;
    return @{@"version":@1, @"silicon": [NSString stringWithFormat:@"%016llx",(unsigned long long)little(p+8,8)],
        @"boot":[NSString stringWithFormat:@"%016llx",(unsigned long long)little(p+16,8)],
        @"usb_epoch":@(little(p+24,4)), @"capture_epoch":@(little(p+28,4)), @"sequence":@(little(p+32,4)),
        @"loss_total":@(little(p+36,4)), @"frame":@(little(p+40,2)), @"valid":little(p+42,2) ? @YES : @NO,
        @"tick":integer(little(p+44,8)), @"current_tick":integer(little(p+52,8)), @"races":@(little(p+60,4)),
        @"missed":@(little(p+64,4)), @"overflow":@(little(p+68,4)), @"fences":@(little(p+72,4)), @"reason":@(little(p+76,4))};
}
static NSDictionary *sourceRecord(NSData *data,unsigned kind) {
    if(data.length!=256)return nil;const uint8_t *p=data.bytes;
    if(memcmp(p,"UTS1",4)||little(p+4,2)!=1||little(p+6,2)!=256||p[72]!=kind||little(p+36,4)>1||little(p+42,2)!=1)return nil;
    uint64_t frame=little(p+40,2);if(frame>2047&&frame!=65535)return nil;
    NSMutableDictionary *d=[@{@"kind":@(kind),@"valid":@(little(p+36,4)==1),@"version":@1,
      @"silicon":[NSString stringWithFormat:@"%016llx",(unsigned long long)little(p+8,8)],
      @"boot":[NSString stringWithFormat:@"%016llx",(unsigned long long)little(p+16,8)],
      @"root":[NSString stringWithFormat:@"%016llx",(unsigned long long)little(p+80,8)],
      @"root_boot":[NSString stringWithFormat:@"%016llx",(unsigned long long)little(p+88,8)],
      @"timescale":@"POSIX_UTC_ESTIMATE",@"precision_qualified":@NO} mutableCopy];
    NSArray *names=@[@"usb_epoch",@"capture_epoch",@"sequence",@"source_epoch",@"map_generation",@"map_age_us",@"bound_ns",@"rate_bound_ppb",@"capture_bound_ns",@"history_overwrites",@"reason"];
    const unsigned offsets[]={24,28,32,96,100,168,172,176,180,216,220};
    for(unsigned i=0;i<names.count;i++)d[names[i]]=@(little(p+offsets[i],4));
    names=@[@"tick",@"current_tick",@"utc_ns",@"until_tick",@"source_expires_ns",@"anchor_tick",@"anchor_ns",@"effective_tick",@"active_lo_tick",@"active_hi_tick",@"source_issued_ns"];
    const unsigned offsets64[]={48,56,64,104,112,120,128,136,144,152,192};
    for(unsigned i=0;i<names.count;i++)d[names[i]]=integer(little(p+offsets64[i],8));
    d[@"frame"]=@(frame);d[@"map_ppb"]=@((int32_t)little(p+160,4));d[@"map_phase_ns"]=@((int32_t)little(p+164,4));
    d[@"utc_offset_ns"]=integer((int64_t)little(p+184,8));
    if(little(p+220,4)==7){
      d[@"sof_map_metadata"]=@{@"magic":[[NSString alloc] initWithBytes:p+232 length:4 encoding:NSASCIIStringEncoding]?:@"UNDECODABLE",
        @"schema":@(little(p+236,2)),@"bytes":@(little(p+238,2)),@"current_source_ns":integer(little(p+240,8)),
        @"flags":@(little(p+248,4)),@"reserved":@(little(p+252,4))};
    }
    NSMutableString *scope=[NSMutableString string];for(unsigned i=0;i<16;i++)[scope appendFormat:@"%02x",p[200+i]];d[@"path_scope"]=scope;
    return d;
}
// Explicit UTE1 request only. Never reinterpret a UTS1 valid bit or accept
// held projections as fresh observations. The returned legacy view is fenced.
static NSDictionary *estimateRecord(NSData *data, NSDictionary **observation) {
    if(data.length!=256)return nil;const uint8_t *p=data.bytes;
    uint32_t flags=(uint32_t)little(p+36,4),state=(uint32_t)little(p+248,4);
    if(memcmp(p,"UTE1",4)||little(p+4,2)!=1||little(p+6,2)!=256||
       p[72]!=1||little(p+42,2)!=1||(flags&~31u)||state>2||
       ((flags&1)&&((flags&30)!=30))||((flags&2)&&(!(flags&4)||!state))||
       (!(flags&2)&&state))return nil;
    NSMutableData *legacy=[data mutableCopy];uint8_t *q=legacy.mutableBytes;
    memcpy(q,"UTS1",4);memset(q+36,0,4);q[36]=flags&1;
    if(!(flags&1)){memset(q+64,0,8);memset(q+220,0,4);q[220]=1;}
    memset(q+232,0,24);
    NSDictionary *base=sourceRecord(legacy,1);if(!base)return nil;
    if(observation)*observation=base;
    NSMutableDictionary *d=[base mutableCopy];
    d[@"schema"]=@1;d[@"contract"]=@"UTE1";d[@"flags"]=@(flags);
    d[@"estimate_present"]=@((flags&2)!=0);d[@"counter_valid"]=@((flags&4)!=0);
    d[@"session_current"]=@((flags&8)!=0);d[@"root_envelope_current"]=@((flags&16)!=0);
    d[@"estimate_utc_ns"]=integer((int64_t)little(p+64,8));
    d[@"last_discipline_tick"]=integer(little(p+232,8));
    d[@"session_until_tick"]=integer(little(p+240,8));d[@"tracking_state"]=@(state);
    d[@"estimate_reason"]=@(little(p+252,4));d[@"measurement"]=@NO;
    d[@"uncertainty"]=@"UNKNOWN";return d;
}
// Preserve an attributable flags0 observation rejection even if its enclosing
// packet is short or malformed. A decoded dictionary is validated by the host.
static bool invalidReason7(NSData *data,NSUInteger transferred,NSDictionary *decoded) {
    if(decoded||transferred<224||data.length<224)return false;
    const uint8_t *p=data.bytes;return !memcmp(p,"UTS1",4)&&little(p+220,4)==7;
}
static NSDictionary *anchorRecord(NSData *data) {
    if(data.length!=64)return nil;const uint8_t *p=data.bytes;
    if(memcmp(p,"UFA1",4)||little(p+4,2)!=1||little(p+6,2)!=64||little(p+36,4)>1)return nil;
    uint64_t frame=little(p+40,2);if(frame>2047&&frame!=65535)return nil;
    return @{@"version":@1,@"silicon":[NSString stringWithFormat:@"%016llx",(unsigned long long)little(p+8,8)],
     @"boot":[NSString stringWithFormat:@"%016llx",(unsigned long long)little(p+16,8)],
     @"usb_epoch":@(little(p+24,4)),@"capture_epoch":@(little(p+28,4)),@"sequence":@(little(p+32,4)),
     @"valid":@(little(p+36,4)==1),@"frame":@(frame),@"token":@(little(p+42,2)),
     @"tick":integer(little(p+44,8)),@"current_tick":integer(little(p+52,8)),@"reason":@(little(p+60,4)),@"authority":@"NONE"};
}
static bool emit(NSDictionary *record) {
    NSError *error = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:record options:NSJSONWritingSortedKeys error:&error];
    if (!data) { fprintf(stderr, "JSON serialization: %s\n", error.description.UTF8String); return false; }
    return fwrite(data.bytes, 1, data.length, stdout) == data.length && fputc('\n', stdout) != EOF && fflush(stdout) == 0;
}
static bool number(const char *s, uint64_t *value) {
    if (!s[0] || s[0] == '-' || s[0] == '+' || isspace((unsigned char)s[0])) return false;
    char *end = NULL; errno = 0;
    *value = strtoull(s, &end, 0);
    return !errno && end != s && !*end;
}
static void usage(void) {
    fprintf(stderr, "usage: reference-probe --clock-only [--samples N --interval-us N --load-threads N]\n"
      "       reference-probe --interface-registry-id ID --vid ID --pid ID --serial TEXT --interface-number N\n"
      "                       [--samples N --interval-us N --load-threads N] [--device-sof]\n"
      "       reference-probe --discover --vid ID --pid ID [--serial TEXT | --after-serial TEXT] [--device-sof]\n"
      "                       [--samples N --interval-us N --duration-seconds N]\n"
      "       reference-probe --self-test | --help\n"
      "Explicit vendor interface only (class FF/subclass 54/protocol 01); never opens a CDC or device object.\n"
      "Samples=1..460800, interval=125..1000000us, duration=1..7200s (default120); load=0..8 only up to120s.\n");
}
static id property(io_registry_entry_t entry, CFStringRef key) {
    return CFBridgingRelease(IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, 0));
}
static NSDictionary *entryInfo(io_registry_entry_t entry) {
    uint64_t registry = 0; io_string_t path = {0}; io_name_t name = {0}; io_name_t className = {0};
    IORegistryEntryGetRegistryEntryID(entry, &registry);
    IORegistryEntryGetPath(entry, kIOServicePlane, path);
    IORegistryEntryGetName(entry, name);IOObjectGetClass(entry,className);
    return @{ @"registry_id":integer(registry), @"name":@(name), @"registry_class":@(className), @"speed":property(entry,(__bridge CFStringRef)IOUSBHostMatchingPropertyKeySpeed) ?: (id)NSNull.null, @"path":@(path) };
}
// Only follow the selected object's ancestry. No global device inventory or opens.
static NSArray *ancestry(io_registry_entry_t entry) {
    NSMutableArray *rows = [NSMutableArray array];
    IOObjectRetain(entry);
    for (unsigned i = 0; entry && i < 24; i++) {
        [rows addObject:entryInfo(entry)];
        io_registry_entry_t parent = 0;
        IOReturn rc = IORegistryEntryGetParentEntry(entry, kIOServicePlane, &parent);
        IOObjectRelease(entry); entry = rc == kIOReturnSuccess ? parent : 0;
    }
    if (entry) IOObjectRelease(entry);
    return rows;
}
static bool matchesNumber(io_registry_entry_t entry, CFStringRef key, uint64_t expected) {
    id found = property(entry, key);
    return [found isKindOfClass:NSNumber.class] && [found unsignedLongLongValue] == expected;
}
// Serial suffix is the factory silicon identity carried in UTS1/UFS1/UFA1.
static NSString *siliconForSerial(NSString *serial) {
    if(serial.length<16 || serial.length>128)return nil;
    for(NSUInteger i=0;i<serial.length;i++){unichar c=[serial characterAtIndex:i];if(c<33||c>126)return nil;}
    NSString *suffix=[[serial substringFromIndex:serial.length-16] lowercaseString];
    if([suffix isEqual:@"0000000000000000"] || [suffix rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet]].location!=NSNotFound)return nil;
    return suffix;
}
// Stable identity order, independent of physical USB port or registry order.
// Duplicate identities are not eligible; never guess which physical unit owns one.
static NSDictionary *selectDevice(NSArray *candidates,NSString *pin,NSString *after) {
    NSArray *sorted=[candidates sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *a,NSDictionary *b){return [a[@"serial"] compare:b[@"serial"]];}];
    NSMutableArray *eligible=[NSMutableArray array];
    for(NSDictionary *row in sorted){
        NSString *serial=row[@"serial"];unsigned count=0;
        for(NSDictionary *other in sorted)if([other[@"silicon"] isEqual:row[@"silicon"]])count++;
        if(count==1 && (!pin || [pin isEqual:serial]))[eligible addObject:row];
    }
    if(after)for(NSDictionary *row in eligible)if([row[@"serial"] compare:after]==NSOrderedDescending)return row;
    return eligible.firstObject;
}
static NSDictionary *discover(NSString *pin,NSString *after,uint64_t vid,uint64_t pid) {
    io_iterator_t iterator=0;
    if(IOServiceGetMatchingServices(kIOMainPortDefault,IOServiceMatching("IOUSBHostInterface"),&iterator)!=KERN_SUCCESS)return nil;
    NSMutableArray *candidates=[NSMutableArray array];io_service_t entry;
    while((entry=IOIteratorNext(iterator))){
        io_registry_entry_t parent=0;
        if(matchesNumber(entry,CFSTR("bInterfaceClass"),255)&&matchesNumber(entry,CFSTR("bInterfaceSubClass"),84)&&matchesNumber(entry,CFSTR("bInterfaceProtocol"),1)&&
           IORegistryEntryGetParentEntry(entry,kIOServicePlane,&parent)==KERN_SUCCESS&&
           matchesNumber(parent,CFSTR("idVendor"),vid)&&matchesNumber(parent,CFSTR("idProduct"),pid)){
            id serial=property(parent,CFSTR("USB Serial Number"));
            NSString *silicon=[serial isKindOfClass:NSString.class]?siliconForSerial(serial):nil;
            if(silicon){uint64_t registry=0;IORegistryEntryGetRegistryEntryID(entry,&registry);
                [candidates addObject:@{@"serial":serial,@"silicon":silicon,@"registry_id":integer(registry),@"interface_number":property(entry,CFSTR("bInterfaceNumber")) ?: (id)NSNull.null}];}
        }
        if(parent)IOObjectRelease(parent);IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    NSDictionary *selected=selectDevice(candidates,pin,after);
    emit(@{@"event":@"discovery",@"candidates":candidates,@"selected":selected ?: (id)NSNull.null,@"policy":@"stable_serial_order_skip_duplicate_identities"});
    return selected;
}
// IOUSBHost owns one registry service exclusively. A reboot creates a NEW
// service while an old client can still hold its dead object. Keep a stable
// same-user silicon lock across that boundary; never unlink a live lock inode.
// This is cooperative ownership, in addition to IOUSBHost's service exclusion.
static NSString *ownershipDirectory(void) {
    // TMPDIR can differ between Terminal, app and build contexts. All normal
    // helpers for this uid must contend on the same stable, private directory.
    return [NSString stringWithFormat:@"/private/tmp/org.ptob.usb-ownership-%u",(unsigned)getuid()];
}
static int ownershipLock(NSString *directory, NSString *silicon, uint64_t vid, uint64_t pid) {
    if (!siliconForSerial(silicon) || silicon.length!=16) { errno=EINVAL; return -1; }
    const char *root=directory.fileSystemRepresentation;
    if (mkdir(root,0700) && errno!=EEXIST) return -1;
    struct stat st;
    if (lstat(root,&st) || !S_ISDIR(st.st_mode) || st.st_uid!=getuid() || (st.st_mode&077)!=0) { errno=EACCES; return -1; }
    NSString *path=[directory stringByAppendingPathComponent:[NSString stringWithFormat:@"%04llx-%04llx-%@.lock",(unsigned long long)vid,(unsigned long long)pid,silicon]];
    int fd=open(path.fileSystemRepresentation,O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return -1;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_uid!=getuid() || st.st_nlink!=1 || (st.st_mode&077)!=0) { close(fd);errno=EACCES;return -1; }
    if(flock(fd,LOCK_EX|LOCK_NB)){int saved=errno;close(fd);errno=saved;return -1;}
    return fd;
}
static bool staleInterfaceError(NSError *error) {
    if(![error.domain isEqual:IOUSBHostErrorDomain])return false;
    switch((IOReturn)error.code){
        case kIOReturnNotResponding: case kIOReturnNotReady: case kIOReturnNoDevice:
        case kIOReturnOffline: case kIOReturnBadArgument: return true;
        default:return false;
    }
}
static atomic_bool serviceTerminated;
static IOUSBHostInterface *openInterface(uint64_t registry, uint64_t vid, uint64_t pid,
                                        NSString *serial, uint64_t interface, NSArray **topology) {
    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IORegistryEntryIDMatching(registry));
    if (!service) { fprintf(stderr, "Selected registry ID is absent.\n"); return nil; }
    IOUSBHostInterface *object = nil;
    io_registry_entry_t parent = 0;
    if (!IOObjectConformsTo(service, "IOUSBHostInterface") ||
        !matchesNumber(service, CFSTR("bInterfaceNumber"), interface) ||
        !matchesNumber(service, CFSTR("bInterfaceClass"), 0xff) ||
        !matchesNumber(service, CFSTR("bInterfaceSubClass"), 0x54) ||
        !matchesNumber(service, CFSTR("bInterfaceProtocol"), 1) ||
        IORegistryEntryGetParentEntry(service, kIOServicePlane, &parent) != kIOReturnSuccess ||
        !IOObjectConformsTo(parent, "IOUSBHostDevice") ||
        !matchesNumber(parent, CFSTR("idVendor"), vid) || !matchesNumber(parent, CFSTR("idProduct"), pid) ||
        ![property(parent, CFSTR("USB Serial Number")) isEqual:serial]) {
        fprintf(stderr, "Interface/device identity mismatch; no user client opened.\n");
    } else {
        *topology = ancestry(service);
        NSError *error = nil;
        object = [[IOUSBHostInterface alloc] initWithIOService:service options:IOUSBHostObjectInitOptionsNone
                                                      queue:nil error:&error interestHandler:^(IOUSBHostObject *hostObject,uint32_t message,void *argument){
            (void)hostObject;(void)argument;
            if(message==kIOMessageServiceIsTerminated)atomic_store_explicit(&serviceTerminated,true,memory_order_relaxed);
        }];
        if (!object) fprintf(stderr, "Interface access failed: %s\n", error.description.UTF8String);
    }
    if (parent) IOObjectRelease(parent);
    IOObjectRelease(service);
    return object;
}
int main(int argc, const char **argv) {
 @autoreleasepool {
    bool clockOnly = false, deviceSOF = false, deviceSource = false, phasePaced = false, frameAnchor = false, automatic = false, haveID = false, haveVID = false, havePID = false, haveIF = false;
    uint64_t registry = 0, vid = 0, pid = 0, interface = 0, samples = 1000, interval = 1000, load = 0, duration = 120;
    NSString *serial = nil, *afterSerial = nil;
    if (argc == 2 && !strcmp(argv[1], "--help")) { usage(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--self-test")) {
        NSString *canonicalOwnership=ownershipDirectory();
        const char *oldTemporary=getenv("TMPDIR");char *savedTemporary=oldTemporary?strdup(oldTemporary):NULL;
        if(setenv("TMPDIR","/private/tmp/ptob-parent-env",1) || ![ownershipDirectory() isEqual:canonicalOwnership])return 1;
        char testPath[]="/private/tmp/ptob-usb-lock-test.XXXXXX";
        char *made=mkdtemp(testPath);if(!made)return 1;
        NSString *testDirectory=@(made),*testSilicon=@"0000000000000001";
        int first=ownershipLock(testDirectory,testSilicon,0x2fe3,0x0100);
        int duplicate=ownershipLock(testDirectory,testSilicon,0x2fe3,0x0100);
        int separate=ownershipLock(testDirectory,@"0000000000000002",0x2fe3,0x0100);
        if(first<0 || duplicate>=0 || separate<0)return 1;
        // Exercise the actual kernel exclusion in a separate process. Close
        // inherited descriptors first so the child tests a distinct open.
        pid_t child=fork();if(child<0)return 1;
        if(!child){
            close(first);close(separate);
            if(setenv("TMPDIR","/private/tmp/ptob-distinct-child-env",1) || ![ownershipDirectory() isEqual:canonicalOwnership])_exit(2);
            int own=ownershipLock(testDirectory,testSilicon,0x2fe3,0x0100);
            if(own>=0)close(own);
            _exit(own<0&&(errno==EWOULDBLOCK||errno==EAGAIN)?0:1);
        }
        int childStatus=0;
        if(waitpid(child,&childStatus,0)!=child || !WIFEXITED(childStatus) || WEXITSTATUS(childStatus))return 1;
        close(first);int reacquired=ownershipLock(testDirectory,testSilicon,0x2fe3,0x0100);
        if(reacquired<0)return 1;close(reacquired);close(separate);
        [[NSFileManager defaultManager] removeItemAtPath:testDirectory error:nil];
        if(savedTemporary){setenv("TMPDIR",savedTemporary,1);free(savedTemporary);}else unsetenv("TMPDIR");
        if(!staleInterfaceError([NSError errorWithDomain:IOUSBHostErrorDomain code:kIOReturnNotResponding userInfo:nil]) ||
           !staleInterfaceError([NSError errorWithDomain:IOUSBHostErrorDomain code:kIOReturnBadArgument userInfo:nil]) ||
           staleInterfaceError([NSError errorWithDomain:IOUSBHostErrorDomain code:kIOReturnTimeout userInfo:nil]) ||
           staleInterfaceError([NSError errorWithDomain:@"OtherDomain" code:kIOReturnBadArgument userInfo:nil]))return 1;
        uint64_t n;
        NSArray *devices=@[@{@"serial":@"TC-03-0000000000000003",@"silicon":@"0000000000000003"},@{@"serial":@"TC-02-0000000000000002",@"silicon":@"0000000000000002"}];
        if(![siliconForSerial(@"TC-02-0000000000000002") isEqual:@"0000000000000002"] || siliconForSerial(@"bad") || siliconForSerial(@"TC-00-0000000000000000") ||
           ![selectDevice(devices,nil,nil)[@"serial"] isEqual:devices[1][@"serial"]] ||
           ![selectDevice(devices,nil,devices[1][@"serial"])[@"serial"] isEqual:devices[0][@"serial"]] ||
           ![selectDevice(devices,nil,devices[0][@"serial"])[@"serial"] isEqual:devices[1][@"serial"]] ||
           selectDevice(devices,@"missing",nil) || selectDevice(@[devices[0],devices[0]],nil,nil))return 1;
        if (![integer(((__int128)1 << 64) - 1) isEqual:@"18446744073709551615"] ||
            ![ns(-25, (mach_timebase_info_data_t){125, 3}) isEqual:@"-1041"] ||
            number("-1", &n) || number("18446744073709551616", &n) ||
            !number("0x54", &n) || n != 84) return 1;
        NSMutableData *packet=[NSMutableData dataWithLength:80];uint8_t *p=packet.mutableBytes;
        memcpy(p,"UFS1",4);p[4]=1;p[6]=80;p[8]=0xee;p[15]=0x45;p[16]=1;p[24]=7;p[28]=1;
        p[40]=0xff;p[41]=7;p[42]=1;p[44]=0xef;p[45]=0xcd;p[46]=0xab;p[47]=0x89;p[48]=0x67;p[49]=0x45;p[50]=0x23;p[51]=1;
        NSDictionary *decoded=rawSOF(packet);
        if(![decoded[@"tick"] isEqual:@"81985529216486895"] || ![decoded[@"frame"] isEqual:@2047] || ![decoded[@"usb_epoch"] isEqual:@7])return 1;
        p[42]=2;if(rawSOF(packet))return 1;p[42]=1;p[0]='X';if(rawSOF(packet))return 1;
        if(rawSOF([NSMutableData dataWithLength:79]))return 1;
        NSMutableData *source=[NSMutableData dataWithLength:256];uint8_t *q=source.mutableBytes;
        memcpy(q,"UTS1",4);q[4]=1;q[7]=1;q[36]=1;q[40]=0xff;q[41]=7;q[42]=1;q[72]=1;
        q[48]=0xef;q[49]=0xcd;q[50]=0xab;q[51]=0x89;q[52]=0x67;q[53]=0x45;q[54]=0x23;q[55]=1;
        memset(q+160,0xff,4);q[164]=0xfe;memset(q+165,0xff,3);memset(q+172,0xff,4);q[200]=0xab;q[215]=0xcd;
        NSDictionary *map=sourceRecord(source,1);
        if(![map[@"tick"] isEqual:@"81985529216486895"] || ![map[@"map_ppb"] isEqual:@(-1)] || ![map[@"map_phase_ns"] isEqual:@(-2)] || ![map[@"bound_ns"] isEqual:@4294967295ULL] || ![map[@"path_scope"] isEqual:@"ab0000000000000000000000000000cd"])return 1;
        if(sourceRecord(source,2))return 1;
        q[36]=2;if(sourceRecord(source,1))return 1;q[36]=1;
        q[42]=2;if(sourceRecord(source,1))return 1;q[42]=1;
        q[41]=8;if(sourceRecord(source,1))return 1;q[41]=7;
        q[4]=2;if(sourceRecord(source,1))return 1;q[4]=1;
        if(sourceRecord([NSMutableData dataWithLength:255],1))return 1;
        memcpy(q,"UTE1",4);q[36]=31;q[248]=1;
        NSDictionary *legacy=nil,*est=estimateRecord(source,&legacy);
        if(!est||![legacy[@"valid"] boolValue]||sourceRecord(source,1))return 1;
        q[36]=6;q[248]=2;est=estimateRecord(source,&legacy);
        if(!est||[legacy[@"valid"] boolValue]||![legacy[@"utc_ns"] isEqual:@"0"])return 1;
        q[36]=7;if(estimateRecord(source,&legacy))return 1;
        q[36]=6;q[248]=0;if(estimateRecord(source,&legacy))return 1;
        memcpy(q,"UTS1",4);q[36]=1;q[248]=0;
        q[72]=2;q[40]=q[41]=0xff;if(!sourceRecord(source,2))return 1;
        q[72]=1;q[40]=3;q[41]=0;q[36]=0;q[220]=7;memcpy(q+232,"USM1",4);q[236]=1;q[238]=24;q[240]=42;q[248]=1;
        NSDictionary *observation=sourceRecord(source,1)[@"sof_map_metadata"];
        if(![observation[@"magic"] isEqual:@"USM1"]||![observation[@"schema"] isEqual:@1]||![observation[@"bytes"] isEqual:@24]||
           ![observation[@"current_source_ns"] isEqual:@"42"]||![observation[@"flags"] isEqual:@1]||![observation[@"reserved"] isEqual:@0])return 1;
        q[236]=2;q[248]=2;q[252]=1;observation=sourceRecord(source,1)[@"sof_map_metadata"];
        if(![observation[@"schema"] isEqual:@2]||![observation[@"flags"] isEqual:@2]||![observation[@"reserved"] isEqual:@1])return 1;
        q[232]='X';if(![sourceRecord(source,1)[@"sof_map_metadata"][@"magic"] isEqual:@"XSM1"])return 1;
        if(invalidReason7(source,256,sourceRecord(source,1))||!invalidReason7(source,255,nil)||
           !invalidReason7([source subdataWithRange:NSMakeRange(0,224)],224,nil)||
           invalidReason7([source subdataWithRange:NSMakeRange(0,223)],223,nil))return 1;
        q[4]=2;if(sourceRecord(source,1)||!invalidReason7(source,256,nil))return 1;q[4]=1;
        q[220]=1;if(invalidReason7(source,256,nil))return 1;q[220]=7;
        NSMutableData *a=[NSMutableData dataWithLength:64];uint8_t *z=a.mutableBytes;memcpy(z,"UFA1",4);z[4]=1;z[6]=64;z[36]=1;z[42]=19;z[44]=0x7b;
        NSDictionary *ar=anchorRecord(a);if(![ar[@"token"] isEqual:@19]||![ar[@"tick"] isEqual:@"123"])return 1;
        z[36]=2;if(anchorRecord(a))return 1;z[36]=1;z[6]=63;if(anchorRecord(a))return 1;
        puts("PASS per-silicon cross-process ownership/release, stale-I/O policy, integer/timebase, strict UFS1 UTS1 UTE1 UFA1 decoders (no IOKit access)"); return 0;
    }
    for (int i = 1; i < argc; i++) {
        const char *key = argv[i];
        if (!strcmp(key,"--frame-anchor")) {if(frameAnchor)return 2;frameAnchor=true;continue;}
        if (!strcmp(key,"--phase-paced")) { if(phasePaced)return 2;phasePaced=true;continue; }
        if (!strcmp(key,"--device-source")) { if(deviceSource)return 2;deviceSource=true;deviceSOF=true;continue; }
        if (!strcmp(key,"--discover")) { if(automatic)return 2;automatic=true;continue; }
        if (!strcmp(key,"--device-sof")) { if(deviceSOF) return 2; deviceSOF=true; continue; }
        if (!strcmp(key, "--clock-only")) { if (clockOnly) { usage(); return 2; } clockOnly = true; continue; }
        if (++i >= argc) { usage(); return 2; }
        if (!strcmp(key, "--serial")) { if (serial) return 2; serial = @(argv[i]); continue; }
        if (!strcmp(key, "--after-serial")) {if(afterSerial)return 2;afterSerial=@(argv[i]);continue;}
        uint64_t value;
        if (!number(argv[i], &value)) { usage(); return 2; }
        if (!strcmp(key, "--interface-registry-id")) { if (haveID) return 2; registry = value; haveID = true; }
        else if (!strcmp(key, "--vid")) { if (haveVID) return 2; vid = value; haveVID = true; }
        else if (!strcmp(key, "--pid")) { if (havePID) return 2; pid = value; havePID = true; }
        else if (!strcmp(key, "--interface-number")) { if (haveIF) return 2; interface = value; haveIF = true; }
        else if (!strcmp(key, "--samples")) samples = value;
        else if (!strcmp(key, "--interval-us")) interval = value;
        else if (!strcmp(key, "--load-threads")) load = value;
        else if (!strcmp(key, "--duration-seconds")) duration = value;
        else { usage(); return 2; }
    }
    bool identity = haveVID && havePID && (automatic ? !haveID&&!haveIF : serial.length&&haveID&&registry&&haveIF);
    if ((!clockOnly && !identity) || (clockOnly && (deviceSOF || automatic || haveID || haveVID || havePID || serial || haveIF)) ||
        (serial && !siliconForSerial(serial)) || (afterSerial && (!automatic || serial || !siliconForSerial(afterSerial))) || (frameAnchor&&!deviceSource) || vid > UINT16_MAX || pid > UINT16_MAX || interface > UINT8_MAX ||
        samples < 1 || samples > 460800 || interval < 125 || interval > 1000000 ||
        duration < 1 || duration > 7200 || samples * interval > duration*1000000 || load > 8 || (duration > 120 && load)) { usage(); return 2; }
    signal(SIGINT, onSignal); signal(SIGTERM, onSignal); signal(SIGPIPE, SIG_IGN);
    mach_timebase_info_data_t tb;
    if (mach_timebase_info(&tb) != KERN_SUCCESS || !tb.numer || !tb.denom) return 1;
    NSArray *topology = @[];
    if(automatic){NSDictionary *selected=discover(serial,afterSerial,vid,pid);
        if(!selected){fprintf(stderr,"No eligible relay timing interface found (or duplicate identity).\n");return 1;}
        serial=selected[@"serial"];registry=[selected[@"registry_id"] longLongValue];interface=[selected[@"interface_number"] unsignedLongLongValue];}
    int lockFD=-1;
    if(!clockOnly){
        NSString *directory=ownershipDirectory();
        lockFD=ownershipLock(directory,siliconForSerial(serial),vid,pid);
        if(lockFD<0){
            int code=errno;
            emit(@{@"event":@"ownership_refused",@"serial":serial,@"silicon":siliconForSerial(serial),@"errno":@(code),@"policy":@"EXACT_SILICON_NO_SEIZE"});
            fprintf(stderr,"USB timing interface ownership unavailable for %s: %s; no interface opened.\n",serial.UTF8String,strerror(code));return 1;
        }
    }
    atomic_init(&serviceTerminated,false);
    IOUSBHostInterface *object = clockOnly ? nil : openInterface(registry, vid, pid, serial, interface, &topology);
    if (!clockOnly && !object) {if(lockFD>=0)close(lockFD);return 1;}
    if (!emit(@{@"schema":@1, @"event":@"manifest", @"mode":clockOnly ? @"MACH_ONLY" : @"USB_REFERENCE",
                @"source_sha256":@USB_PROBE_SOURCE_SHA,
                @"device_sof_requested":@(deviceSOF), @"device_source_requested":@(deviceSource), @"frame_anchor_requested":@(frameAnchor),
                @"api":@"IOUSBHostObject.referenceMicroframeWithTime:error:", @"timestamp_units":@"MACH_ABSOLUTE_TICKS",
                @"timebase_numer":@(tb.numer), @"timebase_denom":@(tb.denom), @"requested_samples":@(samples),
                @"interval_us":@(interval), @"load_threads":@(load), @"topology":topology, @"duration_limit_s":@(duration), @"phase_paced":@(phasePaced),
                @"ownership":clockOnly ? @"NONE" : @"EXACT_SILICON_LOCK_AND_IOUSBHOST_EXCLUSIVE_SERVICE",
                @"serial":serial ?: (id)NSNull.null, @"silicon":siliconForSerial(serial) ?: (id)NSNull.null, @"registry_id":integer(registry), @"interface_number":@(interface), @"os":NSProcessInfo.processInfo.operatingSystemVersionString,
                @"frame_identity":@"UNRESOLVED", @"precision_qualified":@NO})) { [object destroy]; return 1; }
    // Optional finite CPU treatment. It never touches BLE or USB and joins on exit.
    __block atomic_bool stop; atomic_init(&stop, false);
    dispatch_group_t workers = dispatch_group_create();
    for (uint64_t j = 0; j < load; j++) {
        dispatch_group_async(workers, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            volatile uint64_t accumulator = 1;
            while (!atomic_load_explicit(&stop, memory_order_relaxed))
                for (unsigned k = 0; k < 10000; k++) accumulator = accumulator * 6364136223846793005ULL + 1;
        });
    }
    uint64_t completed = 0, failures = 0, repeats = 0, backwards = 0, future = 0, clockBreaks = 0, deviceFailures = 0;
    uint64_t lastFrame = 0, lastTime = 0; bool seen = false, ok = true, stale = false;
    uint64_t started = mach_continuous_time();
    uint64_t period=(uint64_t)((unsigned __int128)interval*1000*tb.denom/tb.numer);
    uint64_t nextSample=started+period,skippedSlots=0;
    uint64_t nextSource=started;uint16_t anchorToken=0;
    // These are kernel-backed I/O mappings. Reuse bounded buffers for the
    // serial synchronous requests instead of allocating them at sample rate.
    NSMutableData *anchorBuffer=nil,*sofBuffer=nil,*sourceBuffer=nil;
    int estimateCapability=0; //0 probe,1 negotiated,-1 legacy (one fallback)

    const uint64_t limit = (uint64_t)((unsigned __int128)duration*1000000000ULL * tb.denom / tb.numer);
    for (uint64_t i = 0; i < samples && !interrupted; i++) { @autoreleasepool {
        if (mach_continuous_time() - started >= limit) break;
        uint64_t a0 = mach_absolute_time(), c0 = mach_continuous_time(), a1 = mach_absolute_time();
        IOUSBHostTime time = 0; NSError *error = nil;
        uint64_t frame = object ? [object referenceMicroframeWithTime:&time error:&error] : 0;
        uint64_t a2 = mach_absolute_time(), c1 = mach_continuous_time(), a3 = mach_absolute_time();
        __int128 lo0 = (__int128)c0 - a1, hi0 = (__int128)c0 - a0;
        __int128 lo1 = (__int128)c1 - a3, hi1 = (__int128)c1 - a2;
        bool overlap = lo0 <= hi1 && lo1 <= hi0;
        bool valid = object && !error && time != 0;
        stale=atomic_load_explicit(&serviceTerminated,memory_order_relaxed)||staleInterfaceError(error);
        if (object && !valid) failures++;
        if (!overlap) clockBreaks++;
        if (valid && seen && frame == lastFrame && time == lastTime) repeats++;
        if (valid && seen && (frame < lastFrame || time < lastTime || ((frame == lastFrame) != (time == lastTime)))) backwards++;
        if (valid && time > a2) future++;
        NSMutableDictionary *row = [@{@"schema":@1, @"event":@"sample", @"sequence":@(i),
            @"absolute_before":integer(a0), @"continuous_before":integer(c0), @"absolute_call_before":integer(a1),
            @"absolute_call_after":integer(a2), @"continuous_after":integer(c1), @"absolute_after":integer(a3),
            @"absolute_continuous_intervals_overlap":@(overlap), @"call_duration_ns":ns((__int128)a2-a1,tb),
            @"reference_microframe":valid ? integer(frame) : (id)NSNull.null,
            @"reference_absolute":valid ? integer(time) : (id)NSNull.null,
            @"reference_age_at_return_ns":valid ? ns((__int128)a2-time,tb) : (id)NSNull.null,
            @"error_domain":error.domain ?: (id)NSNull.null,
            @"error_code":error ? @(error.code) : (id)NSNull.null,
            @"reference_valid":@(valid)} mutableCopy];
        if (valid && seen && frame > lastFrame && time >= lastTime) {
            // Raw deltas retain enough evidence to check 125us scaling without fitting away an offset.
            row[@"microframe_delta"] = integer(frame-lastFrame);
            row[@"reference_delta_ns"] = ns((__int128)time-lastTime,tb);
        }
        if(!stale&&frameAnchor&&i%8==0){
            NSError *outError=nil,*inError=nil;NSUInteger transferred=0;
            if(anchorToken==UINT16_MAX){ok=false;break;}++anchorToken;
            IOUSBDeviceRequest marker={.bmRequestType=0x41,.bRequest=0x54,.wValue=anchorToken,.wIndex=(uint16_t)interface,.wLength=0};
            uint64_t outA=mach_absolute_time(),outC=mach_continuous_time();
            BOOL stamped=[object sendDeviceRequest:marker data:nil bytesTransferred:&transferred completionTimeout:1 error:&outError];
            uint64_t outC1=mach_continuous_time(),outA1=mach_absolute_time();
            if(!anchorBuffer)anchorBuffer=[object ioDataWithCapacity:64 error:&inError];
            NSMutableData *buffer=anchorBuffer;
            IOUSBDeviceRequest fetch={.bmRequestType=0xc1,.bRequest=0x55,.wValue=anchorToken,.wIndex=(uint16_t)interface,.wLength=64};
            BOOL fetched=stamped&&buffer&&[object sendDeviceRequest:fetch data:buffer bytesTransferred:&transferred completionTimeout:1 error:&inError];
            stale=staleInterfaceError(outError)||staleInterfaceError(inError);
            NSDictionary *anchor=fetched&&transferred==64?anchorRecord(buffer):nil;
            if(anchor&&(![[serial lowercaseString] hasSuffix:anchor[@"silicon"]]||[anchor[@"token"] unsignedIntValue]!=anchorToken))anchor=nil;
            if(!anchor)deviceFailures++;
            row[@"anchor_request_token"]=@(anchorToken);row[@"frame_anchor"]=anchor?: (id)NSNull.null;row[@"anchor_out_absolute_before"]=integer(outA);row[@"anchor_out_absolute_after"]=integer(outA1);
            row[@"anchor_out_continuous_before"]=integer(outC);row[@"anchor_out_continuous_after"]=integer(outC1);
            row[@"anchor_out_error"]=outError.description?: (id)NSNull.null;row[@"anchor_in_error"]=inError.description?: (id)NSNull.null;
            IOUSBHostTime ct=0,ft=0;NSError *currentError=nil;
            uint64_t current=[object currentMicroframeWithTime:&ct error:&currentError],busframe=[object frameNumberWithTime:&ft];
            row[@"current_microframe"]=integer(current);row[@"current_microframe_absolute"]=integer(ct);row[@"bus_frame_number"]=integer(busframe);row[@"bus_frame_absolute"]=integer(ft);
            row[@"current_microframe_error"]=currentError.description?: (id)NSNull.null;
        }
        if(!stale&&deviceSOF&&!frameAnchor){
            NSError *requestError=nil;
            if(!sofBuffer)sofBuffer=[object ioDataWithCapacity:80 error:&requestError];
            NSMutableData *buffer=sofBuffer;
            NSUInteger transferred=0;
            IOUSBDeviceRequest request={.bmRequestType=0xc1,.bRequest=0x51,.wValue=0,.wIndex=(uint16_t)interface,.wLength=80};
            uint64_t d0=mach_absolute_time(),dc0=mach_continuous_time();
            BOOL sent=buffer && [object sendDeviceRequest:request data:buffer bytesTransferred:&transferred completionTimeout:1 error:&requestError];
            uint64_t dc1=mach_continuous_time(),d1=mach_absolute_time();
            stale=staleInterfaceError(requestError);
            NSDictionary *decoded=sent && transferred==80 ? rawSOF(buffer) : nil;
            if(decoded && ![[serial lowercaseString] hasSuffix:decoded[@"silicon"]]) decoded=nil;
            if(!decoded) deviceFailures++;
            row[@"device_sof"]=decoded ?: (id)NSNull.null;
            row[@"device_request_absolute_before"]=integer(d0); row[@"device_request_absolute_after"]=integer(d1);
            row[@"device_request_continuous_before"]=integer(dc0); row[@"device_request_continuous_after"]=integer(dc1);
            row[@"device_request_error_domain"]=requestError.domain ?: (id)NSNull.null;
            row[@"device_request_error_code"]=requestError ? @(requestError.code) : (id)NSNull.null;
            row[@"device_request_bytes"]=@(transferred);
        }
        if(!stale&&deviceSource){
            for(unsigned kind=1;kind<=2;kind++){
                if(kind==2&&mach_continuous_time()<nextSource)continue;
                NSError *requestError=nil;
                if(!sourceBuffer)sourceBuffer=[object ioDataWithCapacity:256 error:&requestError];
                NSMutableData *buffer=sourceBuffer;NSUInteger transferred=0;
                IOUSBDeviceRequest request={.bmRequestType=0xc1,.bRequest=kind==1?(estimateCapability>=0?0x57:0x53):0x52,.wValue=0,.wIndex=(uint16_t)interface,.wLength=256};
                uint64_t a=mach_absolute_time(),c=mach_continuous_time();
                uint64_t wallBefore=clock_gettime_nsec_np(CLOCK_REALTIME);
                BOOL sent=buffer&&[object sendDeviceRequest:request data:buffer bytesTransferred:&transferred completionTimeout:1 error:&requestError];
                uint64_t wallAfter=clock_gettime_nsec_np(CLOCK_REALTIME);
                uint64_t c1=mach_continuous_time(),a1=mach_absolute_time();
                NSDictionary *decoded=nil,*estimate=nil;
                if(kind==1&&estimateCapability>=0){
                    estimate=sent&&transferred==256?estimateRecord(buffer,&decoded):nil;
                    if(estimate){estimateCapability=1;}
                    else if(estimateCapability==0){
                        // Unsupported old firmware: exactly one capability probe.
                        // Next ordinary sample uses legacy UTS1, no extra request.
                        estimateCapability=-1;row[@"estimate_capability"]=@"UNSUPPORTED";
                    }
                }else decoded=sent&&transferred==256?sourceRecord(buffer,kind):nil;
                if(estimate&&[[serial lowercaseString] hasSuffix:estimate[@"silicon"]])row[@"local_estimate"]=estimate;
                row[@"estimate_capability"]=@(estimateCapability);
                if(decoded&&![[serial lowercaseString] hasSuffix:decoded[@"silicon"]])decoded=nil;
                NSString *key=kind==1?@"precision_source":@"device_source";
                if(sent&&invalidReason7(buffer,transferred,decoded))row[[key stringByAppendingString:@"_invalid_reason7"]]=@YES;

                row[key]=decoded?: (id)NSNull.null;
                row[[key stringByAppendingString:@"_continuous_before"]]=integer(c);row[[key stringByAppendingString:@"_continuous_after"]]=integer(c1);
                row[[key stringByAppendingString:@"_absolute_before"]]=integer(a);row[[key stringByAppendingString:@"_absolute_after"]]=integer(a1);
                row[[key stringByAppendingString:@"_wall_before_ns"]]=integer(wallBefore);row[[key stringByAppendingString:@"_wall_after_ns"]]=integer(wallAfter);
                row[[key stringByAppendingString:@"_error"]]=requestError.description?: (id)NSNull.null;
                row[[key stringByAppendingString:@"_error_domain"]]=requestError.domain?: (id)NSNull.null;
                row[[key stringByAppendingString:@"_error_code"]]=requestError?@(requestError.code):(id)NSNull.null;
                row[[key stringByAppendingString:@"_request_bytes"]]=@(transferred);
                if(!decoded)deviceFailures++;
                stale=staleInterfaceError(requestError)||atomic_load_explicit(&serviceTerminated,memory_order_relaxed);
                if(stale)break;
                if(kind==2)nextSource=mach_continuous_time()+(uint64_t)((unsigned __int128)1000000000ULL*tb.denom/tb.numer);
            }
        }
        if (valid) { lastFrame = frame; lastTime = time; seen = true; }
        completed++;
        if (!emit(row)) { ok = false; break; }
        if(stale){
            emit(@{@"event":@"transport_end",@"reason":@"STALE_INTERFACE",@"serial":serial,@"registry_id":integer(registry),@"rebind":@"SAME_SILICON_REDISCOVERY_REQUIRED",@"device_reset_requested":@NO});
            ok=false;break;
        }
        if (i+1 < samples) {
            if(phasePaced){
                uint64_t now=mach_continuous_time();
                if(now>=nextSample){uint64_t skip=(now-nextSample)/period+1;skippedSlots+=skip;nextSample+=skip*period;}
                if(nextSample>now)usleep((useconds_t)((unsigned __int128)(nextSample-now)*tb.numer/tb.denom/1000));
                nextSample+=period;
            }else usleep((useconds_t)interval);
        }
    } }
    atomic_store_explicit(&stop, true, memory_order_relaxed);
    dispatch_group_wait(workers, DISPATCH_TIME_FOREVER);
    // Mappings belong to this connection. Never carry them into a rebind.
    anchorBuffer=nil;sofBuffer=nil;sourceBuffer=nil;
    [object destroy];if(lockFD>=0)close(lockFD);
    ok = emit(@{@"schema":@1, @"event":@"summary", @"completed_samples":@(completed), @"api_failures":@(failures),
        @"repeated_references":@(repeats), @"backwards_references":@(backwards), @"future_references":@(future),
        @"clock_association_breaks":@(clockBreaks), @"interrupted":interrupted ? @YES : @NO,
        @"device_request_failures":@(deviceFailures), @"stale_interface":@(stale),
        @"phase_skipped_slots":@(skippedSlots),
        @"sample_limit_reached":completed == samples ? @YES : @NO, @"frame_identity":@"UNRESOLVED",
        @"api_access_observed":object && seen ? @YES : @NO, @"precision_qualified":@NO}) && ok;
    return ok && !failures && !deviceFailures && !backwards && !future && !clockBreaks && (completed == samples || interrupted || mach_continuous_time()-started >= limit) ? 0 : 1;
 }
}
