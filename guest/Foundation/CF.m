// CoreFoundation (toll-free bridged to our Foundation classes), plus small
// C APIs from SystemConfiguration, Security and CFNetwork.
#import <emu/Foundation.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

extern id objc_retain(id);
extern void objc_release(id);
#include <emu/msg.h>

typedef const void* CFTypeRef;
typedef const void* CFAllocatorRef;
typedef long CFIndex;
typedef unsigned long CFTypeID;
typedef unsigned int CFStringEncoding;
typedef unsigned long CFHashCode;
typedef struct { unsigned char b[16]; } CFUUIDBytes;
typedef struct { CFIndex location, length; } CFRange;

EMU_EXPORT const CFAllocatorRef kCFAllocatorDefault = NULL;
EMU_EXPORT const CFAllocatorRef kCFAllocatorSystemDefault = NULL;
EMU_EXPORT const CFAllocatorRef kCFAllocatorMalloc = NULL;
EMU_EXPORT const CFAllocatorRef kCFAllocatorNull = (CFAllocatorRef)1;
EMU_EXPORT CFTypeRef kCFBooleanTrue;
EMU_EXPORT CFTypeRef kCFBooleanFalse;
EMU_EXPORT CFTypeRef kCFNull;
EMU_EXPORT const void* kCFTypeArrayCallBacks[5];
EMU_EXPORT const void* kCFTypeDictionaryKeyCallBacks[6];
EMU_EXPORT const void* kCFTypeDictionaryValueCallBacks[5];
EMU_EXPORT const void* kCFCopyStringDictionaryKeyCallBacks[6];

__attribute__((constructor)) static void cf_init(void) {
    kCFBooleanTrue = (CFTypeRef)[NSNumber numberWithBool:YES];
    kCFBooleanFalse = (CFTypeRef)[NSNumber numberWithBool:NO];
    kCFNull = (CFTypeRef)[NSNull null];
}

EMU_EXPORT CFTypeRef CFRetain(CFTypeRef o) { return o ? (CFTypeRef)objc_retain((id)o) : NULL; }
EMU_EXPORT void CFRelease(CFTypeRef o) { if (o) objc_release((id)o); }
EMU_EXPORT CFTypeRef CFAutorelease(CFTypeRef o) { return (CFTypeRef)[(id)o autorelease]; }
EMU_EXPORT CFIndex CFGetRetainCount(CFTypeRef o) { return (CFIndex)[(id)o retainCount]; }
EMU_EXPORT CFTypeRef CFMakeCollectable(CFTypeRef o) { return o; }
EMU_EXPORT BOOL CFEqual(CFTypeRef a, CFTypeRef b) { return a == b || [(id)a isEqual:(id)b]; }
EMU_EXPORT CFHashCode CFHash(CFTypeRef o) { return [(id)o hash]; }
EMU_EXPORT CFTypeRef CFCopyDescription(CFTypeRef o) { return (CFTypeRef)[[(id)o description] retain]; }
EMU_EXPORT CFTypeID CFGetTypeID(CFTypeRef o) {
    id x = (id)o;
    if ([x isKindOfClass:[NSString class]]) return 7;
    if ([x isKindOfClass:[NSArray class]]) return 19;
    if ([x isKindOfClass:[NSDictionary class]]) return 18;
    if ([x isKindOfClass:[NSNumber class]]) return x == (id)kCFBooleanTrue || x == (id)kCFBooleanFalse ? 21 : 22;
    if ([x isKindOfClass:[NSData class]]) return 20;
    if ([x isKindOfClass:[NSDate class]]) return 42;
    return 1;
}
EMU_EXPORT CFTypeID CFStringGetTypeID(void) { return 7; }
EMU_EXPORT CFTypeID CFArrayGetTypeID(void) { return 19; }
EMU_EXPORT CFTypeID CFDictionaryGetTypeID(void) { return 18; }
EMU_EXPORT CFTypeID CFBooleanGetTypeID(void) { return 21; }
EMU_EXPORT CFTypeID CFNumberGetTypeID(void) { return 22; }
EMU_EXPORT CFTypeID CFDataGetTypeID(void) { return 20; }
EMU_EXPORT CFTypeID CFDateGetTypeID(void) { return 42; }
EMU_EXPORT BOOL CFBooleanGetValue(CFTypeRef b) { return [(id)b boolValue]; }

// ---- strings ----
#define kCFStringEncodingUTF8 0x08000100
static NSStringEncoding ns_enc(CFStringEncoding e) {
    switch (e) {
    case kCFStringEncodingUTF8: return NSUTF8StringEncoding;
    case 0x0600: return NSASCIIStringEncoding;
    case 0x0201: return NSISOLatin1StringEncoding;
    case 0x0100: return NSUnicodeStringEncoding;
    case 0x10000100: return NSUTF16BigEndianStringEncoding;
    case 0x14000100: return NSUTF16LittleEndianStringEncoding;
    case 0x0500: return NSWindowsCP1252StringEncoding;
    case 0: return NSMacOSRomanStringEncoding;
    default: return NSUTF8StringEncoding;
    }
}
EMU_EXPORT NSStringEncoding CFStringConvertEncodingToNSStringEncoding(CFStringEncoding e) { return ns_enc(e); }
EMU_EXPORT CFStringEncoding CFStringConvertNSStringEncodingToEncoding(NSStringEncoding e) {
    return e == NSASCIIStringEncoding ? 0x0600 : e == NSUnicodeStringEncoding ? 0x0100 : kCFStringEncodingUTF8;
}
EMU_EXPORT CFStringEncoding CFStringGetSystemEncoding(void) { return 0; }
EMU_EXPORT CFTypeRef CFStringCreateWithCString(CFAllocatorRef a, const char* s, CFStringEncoding e) {
    (void)a;
    return s ? (CFTypeRef)[[NSString alloc] initWithBytes:s length:strlen(s) encoding:ns_enc(e)] : NULL;
}
EMU_EXPORT CFTypeRef CFStringCreateWithCStringNoCopy(CFAllocatorRef a, const char* s, CFStringEncoding e, CFAllocatorRef d) {
    (void)d;
    return CFStringCreateWithCString(a, s, e);
}
EMU_EXPORT CFTypeRef CFStringCreateWithBytes(CFAllocatorRef a, const uint8_t* b, CFIndex n, CFStringEncoding e, BOOL bom) {
    (void)a; (void)bom;
    return (CFTypeRef)[[NSString alloc] initWithBytes:b length:(NSUInteger)n encoding:ns_enc(e)];
}
EMU_EXPORT CFTypeRef CFStringCreateWithCharacters(CFAllocatorRef a, const unichar* c, CFIndex n) {
    (void)a;
    return (CFTypeRef)[[NSString alloc] initWithCharacters:c length:(NSUInteger)n];
}
EMU_EXPORT CFTypeRef CFStringCreateCopy(CFAllocatorRef a, CFTypeRef s) { (void)a; return (CFTypeRef)[(id)s copy]; }
EMU_EXPORT CFTypeRef CFStringCreateMutable(CFAllocatorRef a, CFIndex n) { (void)a; (void)n; return (CFTypeRef)[[NSMutableString alloc] init]; }
EMU_EXPORT CFTypeRef CFStringCreateMutableCopy(CFAllocatorRef a, CFIndex n, CFTypeRef s) { (void)a; (void)n; return (CFTypeRef)[(id)s mutableCopy]; }
EMU_EXPORT void CFStringAppend(CFTypeRef m, CFTypeRef s) { [(NSMutableString*)m appendString:(NSString*)s]; }
EMU_EXPORT void CFStringAppendCString(CFTypeRef m, const char* s, CFStringEncoding e) { (void)e; [(NSMutableString*)m appendString:[NSString stringWithUTF8String:s]]; }
EMU_EXPORT CFIndex CFStringGetLength(CFTypeRef s) { return (CFIndex)[(NSString*)s length]; }
EMU_EXPORT unichar CFStringGetCharacterAtIndex(CFTypeRef s, CFIndex i) { return [(NSString*)s characterAtIndex:(NSUInteger)i]; }
EMU_EXPORT void CFStringGetCharacters(CFTypeRef s, CFRange r, unichar* buf) {
    [(NSString*)s getCharacters:buf range:NSMakeRange((NSUInteger)r.location, (NSUInteger)r.length)];
}
EMU_EXPORT const char* CFStringGetCStringPtr(CFTypeRef s, CFStringEncoding e) {
    (void)e;
    return s ? [(NSString*)s UTF8String] : NULL;
}
EMU_EXPORT const unichar* CFStringGetCharactersPtr(CFTypeRef s) { (void)s; return NULL; }
EMU_EXPORT BOOL CFStringGetCString(CFTypeRef s, char* buf, CFIndex n, CFStringEncoding e) {
    (void)e;
    const char* u = [(NSString*)s UTF8String];
    size_t l = strlen(u);
    if ((CFIndex)l + 1 > n) return NO;
    memcpy(buf, u, l + 1);
    return YES;
}
EMU_EXPORT CFIndex CFStringGetMaximumSizeForEncoding(CFIndex n, CFStringEncoding e) { (void)e; return n * 4; }
EMU_EXPORT int CFStringCompare(CFTypeRef a, CFTypeRef b, unsigned long opt) { return (int)[(NSString*)a compare:(NSString*)b options:opt]; }
EMU_EXPORT BOOL CFStringHasPrefix(CFTypeRef a, CFTypeRef b) { return [(NSString*)a hasPrefix:(NSString*)b]; }
EMU_EXPORT BOOL CFStringHasSuffix(CFTypeRef a, CFTypeRef b) { return [(NSString*)a hasSuffix:(NSString*)b]; }
EMU_EXPORT int CFStringGetIntValue(CFTypeRef s) { return [(NSString*)s intValue]; }
EMU_EXPORT double CFStringGetDoubleValue(CFTypeRef s) { return [(NSString*)s doubleValue]; }
EMU_EXPORT CFTypeRef CFStringCreateWithFormat(CFAllocatorRef a, CFTypeRef opts, CFTypeRef fmt, ...) {
    (void)a; (void)opts;
    va_list ap;
    va_start(ap, fmt);
    NSString* s = [[NSString alloc] initWithFormat:(NSString*)fmt arguments:ap];
    va_end(ap);
    return (CFTypeRef)s;
}
EMU_EXPORT CFTypeRef CFURLCreateStringByAddingPercentEscapes(CFAllocatorRef a, CFTypeRef s, CFTypeRef leave, CFTypeRef escape, CFStringEncoding e) {
    (void)a; (void)e;
    NSString* str = (NSString*)s;
    NSMutableString* out = [NSMutableString string];
    const unsigned char* p = (const unsigned char*)[str UTF8String];
    NSString* lv = (NSString*)leave;
    NSString* es = (NSString*)escape;
    for (; *p; p++) {
        char c = (char)*p;
        NSString* cs = [[[NSString alloc] initWithBytes:&c length:1 encoding:NSISOLatin1StringEncoding] autorelease];
        BOOL keep = (*p < 128) && (isalnum(*p) || strchr("-_.!~*'()", *p) || (lv && [lv rangeOfString:cs].location != NSNotFound) ||
                                   (strchr(";/?:@&=+$,#[]", *p) && !(es && [es rangeOfString:cs].location != NSNotFound)));
        if (keep) [out appendString:cs];
        else [out appendFormat:@"%%%02X", *p];
    }
    return (CFTypeRef)[out copy];
}
EMU_EXPORT CFTypeRef CFURLCreateStringByReplacingPercentEscapes(CFAllocatorRef a, CFTypeRef s, CFTypeRef leave) {
    (void)a; (void)leave;
    return (CFTypeRef)[[(NSString*)s stringByRemovingPercentEncoding] retain];
}
EMU_EXPORT CFTypeRef CFURLCreateStringByReplacingPercentEscapesUsingEncoding(CFAllocatorRef a, CFTypeRef s, CFTypeRef leave, CFStringEncoding e) {
    (void)e;
    return CFURLCreateStringByReplacingPercentEscapes(a, s, leave);
}

// ---- arrays / dictionaries / data / numbers ----
EMU_EXPORT CFTypeRef CFArrayCreate(CFAllocatorRef a, const void** v, CFIndex n, const void* cb) {
    (void)a; (void)cb;
    return (CFTypeRef)[[NSArray alloc] initWithObjects:(const id*)v count:(NSUInteger)n];
}
EMU_EXPORT CFTypeRef CFArrayCreateMutable(CFAllocatorRef a, CFIndex n, const void* cb) { (void)a; (void)n; (void)cb; return (CFTypeRef)[[NSMutableArray alloc] init]; }
EMU_EXPORT CFTypeRef CFArrayCreateCopy(CFAllocatorRef a, CFTypeRef arr) { (void)a; return (CFTypeRef)[(id)arr copy]; }
EMU_EXPORT CFIndex CFArrayGetCount(CFTypeRef a) { return (CFIndex)[(NSArray*)a count]; }
EMU_EXPORT const void* CFArrayGetValueAtIndex(CFTypeRef a, CFIndex i) { return [(NSArray*)a objectAtIndex:(NSUInteger)i]; }
EMU_EXPORT void CFArrayAppendValue(CFTypeRef a, const void* v) { [(NSMutableArray*)a addObject:(id)v]; }
EMU_EXPORT void CFArrayRemoveValueAtIndex(CFTypeRef a, CFIndex i) { [(NSMutableArray*)a removeObjectAtIndex:(NSUInteger)i]; }
EMU_EXPORT void CFArrayRemoveAllValues(CFTypeRef a) { [(NSMutableArray*)a removeAllObjects]; }
EMU_EXPORT CFTypeRef CFDictionaryCreate(CFAllocatorRef a, const void** k, const void** v, CFIndex n, const void* kc, const void* vc) {
    (void)a; (void)kc; (void)vc;
    return (CFTypeRef)[[NSDictionary alloc] initWithObjects:(const id*)v forKeys:(const id*)k count:(NSUInteger)n];
}
EMU_EXPORT CFTypeRef CFDictionaryCreateMutable(CFAllocatorRef a, CFIndex n, const void* kc, const void* vc) {
    (void)a; (void)n; (void)kc; (void)vc;
    return (CFTypeRef)[[NSMutableDictionary alloc] init];
}
EMU_EXPORT CFTypeRef CFDictionaryCreateMutableCopy(CFAllocatorRef a, CFIndex n, CFTypeRef d) { (void)a; (void)n; return (CFTypeRef)[(id)d mutableCopy]; }
EMU_EXPORT CFTypeRef CFDictionaryCreateCopy(CFAllocatorRef a, CFTypeRef d) { (void)a; return (CFTypeRef)[(id)d copy]; }
EMU_EXPORT const void* CFDictionaryGetValue(CFTypeRef d, const void* k) { return d && k ? [(NSDictionary*)d objectForKey:(id)k] : NULL; }
EMU_EXPORT BOOL CFDictionaryGetValueIfPresent(CFTypeRef d, const void* k, const void** v) {
    id r = [(NSDictionary*)d objectForKey:(id)k];
    if (v) *v = r;
    return r != nil;
}
EMU_EXPORT BOOL CFDictionaryContainsKey(CFTypeRef d, const void* k) { return [(NSDictionary*)d objectForKey:(id)k] != nil; }
EMU_EXPORT CFIndex CFDictionaryGetCount(CFTypeRef d) { return (CFIndex)[(NSDictionary*)d count]; }
EMU_EXPORT void CFDictionarySetValue(CFTypeRef d, const void* k, const void* v) { [(NSMutableDictionary*)d setObject:(id)v forKey:(id)k]; }
EMU_EXPORT void CFDictionaryAddValue(CFTypeRef d, const void* k, const void* v) {
    if (![(NSDictionary*)d objectForKey:(id)k]) [(NSMutableDictionary*)d setObject:(id)v forKey:(id)k];
}
EMU_EXPORT void CFDictionaryRemoveValue(CFTypeRef d, const void* k) { [(NSMutableDictionary*)d removeObjectForKey:(id)k]; }
EMU_EXPORT void CFDictionaryGetKeysAndValues(CFTypeRef d, const void** keys, const void** vals) {
    NSUInteger i = 0;
    for (id k in (NSDictionary*)d) {
        if (keys) keys[i] = k;
        if (vals) vals[i] = [(NSDictionary*)d objectForKey:k];
        i++;
    }
}
EMU_EXPORT CFTypeRef CFDataCreate(CFAllocatorRef a, const uint8_t* b, CFIndex n) { (void)a; return (CFTypeRef)[[NSData alloc] initWithBytes:b length:(NSUInteger)n]; }
EMU_EXPORT CFTypeRef CFDataCreateWithBytesNoCopy(CFAllocatorRef a, const uint8_t* b, CFIndex n, CFAllocatorRef d) {
    (void)a;
    return (CFTypeRef)[[NSData alloc] initWithBytesNoCopy:(void*)b length:(NSUInteger)n freeWhenDone:d != kCFAllocatorNull];
}
EMU_EXPORT CFTypeRef CFDataCreateMutable(CFAllocatorRef a, CFIndex n) { (void)a; (void)n; return (CFTypeRef)[[NSMutableData alloc] init]; }
EMU_EXPORT const uint8_t* CFDataGetBytePtr(CFTypeRef d) { return [(NSData*)d bytes]; }
EMU_EXPORT uint8_t* CFDataGetMutableBytePtr(CFTypeRef d) { return [(NSMutableData*)d mutableBytes]; }
EMU_EXPORT CFIndex CFDataGetLength(CFTypeRef d) { return (CFIndex)[(NSData*)d length]; }
EMU_EXPORT void CFDataAppendBytes(CFTypeRef d, const uint8_t* b, CFIndex n) { [(NSMutableData*)d appendBytes:b length:(NSUInteger)n]; }
EMU_EXPORT void CFDataSetLength(CFTypeRef d, CFIndex n) { [(NSMutableData*)d setLength:(NSUInteger)n]; }
EMU_EXPORT BOOL CFNumberIsFloatType(CFTypeRef n) {
    const char* t = [(NSNumber*)n objCType];
    return *t == 'f' || *t == 'd';
}
EMU_EXPORT CFTypeRef CFNumberCreate(CFAllocatorRef a, CFIndex type, const void* v) {
    (void)a;
    switch (type) {
    case 1: return (CFTypeRef)[[NSNumber numberWithChar:*(const char*)v] retain];
    case 2: return (CFTypeRef)[[NSNumber numberWithInt:*(const short*)v] retain];
    case 3: case 9: return (CFTypeRef)[[NSNumber numberWithInt:*(const int*)v] retain];
    case 4: case 10: case 11: case 15: return (CFTypeRef)[[NSNumber numberWithLongLong:*(const long long*)v] retain];
    case 5: case 12: return (CFTypeRef)[[NSNumber numberWithFloat:*(const float*)v] retain];
    default: return (CFTypeRef)[[NSNumber numberWithDouble:*(const double*)v] retain];
    }
}
EMU_EXPORT BOOL CFNumberGetValue(CFTypeRef n, CFIndex type, void* v) {
    NSNumber* x = (NSNumber*)n;
    switch (type) {
    case 1: *(char*)v = [x charValue]; break;
    case 2: *(short*)v = (short)[x intValue]; break;
    case 3: case 9: *(int*)v = [x intValue]; break;
    case 4: case 10: case 11: case 15: *(long long*)v = [x longLongValue]; break;
    case 5: case 12: *(float*)v = [x floatValue]; break;
    default: *(double*)v = [x doubleValue]; break;
    }
    return YES;
}
EMU_EXPORT CFIndex CFNumberGetType(CFTypeRef n) {
    const char* t = [(NSNumber*)n objCType];
    return *t == 'd' ? 13 : *t == 'f' ? 12 : 4;
}

// ---- UUID ----
@interface __CFUUID : NSObject {
@public
    CFUUIDBytes _b;
}
@end
@implementation __CFUUID
@end
EMU_EXPORT CFTypeRef CFUUIDCreate(CFAllocatorRef a) {
    (void)a;
    __CFUUID* u = [[__CFUUID alloc] init];
    arc4random_buf(u->_b.b, 16);
    u->_b.b[6] = (u->_b.b[6] & 0x0f) | 0x40;
    u->_b.b[8] = (u->_b.b[8] & 0x3f) | 0x80;
    return (CFTypeRef)u;
}
EMU_EXPORT CFTypeRef CFUUIDCreateFromUUIDBytes(CFAllocatorRef a, CFUUIDBytes b) {
    (void)a;
    __CFUUID* u = [[__CFUUID alloc] init];
    u->_b = b;
    return (CFTypeRef)u;
}
EMU_EXPORT CFUUIDBytes CFUUIDGetUUIDBytes(CFTypeRef u) { return ((__CFUUID*)u)->_b; }
EMU_EXPORT CFTypeRef CFUUIDCreateString(CFAllocatorRef a, CFTypeRef uu) {
    (void)a;
    const unsigned char* u = ((__CFUUID*)uu)->_b.b;
    return (CFTypeRef)[[NSString stringWithFormat:@"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", u[0], u[1], u[2],
                                                  u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]] retain];
}

// ---- run loop / bundle / misc ----
EMU_EXPORT CFTypeRef CFRunLoopGetCurrent(void) { return (CFTypeRef)[NSRunLoop currentRunLoop]; }
EMU_EXPORT CFTypeRef CFRunLoopGetMain(void) { return (CFTypeRef)[NSRunLoop mainRunLoop]; }
EMU_EXPORT void CFRunLoopRun(void) { [[NSRunLoop currentRunLoop] run]; }
EMU_EXPORT int CFRunLoopRunInMode(CFTypeRef mode, double secs, BOOL ret) {
    (void)mode; (void)ret;
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:secs]];
    return 3;
}
EMU_EXPORT void CFRunLoopStop(CFTypeRef rl) { (void)rl; }
EMU_EXPORT void CFRunLoopWakeUp(CFTypeRef rl) { (void)rl; }
EMU_EXPORT double CFAbsoluteTimeGetCurrent(void) { return [NSDate timeIntervalSinceReferenceDate]; }
EMU_EXPORT CFTypeRef CFBundleGetMainBundle(void) { return (CFTypeRef)objc_msgSend((id)NSClassFromString(@"NSBundle"), @selector(mainBundle)); }
EMU_EXPORT CFTypeRef CFBundleGetValueForInfoDictionaryKey(CFTypeRef b, CFTypeRef k) { return (CFTypeRef)objc_msgSend((id)b, @selector(objectForInfoDictionaryKey:), (id)k); }
EMU_EXPORT CFTypeRef CFBundleGetInfoDictionary(CFTypeRef b) { return (CFTypeRef)objc_msgSend((id)b, @selector(infoDictionary)); }
EMU_EXPORT CFTypeRef CFBundleGetIdentifier(CFTypeRef b) { return (CFTypeRef)objc_msgSend((id)b, @selector(bundleIdentifier)); }
EMU_EXPORT CFTypeRef CFLocaleCopyCurrent(void) { return (CFTypeRef)[objc_msgSend((id)NSClassFromString(@"NSLocale"), @selector(currentLocale)) retain]; }
EMU_EXPORT CFTypeRef CFLocaleGetValue(CFTypeRef l, CFTypeRef k) { return (CFTypeRef)objc_msgSend((id)l, @selector(objectForKey:), (id)k); }
EMU_EXPORT CFTypeRef CFLocaleCopyPreferredLanguages(void) { return (CFTypeRef)[[NSArray arrayWithObject:@"en"] retain]; }
EMU_EXPORT CFTypeRef CFLocaleGetIdentifier(CFTypeRef l) { return (CFTypeRef)objc_msgSend((id)l, @selector(localeIdentifier)); }
EMU_EXPORT CFTypeRef CFPreferencesCopyAppValue(CFTypeRef k, CFTypeRef app) {
    (void)app;
    id d = objc_msgSend((id)NSClassFromString(@"NSUserDefaults"), @selector(standardUserDefaults));
    return (CFTypeRef)[objc_msgSend(d, @selector(objectForKey:), (id)k) retain];
}
EMU_EXPORT void CFPreferencesSetAppValue(CFTypeRef k, CFTypeRef v, CFTypeRef app) {
    (void)app;
    id d = objc_msgSend((id)NSClassFromString(@"NSUserDefaults"), @selector(standardUserDefaults));
    objc_msgSend(d, @selector(setObject:forKey:), (id)v, (id)k);
}
EMU_EXPORT BOOL CFPreferencesAppSynchronize(CFTypeRef app) { (void)app; return YES; }
EMU_EXPORT CFTypeRef CFPropertyListCreateWithData(CFAllocatorRef a, CFTypeRef d, unsigned long opt, CFIndex* fmt, void** err) {
    (void)a; (void)opt;
    if (fmt) *fmt = 100;
    if (err) *err = NULL;
    return (CFTypeRef)[_emu_plist_parse((NSData*)d) retain];
}
EMU_EXPORT CFTypeRef CFPropertyListCreateDeepCopy(CFAllocatorRef a, CFTypeRef p, unsigned long opt) { (void)a; (void)opt; return (CFTypeRef)[(id)p mutableCopy]; }
EMU_EXPORT CFTypeRef CFTimeZoneCopySystem(void) { return (CFTypeRef)[objc_msgSend((id)NSClassFromString(@"NSTimeZone"), @selector(systemTimeZone)) retain]; }
EMU_EXPORT CFTypeRef CFNotificationCenterGetDarwinNotifyCenter(void) { return (CFTypeRef)1; }
EMU_EXPORT CFTypeRef CFNotificationCenterGetLocalCenter(void) { return (CFTypeRef)1; }
EMU_EXPORT void CFNotificationCenterAddObserver(CFTypeRef c, const void* o, void* cb, CFTypeRef n, const void* obj, CFIndex s) {
    (void)c; (void)o; (void)cb; (void)n; (void)obj; (void)s;
}
EMU_EXPORT void CFNotificationCenterRemoveObserver(CFTypeRef c, const void* o, CFTypeRef n, const void* obj) { (void)c; (void)o; (void)n; (void)obj; }
EMU_EXPORT void CFNotificationCenterPostNotification(CFTypeRef c, CFTypeRef n, const void* o, CFTypeRef u, BOOL d) { (void)c; (void)n; (void)o; (void)u; (void)d; }

// ===================================================================== SystemConfiguration
@interface __SCNetworkReachability : NSObject
@end
@implementation __SCNetworkReachability
@end
EMU_EXPORT CFTypeRef SCNetworkReachabilityCreateWithAddress(CFAllocatorRef a, const void* addr) { (void)a; (void)addr; return (CFTypeRef)[[__SCNetworkReachability alloc] init]; }
EMU_EXPORT CFTypeRef SCNetworkReachabilityCreateWithName(CFAllocatorRef a, const char* n) { (void)a; (void)n; return (CFTypeRef)[[__SCNetworkReachability alloc] init]; }
EMU_EXPORT BOOL SCNetworkReachabilityGetFlags(CFTypeRef r, uint32_t* flags) {
    (void)r;
    // reachable over a local (Wi-Fi like) network: kSCNetworkReachabilityFlagsReachable |
    // kSCNetworkReachabilityFlagsIsDirect. The game only allows LAN multiplayer on Wi-Fi.
    // EMU_OFFLINE=1 reports "no network" instead.
    *flags = getenv("EMU_OFFLINE") ? 0 : ((1u << 1) | (1u << 17));
    return YES;
}
EMU_EXPORT BOOL SCNetworkReachabilitySetCallback(CFTypeRef r, void* cb, void* ctx) { (void)r; (void)cb; (void)ctx; return YES; }
EMU_EXPORT BOOL SCNetworkReachabilityScheduleWithRunLoop(CFTypeRef r, CFTypeRef rl, CFTypeRef m) { (void)r; (void)rl; (void)m; return YES; }
EMU_EXPORT BOOL SCNetworkReachabilityUnscheduleFromRunLoop(CFTypeRef r, CFTypeRef rl, CFTypeRef m) { (void)r; (void)rl; (void)m; return YES; }
EMU_EXPORT BOOL SCNetworkReachabilitySetDispatchQueue(CFTypeRef r, void* q) { (void)r; (void)q; return YES; }
EMU_EXPORT CFTypeRef CFNetworkCopySystemProxySettings(void) { return (CFTypeRef)[[NSDictionary alloc] init]; }
EMU_EXPORT NSString* const kCFNetworkProxiesHTTPPort = @"HTTPPort";
EMU_EXPORT NSString* const kCFNetworkProxiesHTTPProxy = @"HTTPProxy";
EMU_EXPORT NSString* const kCFNetworkProxiesHTTPEnable = @"HTTPEnable";

// ===================================================================== Security (no keychain)
#define SECSTR(n) EMU_EXPORT NSString* const n = @#n
SECSTR(kSecAttrAccessGroup); SECSTR(kSecAttrAccessible); SECSTR(kSecAttrAccessibleAfterFirstUnlock);
SECSTR(kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly); SECSTR(kSecAttrAccessibleAlways); SECSTR(kSecAttrAccessibleAlwaysThisDeviceOnly);
SECSTR(kSecAttrAccessibleWhenUnlocked); SECSTR(kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
SECSTR(kSecAttrAccount); SECSTR(kSecAttrDescription); SECSTR(kSecAttrGeneric); SECSTR(kSecAttrIsInvisible); SECSTR(kSecAttrLabel);
SECSTR(kSecAttrService); SECSTR(kSecClass); SECSTR(kSecClassGenericPassword); SECSTR(kSecClassKey); SECSTR(kSecClassInternetPassword);
SECSTR(kSecMatchLimit); SECSTR(kSecMatchLimitAll); SECSTR(kSecMatchLimitOne); SECSTR(kSecReturnAttributes); SECSTR(kSecReturnData);
SECSTR(kSecReturnRef); SECSTR(kSecValueData); SECSTR(kSecValueRef); SECSTR(kSecAttrKeyType); SECSTR(kSecAttrApplicationTag);
SECSTR(kSecAttrKeySizeInBits); SECSTR(kSecAttrKeyTypeRSA); SECSTR(kSecAttrSynchronizable); SECSTR(kSecAttrServer);

// Keychain: kept in <home>/Library/Keychain.plist so that items (e.g. the game's device
// id) survive restarts like on a device, and two data folders are two devices.
static NSMutableArray* g_keychain;
static NSString* kc_path(void) {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Keychain.plist"];
}
static void kc_load(void) {
    if (g_keychain) return;
    NSArray* a = [NSArray arrayWithContentsOfFile:kc_path()];
    g_keychain = a ? [a mutableCopy] : [[NSMutableArray alloc] init];
}
static void kc_save(void) {
    ((BOOL (*)(id, SEL, id, BOOL, id, id*))objc_msgSend)([(id)NSClassFromString(@"NSFileManager") performSelector:@selector(defaultManager)], @selector(createDirectoryAtPath:withIntermediateDirectories:attributes:error:), [kc_path() stringByDeletingLastPathComponent], YES, nil, NULL);
    if (![g_keychain writeToFile:kc_path() atomically:YES]) NSLog(@"[Security] could not save the keychain");
}
static void kc_trace(const char* op, NSDictionary* d) {
    if (getenv("EMU_TRACE_KEYCHAIN")) NSLog(@"[Security] %s %@", op, d);
}
static BOOL kc_match(NSDictionary* item, NSDictionary* q) {
    for (NSString* k in @[@"kSecClass", @"kSecAttrService", @"kSecAttrAccount", @"kSecAttrGeneric"]) {
        id qv = [q objectForKey:k];
        if (qv && ![qv isEqual:[item objectForKey:k]]) return NO;
    }
    return YES;
}
EMU_EXPORT int SecItemAdd(CFTypeRef attrs, CFTypeRef* result) {
    kc_load();
    NSDictionary* a = (NSDictionary*)attrs;
    kc_trace("add", a);
    for (NSDictionary* i in g_keychain) if (kc_match(i, a)) return -25299;  // errSecDuplicateItem
    [g_keychain addObject:[[a copy] autorelease]];
    kc_save();
    if (result) *result = NULL;
    return 0;
}
EMU_EXPORT int SecItemCopyMatching(CFTypeRef query, CFTypeRef* result) {
    kc_load();
    NSDictionary* q = (NSDictionary*)query;
    kc_trace("copy", q);
    NSMutableArray* found = [NSMutableArray array];
    for (NSDictionary* i in g_keychain) if (kc_match(i, q)) [found addObject:i];
    if (![found count]) { if (result) *result = NULL; return -25300; }  // errSecItemNotFound
    if (result) {
        BOOL all = [[q objectForKey:@"kSecMatchLimit"] isEqual:@"kSecMatchLimitAll"];
        BOOL data = [[q objectForKey:@"kSecReturnData"] boolValue];
        id first = [found objectAtIndex:0];
        id r = all ? (id)found : data && ![[q objectForKey:@"kSecReturnAttributes"] boolValue] ? [first objectForKey:@"kSecValueData"] : first;
        *result = (CFTypeRef)[r retain];
    }
    return 0;
}
EMU_EXPORT int SecItemUpdate(CFTypeRef query, CFTypeRef attrs) {
    kc_load();
    NSDictionary* q = (NSDictionary*)query;
    kc_trace("update", (NSDictionary*)attrs);
    for (NSUInteger i = 0; i < [g_keychain count]; i++) {
        if (!kc_match([g_keychain objectAtIndex:i], q)) continue;
        NSMutableDictionary* m = [[[g_keychain objectAtIndex:i] mutableCopy] autorelease];
        [m addEntriesFromDictionary:(NSDictionary*)attrs];
        [g_keychain replaceObjectAtIndex:i withObject:m];
        kc_save();
        return 0;
    }
    return -25300;
}
EMU_EXPORT int SecItemDelete(CFTypeRef query) {
    kc_load();
    NSDictionary* q = (NSDictionary*)query;
    kc_trace("delete", q);
    for (NSUInteger i = [g_keychain count]; i-- > 0;) if (kc_match([g_keychain objectAtIndex:i], q)) [g_keychain removeObjectAtIndex:i];
    kc_save();
    return 0;
}
EMU_EXPORT CFTypeRef SecCertificateCreateWithData(CFAllocatorRef a, CFTypeRef d) { (void)a; (void)d; return NULL; }
EMU_EXPORT CFTypeRef SecPolicyCreateSSL(BOOL server, CFTypeRef host) { (void)server; (void)host; return NULL; }
EMU_EXPORT int SecTrustCreateWithCertificates(CFTypeRef c, CFTypeRef p, CFTypeRef* t) { (void)c; (void)p; if (t) *t = NULL; return -50; }
EMU_EXPORT int SecTrustEvaluate(CFTypeRef t, uint32_t* r) { (void)t; if (r) *r = 0; return -50; }
EMU_EXPORT int SecTrustSetAnchorCertificates(CFTypeRef t, CFTypeRef c) { (void)t; (void)c; return -50; }
