// Foundation system classes: bundle, file manager, defaults, process info,
// locale, errors, exceptions, threads, run loop, timers, notifications,
// operation queues, KVC.
#import <emu/Foundation.h>

#include <dirent.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <emu/msg.h>
extern Class object_getClass(id);
extern const char* class_getName(Class);
extern void* objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void*);
extern void objc_exception_throw(id) __attribute__((noreturn));
extern int _NSGetExecutablePath(char* buf, uint32_t* size);
typedef struct objc_method* Method;
extern Method class_getInstanceMethod(Class, SEL);
extern const char* method_getTypeEncoding(Method);
extern IMP method_getImplementation(Method);
extern SEL sel_registerName(const char*);

// ===================================================================== constants
#define STR_CONST(name) EMU_EXPORT NSString* const name = @#name
STR_CONST(NSDefaultRunLoopMode);
EMU_EXPORT NSString* const NSRunLoopCommonModes = @"kCFRunLoopCommonModes";
EMU_EXPORT NSString* const kCFRunLoopDefaultMode = @"kCFRunLoopDefaultMode";
EMU_EXPORT NSString* const kCFRunLoopCommonModes = @"kCFRunLoopCommonModes";
STR_CONST(NSFileSize);
STR_CONST(NSFileType);
STR_CONST(NSFileTypeRegular);
STR_CONST(NSFileTypeDirectory);
STR_CONST(NSFilePosixPermissions);
STR_CONST(NSFileModificationDate);
STR_CONST(NSFileCreationDate);
STR_CONST(NSFileProtectionKey);
STR_CONST(NSFileProtectionNone);
STR_CONST(NSFileProtectionComplete);
STR_CONST(NSFileProtectionCompleteUntilFirstUserAuthentication);
STR_CONST(NSLocalizedDescriptionKey);
STR_CONST(NSLocalizedFailureReasonErrorKey);
STR_CONST(NSLocalizedRecoverySuggestionErrorKey);
STR_CONST(NSUnderlyingErrorKey);
STR_CONST(NSFilePathErrorKey);
STR_CONST(NSURLErrorDomain);
STR_CONST(NSURLErrorFailingURLErrorKey);
STR_CONST(NSURLErrorFailingURLStringErrorKey);
STR_CONST(NSPOSIXErrorDomain);
STR_CONST(NSMachErrorDomain);
STR_CONST(NSOSStatusErrorDomain);
STR_CONST(NSCocoaErrorDomain);
STR_CONST(NSURLIsExcludedFromBackupKey);
STR_CONST(NSURLNameKey);
STR_CONST(NSURLIsDirectoryKey);
STR_CONST(NSURLFileSizeKey);
EMU_EXPORT NSString* const NSLocaleCountryCode = @"kCFLocaleCountryCodeKey";
EMU_EXPORT NSString* const NSLocaleLanguageCode = @"kCFLocaleLanguageCodeKey";
EMU_EXPORT NSString* const NSLocaleIdentifier = @"kCFLocaleIdentifierKey";
EMU_EXPORT NSString* const NSLocaleDecimalSeparator = @"kCFLocaleDecimalSeparatorKey";
EMU_EXPORT NSString* const NSLocaleCurrencyCode = @"kCFLocaleCurrencyCodeKey";
EMU_EXPORT NSString* const NSLocaleCurrencySymbol = @"kCFLocaleCurrencySymbolKey";
STR_CONST(NSGenericException);
STR_CONST(NSRangeException);
STR_CONST(NSInvalidArgumentException);
STR_CONST(NSInternalInconsistencyException);
STR_CONST(NSMallocException);
STR_CONST(NSObjectNotAvailableException);
STR_CONST(NSDestinationInvalidException);
STR_CONST(NSCurrentLocaleDidChangeNotification);
STR_CONST(NSSystemTimeZoneDidChangeNotification);
STR_CONST(NSUserDefaultsDidChangeNotification);
STR_CONST(NSThreadWillExitNotification);
STR_CONST(NSWillBecomeMultiThreadedNotification);
STR_CONST(NSKeyValueChangeNewKey);
STR_CONST(NSKeyValueChangeOldKey);
EMU_EXPORT const double NSFoundationVersionNumber = 1241.14;
EMU_EXPORT const double kCFCoreFoundationVersionNumber = 1240.1;
EMU_EXPORT NSString* const kCFBundleVersionKey = @"CFBundleVersion";
EMU_EXPORT NSString* const kCFBundleIdentifierKey = @"CFBundleIdentifier";
EMU_EXPORT NSString* const kCFBundleNameKey = @"CFBundleName";
EMU_EXPORT NSString* const kCFBundleExecutableKey = @"CFBundleExecutable";
EMU_EXPORT NSString* const kCFBundleInfoDictionaryVersionKey = @"CFBundleInfoDictionaryVersion";

// ===================================================================== paths
EMU_EXPORT NSString* NSHomeDirectory(void) {
    const char* h = getenv("HOME");
    return [NSString stringWithUTF8String:h ? h : "/"];
}
EMU_EXPORT NSString* NSTemporaryDirectory(void) { return [NSHomeDirectory() stringByAppendingString:@"/tmp/"]; }
EMU_EXPORT NSString* NSUserName(void) { return @"mobile"; }
EMU_EXPORT NSString* NSFullUserName(void) { return @"Mobile User"; }
EMU_EXPORT NSString* NSOpenStepRootDirectory(void) { return @"/"; }
EMU_EXPORT NSArray* NSSearchPathForDirectoriesInDomains(NSUInteger dir, NSUInteger mask, BOOL expand) {
    (void)mask; (void)expand;
    NSString* h = NSHomeDirectory();
    NSString* p;
    switch (dir) {
    case 9: p = [h stringByAppendingPathComponent:@"Documents"]; break;
    case 5: p = [h stringByAppendingPathComponent:@"Library"]; break;
    case 13: p = [h stringByAppendingPathComponent:@"Library/Caches"]; break;
    case 14: p = [h stringByAppendingPathComponent:@"Library/Application Support"]; break;
    case 1: p = [h stringByAppendingPathComponent:@"Applications"]; break;
    case 15: p = [h stringByAppendingPathComponent:@"Downloads"]; break;
    default: p = [h stringByAppendingPathComponent:@"Library"]; break;
    }
    return [NSArray arrayWithObject:p];
}

// ===================================================================== NSObject additions
@implementation NSObject (EmuFoundation)
- (NSString*)description { return [NSString stringWithFormat:@"<%s: %p>", class_getName(object_getClass(self)), self]; }
- (NSString*)debugDescription { return [self description]; }
+ (NSString*)description { return [NSString stringWithUTF8String:class_getName((Class)self)]; }
+ (NSString*)debugDescription { return [self description]; }
+ (NSString*)className { return [NSString stringWithUTF8String:class_getName((Class)self)]; }
- (NSString*)className { return [NSString stringWithUTF8String:class_getName(object_getClass(self))]; }

// ---- KVC ----
static NSString* cap_first(NSString* k) {
    if (![k length]) return k;
    return [[[k substringToIndex:1] uppercaseString] stringByAppendingString:[k substringFromIndex:1]];
}
static const char* arg_type(Method m, int argi) {
    // skip return type, self, _cmd: returns pointer to the type of argument argi (0-based user argument)
    const char* t = method_getTypeEncoding(m);
    if (!t) return "@";
    int idx = -1;
    while (*t) {
        while (*t && strchr("rnNoORV", *t)) t++;
        const char* start = t;
        // skip one type
        if (*t == '{' || *t == '(' || *t == '[') {
            char open = *t, close = open == '{' ? '}' : open == '(' ? ')' : ']';
            int d = 0;
            do { if (*t == open) d++; else if (*t == close) d--; t++; } while (*t && d);
        } else if (*t == '^') { t++; if (*t) t++; }
        else if (*t == '@' && t[1] == '"') { t += 2; while (*t && *t != '"') t++; if (*t) t++; }
        else if (*t) t++;
        while (*t >= '0' && *t <= '9') t++;
        if (idx == argi + 2) return start;
        idx++;
    }
    return "@";
}
- (id)valueForKey:(NSString*)key {
    SEL s = sel_registerName([key UTF8String]);
    Method m = class_getInstanceMethod(object_getClass(self), s);
    if (!m) {
        s = sel_registerName([[@"is" stringByAppendingString:cap_first(key)] UTF8String]);
        m = class_getInstanceMethod(object_getClass(self), s);
    }
    if (!m) {
        NSLog(@"valueForKey: %@ not found on %s", key, class_getName(object_getClass(self)));
        return nil;
    }
    const char* t = method_getTypeEncoding(m);
    IMP imp = method_getImplementation(m);
    switch (t ? *t : '@') {
    case 'c': case 'B': return [NSNumber numberWithBool:((BOOL(*)(id, SEL))imp)(self, s)];
    case 'i': return [NSNumber numberWithInt:((int (*)(id, SEL))imp)(self, s)];
    case 'I': return [NSNumber numberWithUnsignedInt:((unsigned (*)(id, SEL))imp)(self, s)];
    case 'q': case 'l': return [NSNumber numberWithLongLong:((long long (*)(id, SEL))imp)(self, s)];
    case 'Q': case 'L': return [NSNumber numberWithUnsignedLongLong:((unsigned long long (*)(id, SEL))imp)(self, s)];
    case 'f': return [NSNumber numberWithFloat:((float (*)(id, SEL))imp)(self, s)];
    case 'd': return [NSNumber numberWithDouble:((double (*)(id, SEL))imp)(self, s)];
    default: return ((id(*)(id, SEL))imp)(self, s);
    }
}
- (void)setValue:(id)v forKey:(NSString*)key {
    NSString* setter = [NSString stringWithFormat:@"set%@:", cap_first(key)];
    SEL s = sel_registerName([setter UTF8String]);
    Method m = class_getInstanceMethod(object_getClass(self), s);
    if (!m) {
        NSLog(@"setValue:forKey: %@ not found on %s", key, class_getName(object_getClass(self)));
        return;
    }
    IMP imp = method_getImplementation(m);
    const char* t = arg_type(m, 0);
    switch (*t) {
    case 'c': case 'B': ((void (*)(id, SEL, BOOL))imp)(self, s, [v boolValue]); break;
    case 'i': case 'I': case 's': case 'S': ((void (*)(id, SEL, int))imp)(self, s, [v intValue]); break;
    case 'q': case 'Q': case 'l': case 'L': ((void (*)(id, SEL, long long))imp)(self, s, [v longLongValue]); break;
    case 'f': ((void (*)(id, SEL, float))imp)(self, s, [v floatValue]); break;
    case 'd': ((void (*)(id, SEL, double))imp)(self, s, [v doubleValue]); break;
    default: ((void (*)(id, SEL, id))imp)(self, s, v); break;
    }
}
- (id)valueForKeyPath:(NSString*)kp {
    id o = self;
    for (NSString* k in [kp componentsSeparatedByString:@"."]) o = [o valueForKey:k];
    return o;
}
- (void)setValue:(id)v forKeyPath:(NSString*)kp {
    NSArray* parts = [kp componentsSeparatedByString:@"."];
    id o = self;
    for (NSUInteger i = 0; i + 1 < [parts count]; i++) o = [o valueForKey:[parts objectAtIndex:i]];
    [o setValue:v forKey:[parts lastObject]];
}
- (void)setValuesForKeysWithDictionary:(NSDictionary*)d {
    for (id k in d) [self setValue:[d objectForKey:k] forKey:k];
}
- (NSDictionary*)dictionaryWithValuesForKeys:(NSArray*)keys {
    NSMutableDictionary* r = [NSMutableDictionary dictionary];
    for (NSString* k in keys) { id v = [self valueForKey:k]; [r setObject:v ? v : (id)[NSNull null] forKey:k]; }
    return r;
}
- (id)valueForUndefinedKey:(NSString*)k { (void)k; return nil; }
- (void)setValue:(id)v forUndefinedKey:(NSString*)k { (void)v; (void)k; }
- (void)setNilValueForKey:(NSString*)k { (void)k; }
// ---- KVO (no-op) ----
- (void)addObserver:(id)o forKeyPath:(NSString*)kp options:(NSUInteger)opt context:(void*)ctx { (void)o; (void)kp; (void)opt; (void)ctx; }
- (void)removeObserver:(id)o forKeyPath:(NSString*)kp { (void)o; (void)kp; }
- (void)removeObserver:(id)o forKeyPath:(NSString*)kp context:(void*)ctx { (void)o; (void)kp; (void)ctx; }
- (void)willChangeValueForKey:(NSString*)k { (void)k; }
- (void)didChangeValueForKey:(NSString*)k { (void)k; }
+ (BOOL)automaticallyNotifiesObserversForKey:(NSString*)k { (void)k; return NO; }
// ---- archiving glue ----
- (id)awakeAfterUsingCoder:(id)c { (void)c; return self; }
- (Class)classForCoder { return object_getClass(self); }
- (id)replacementObjectForCoder:(id)c { (void)c; return self; }
- (void)awakeFromNib {}
@end

// ===================================================================== NSError
@interface NSError () {
    NSString* _domain;
    NSInteger _code;
    NSDictionary* _userInfo;
}
@end
@implementation NSError
+ (instancetype)errorWithDomain:(NSString*)d code:(NSInteger)c userInfo:(NSDictionary*)u { return [[[self alloc] initWithDomain:d code:c userInfo:u] autorelease]; }
- (instancetype)initWithDomain:(NSString*)d code:(NSInteger)c userInfo:(NSDictionary*)u {
    _domain = [d copy];
    _code = c;
    _userInfo = [u copy];
    return self;
}
- (void)dealloc { [_domain release]; [_userInfo release]; [super dealloc]; }
- (id)copyWithZone:(NSZone*)z { (void)z; return [self retain]; }
- (NSString*)domain { return _domain; }
- (NSInteger)code { return _code; }
- (NSDictionary*)userInfo { return _userInfo; }
- (NSString*)localizedDescription {
    NSString* d = [_userInfo objectForKey:NSLocalizedDescriptionKey];
    return d ? d : [NSString stringWithFormat:@"The operation couldn't be completed. (%@ error %ld.)", _domain, (long)_code];
}
- (NSString*)localizedFailureReason { return [_userInfo objectForKey:NSLocalizedFailureReasonErrorKey]; }
- (NSString*)localizedRecoverySuggestion { return nil; }
- (NSString*)description { return [NSString stringWithFormat:@"Error Domain=%@ Code=%ld \"%@\"", _domain, (long)_code, [self localizedDescription]]; }
@end

// ===================================================================== NSException
@interface NSException () {
    NSString* _name;
    NSString* _reason;
    NSDictionary* _userInfo;
}
@end
@implementation NSException
+ (NSException*)exceptionWithName:(NSString*)n reason:(NSString*)r userInfo:(NSDictionary*)u { return [[[self alloc] initWithName:n reason:r userInfo:u] autorelease]; }
+ (void)raise:(NSString*)n format:(NSString*)fmt, ... {
    va_list ap;
    va_start(ap, fmt);
    NSString* r = [[[NSString alloc] initWithFormat:fmt arguments:ap] autorelease];
    va_end(ap);
    [[self exceptionWithName:n reason:r userInfo:nil] raise];
}
+ (void)raise:(NSString*)n format:(NSString*)fmt arguments:(va_list)ap {
    NSString* r = [[[NSString alloc] initWithFormat:fmt arguments:ap] autorelease];
    [[self exceptionWithName:n reason:r userInfo:nil] raise];
}
- (instancetype)initWithName:(NSString*)n reason:(NSString*)r userInfo:(NSDictionary*)u {
    _name = [n copy];
    _reason = [r copy];
    _userInfo = [u retain];
    return self;
}
- (void)dealloc { [_name release]; [_reason release]; [_userInfo release]; [super dealloc]; }
- (void)raise { objc_exception_throw(self); }
- (NSString*)name { return _name; }
- (NSString*)reason { return _reason; }
- (NSDictionary*)userInfo { return _userInfo; }
- (NSArray*)callStackSymbols { return [NSArray array]; }
- (NSArray*)callStackReturnAddresses { return [NSArray array]; }
- (NSString*)description { return _reason ? _reason : _name; }
@end
static void* g_uncaught;
EMU_EXPORT void NSSetUncaughtExceptionHandler(void* h) { g_uncaught = h; }
EMU_EXPORT void* NSGetUncaughtExceptionHandler(void) { return g_uncaught; }

@interface NSAssertionHandler : NSObject
@end
@implementation NSAssertionHandler
+ (NSAssertionHandler*)currentHandler {
    static NSAssertionHandler* h;
    if (!h) h = [[NSAssertionHandler alloc] init];
    return h;
}
- (void)handleFailureInMethod:(SEL)s object:(id)o file:(NSString*)f lineNumber:(NSInteger)l description:(NSString*)fmt, ... {
    va_list ap;
    va_start(ap, fmt);
    NSString* d = fmt ? [[[NSString alloc] initWithFormat:fmt arguments:ap] autorelease] : @"";
    va_end(ap);
    NSLog(@"*** Assertion failure in -[%s %s], %@:%ld: %@", class_getName(object_getClass(o)), (const char*)s, f, (long)l, d);
}
- (void)handleFailureInFunction:(NSString*)fn file:(NSString*)f lineNumber:(NSInteger)l description:(NSString*)fmt, ... {
    va_list ap;
    va_start(ap, fmt);
    NSString* d = fmt ? [[[NSString alloc] initWithFormat:fmt arguments:ap] autorelease] : @"";
    va_end(ap);
    NSLog(@"*** Assertion failure in %@, %@:%ld: %@", fn, f, (long)l, d);
}
@end

// ===================================================================== NSAutoreleasePool
@interface NSAutoreleasePool : NSObject {
    void* _token;
}
@end
@implementation NSAutoreleasePool
- (instancetype)init { _token = objc_autoreleasePoolPush(); return self; }
- (void)drain { void* t = _token; _token = NULL; objc_autoreleasePoolPop(t); [super release]; }
- (oneway void)release { [self drain]; }
- (id)retain { return self; }
- (id)autorelease { return self; }
+ (void)addObject:(id)o { [o autorelease]; }
- (void)addObject:(id)o { [o autorelease]; }
@end

// ===================================================================== NSProcessInfo
typedef struct { NSInteger major, minor, patch; } NSOperatingSystemVersion;
@interface NSProcessInfo : NSObject
@end
@implementation NSProcessInfo
+ (NSProcessInfo*)processInfo {
    static NSProcessInfo* p;
    if (!p) p = [[NSProcessInfo alloc] init];
    return p;
}
- (NSDictionary*)environment {
    return [NSDictionary dictionaryWithObjectsAndKeys:NSHomeDirectory(), @"HOME", NSTemporaryDirectory(), @"TMPDIR", nil];
}
- (NSArray*)arguments {
    char buf[1024];
    uint32_t n = sizeof buf;
    _NSGetExecutablePath(buf, &n);
    return [NSArray arrayWithObject:[NSString stringWithUTF8String:buf]];
}
- (NSString*)processName { return @"minecraftpe"; }
- (int)processIdentifier { return getpid(); }
- (NSString*)hostName { return @"iPhone"; }
- (NSString*)globallyUniqueString { return [(id)[NSClassFromString(@"NSUUID") performSelector:@selector(UUID)] description]; }
- (NSString*)operatingSystemVersionString { return @"Version 9.3.2 (Build 13F69)"; }
- (NSOperatingSystemVersion)operatingSystemVersion { NSOperatingSystemVersion v = {9, 3, 2}; return v; }
- (BOOL)isOperatingSystemAtLeastVersion:(NSOperatingSystemVersion)v {
    if (v.major != 9) return v.major < 9;
    if (v.minor != 3) return v.minor < 3;
    return v.patch <= 2;
}
- (unsigned long long)physicalMemory { return 2ull << 30; }
- (NSUInteger)processorCount { return (NSUInteger)sysconf(_SC_NPROCESSORS_CONF); }
- (NSUInteger)activeProcessorCount { return (NSUInteger)sysconf(_SC_NPROCESSORS_ONLN); }
- (NSTimeInterval)systemUptime { return _emu_now(); }
- (BOOL)isLowPowerModeEnabled { return NO; }
- (NSInteger)thermalState { return 0; }
- (id)beginActivityWithOptions:(unsigned long long)o reason:(NSString*)r { (void)o; (void)r; return [[NSObject new] autorelease]; }
- (void)endActivity:(id)a { (void)a; }
@end

// ===================================================================== NSFileManager
@interface NSDirectoryEnumerator : NSEnumerator {
    NSMutableArray* _items;
    NSUInteger _i;
}
@end
@implementation NSDirectoryEnumerator
static void collect(NSString* base, NSString* rel, NSMutableArray* out) {
    NSString* full = [rel length] ? [base stringByAppendingPathComponent:rel] : base;
    DIR* d = opendir([full fileSystemRepresentation]);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        NSString* name = [NSString stringWithUTF8String:e->d_name];
        NSString* r = [rel length] ? [rel stringByAppendingPathComponent:name] : name;
        [out addObject:r];
        if (e->d_type == DT_DIR) collect(base, r, out);
    }
    closedir(d);
}
- (instancetype)initWithPath:(NSString*)p {
    _items = [[NSMutableArray alloc] init];
    collect(p, @"", _items);
    return self;
}
- (void)dealloc { [_items release]; [super dealloc]; }
- (id)nextObject { return _i < [_items count] ? [_items objectAtIndex:_i++] : nil; }
- (void)skipDescendants {}
- (NSDictionary*)fileAttributes { return nil; }
@end

@interface NSFileManager : NSObject
@end
@implementation NSFileManager
+ (NSFileManager*)defaultManager {
    static NSFileManager* m;
    if (!m) m = [[NSFileManager alloc] init];
    return m;
}
- (BOOL)fileExistsAtPath:(NSString*)p isDirectory:(BOOL*)dir {
    struct stat st;
    if (!p || stat([p fileSystemRepresentation], &st) != 0) return NO;
    if (dir) *dir = (st.st_mode & S_IFMT) == S_IFDIR;
    return YES;
}
- (BOOL)fileExistsAtPath:(NSString*)p { return [self fileExistsAtPath:p isDirectory:NULL]; }
- (BOOL)isReadableFileAtPath:(NSString*)p { return [self fileExistsAtPath:p]; }
- (BOOL)isWritableFileAtPath:(NSString*)p { return [self fileExistsAtPath:p]; }
- (BOOL)isDeletableFileAtPath:(NSString*)p { return [self fileExistsAtPath:p]; }
- (BOOL)isExecutableFileAtPath:(NSString*)p { return [self fileExistsAtPath:p]; }
static NSError* posix_error(void) { return [NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:nil]; }
- (BOOL)createDirectoryAtPath:(NSString*)p withIntermediateDirectories:(BOOL)inter attributes:(NSDictionary*)a error:(NSError**)e {
    (void)a;
    BOOL isdir = NO;
    if ([self fileExistsAtPath:p isDirectory:&isdir]) {
        if (isdir && inter) return YES;
        if (e) *e = [NSError errorWithDomain:NSCocoaErrorDomain code:516 userInfo:nil];
        return NO;
    }
    if (inter) {
        NSString* parent = [p stringByDeletingLastPathComponent];
        if ([parent length] > 1 && ![self fileExistsAtPath:parent])
            if (![self createDirectoryAtPath:parent withIntermediateDirectories:YES attributes:nil error:e]) return NO;
    }
    if (mkdir([p fileSystemRepresentation], 0755) != 0 && errno != EEXIST) {
        if (e) *e = posix_error();
        return NO;
    }
    return YES;
}
- (BOOL)createDirectoryAtURL:(NSURL*)u withIntermediateDirectories:(BOOL)i attributes:(NSDictionary*)a error:(NSError**)e {
    return [self createDirectoryAtPath:(NSString*)[(id)u performSelector:@selector(path)] withIntermediateDirectories:i attributes:a error:e];
}
- (BOOL)createDirectoryAtPath:(NSString*)p attributes:(NSDictionary*)a { return [self createDirectoryAtPath:p withIntermediateDirectories:NO attributes:a error:NULL]; }
- (BOOL)createFileAtPath:(NSString*)p contents:(NSData*)d attributes:(NSDictionary*)a {
    (void)a;
    return [(d ? d : [NSData data]) writeToFile:p atomically:NO];
}
- (BOOL)removeItemAtPath:(NSString*)p error:(NSError**)e {
    BOOL dir = NO;
    if (![self fileExistsAtPath:p isDirectory:&dir]) {
        if (e) *e = [NSError errorWithDomain:NSCocoaErrorDomain code:4 userInfo:nil];
        return NO;
    }
    if (dir) {
        for (NSString* c in [self contentsOfDirectoryAtPath:p error:NULL])
            [self removeItemAtPath:[p stringByAppendingPathComponent:c] error:NULL];
        if (rmdir([p fileSystemRepresentation]) != 0) { if (e) *e = posix_error(); return NO; }
        return YES;
    }
    if (unlink([p fileSystemRepresentation]) != 0) { if (e) *e = posix_error(); return NO; }
    return YES;
}
- (BOOL)removeItemAtURL:(NSURL*)u error:(NSError**)e { return [self removeItemAtPath:(NSString*)[(id)u performSelector:@selector(path)] error:e]; }
- (BOOL)removeFileAtPath:(NSString*)p handler:(id)h { (void)h; return [self removeItemAtPath:p error:NULL]; }
- (BOOL)moveItemAtPath:(NSString*)a toPath:(NSString*)b error:(NSError**)e {
    if ([self fileExistsAtPath:b]) { if (e) *e = [NSError errorWithDomain:NSCocoaErrorDomain code:516 userInfo:nil]; return NO; }
    if (rename([a fileSystemRepresentation], [b fileSystemRepresentation]) != 0) { if (e) *e = posix_error(); return NO; }
    return YES;
}
- (BOOL)moveItemAtURL:(NSURL*)a toURL:(NSURL*)b error:(NSError**)e {
    return [self moveItemAtPath:(NSString*)[(id)a performSelector:@selector(path)] toPath:(NSString*)[(id)b performSelector:@selector(path)] error:e];
}
- (BOOL)copyItemAtPath:(NSString*)a toPath:(NSString*)b error:(NSError**)e {
    BOOL dir = NO;
    if (![self fileExistsAtPath:a isDirectory:&dir]) { if (e) *e = [NSError errorWithDomain:NSCocoaErrorDomain code:260 userInfo:nil]; return NO; }
    if ([self fileExistsAtPath:b]) { if (e) *e = [NSError errorWithDomain:NSCocoaErrorDomain code:516 userInfo:nil]; return NO; }
    if (dir) {
        if (![self createDirectoryAtPath:b withIntermediateDirectories:YES attributes:nil error:e]) return NO;
        for (NSString* c in [self contentsOfDirectoryAtPath:a error:NULL])
            if (![self copyItemAtPath:[a stringByAppendingPathComponent:c] toPath:[b stringByAppendingPathComponent:c] error:e]) return NO;
        return YES;
    }
    NSData* d = [NSData dataWithContentsOfFile:a];
    return d && [d writeToFile:b atomically:NO];
}
- (BOOL)copyItemAtURL:(NSURL*)a toURL:(NSURL*)b error:(NSError**)e {
    return [self copyItemAtPath:(NSString*)[(id)a performSelector:@selector(path)] toPath:(NSString*)[(id)b performSelector:@selector(path)] error:e];
}
- (NSArray*)contentsOfDirectoryAtPath:(NSString*)p error:(NSError**)e {
    DIR* d = opendir([p fileSystemRepresentation]);
    if (!d) {
        if (e) *e = posix_error();
        return nil;
    }
    NSMutableArray* r = [NSMutableArray array];
    struct dirent* ent;
    while ((ent = readdir(d)))
        if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, "..")) [r addObject:[NSString stringWithUTF8String:ent->d_name]];
    closedir(d);
    return r;
}
- (NSArray*)directoryContentsAtPath:(NSString*)p { return [self contentsOfDirectoryAtPath:p error:NULL]; }
- (NSArray*)contentsOfDirectoryAtURL:(NSURL*)u includingPropertiesForKeys:(NSArray*)k options:(NSUInteger)o error:(NSError**)e {
    (void)k; (void)o;
    NSString* p = (NSString*)[(id)u performSelector:@selector(path)];
    NSArray* names = [self contentsOfDirectoryAtPath:p error:e];
    if (!names) return nil;
    NSMutableArray* r = [NSMutableArray array];
    Class urlc = NSClassFromString(@"NSURL");
    for (NSString* n in names) [r addObject:objc_msgSend((id)urlc, @selector(fileURLWithPath:), [p stringByAppendingPathComponent:n])];
    return r;
}
- (NSArray*)subpathsOfDirectoryAtPath:(NSString*)p error:(NSError**)e {
    (void)e;
    NSDirectoryEnumerator* en = [[[NSDirectoryEnumerator alloc] initWithPath:p] autorelease];
    return [en allObjects];
}
- (NSArray*)subpathsAtPath:(NSString*)p { return [self subpathsOfDirectoryAtPath:p error:NULL]; }
- (NSDirectoryEnumerator*)enumeratorAtPath:(NSString*)p { return [[[NSDirectoryEnumerator alloc] initWithPath:p] autorelease]; }
- (NSDictionary*)attributesOfItemAtPath:(NSString*)p error:(NSError**)e {
    struct stat st;
    if (stat([p fileSystemRepresentation], &st) != 0) {
        if (e) *e = posix_error();
        return nil;
    }
    BOOL dir = (st.st_mode & S_IFMT) == S_IFDIR;
    return [NSDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithUnsignedLongLong:(unsigned long long)st.st_size], NSFileSize,
        dir ? NSFileTypeDirectory : NSFileTypeRegular, NSFileType,
        [NSNumber numberWithUnsignedShort:st.st_mode & 0777], NSFilePosixPermissions,
        [NSDate dateWithTimeIntervalSince1970:(double)st.st_mtimespec.tv_sec], NSFileModificationDate,
        [NSDate dateWithTimeIntervalSince1970:(double)st.st_birthtimespec.tv_sec], NSFileCreationDate, nil];
}
- (NSDictionary*)fileAttributesAtPath:(NSString*)p traverseLink:(BOOL)t { (void)t; return [self attributesOfItemAtPath:p error:NULL]; }
- (BOOL)setAttributes:(NSDictionary*)a ofItemAtPath:(NSString*)p error:(NSError**)e { (void)a; (void)p; (void)e; return YES; }
- (NSDictionary*)attributesOfFileSystemForPath:(NSString*)p error:(NSError**)e {
    (void)p; (void)e;
    return [NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithUnsignedLongLong:32ull << 30], @"NSFileSystemSize",
                                                      [NSNumber numberWithUnsignedLongLong:16ull << 30], @"NSFileSystemFreeSize", nil];
}
- (NSData*)contentsAtPath:(NSString*)p { return [NSData dataWithContentsOfFile:p]; }
- (BOOL)contentsEqualAtPath:(NSString*)a andPath:(NSString*)b { return [[NSData dataWithContentsOfFile:a] isEqualToData:[NSData dataWithContentsOfFile:b]]; }
- (NSString*)currentDirectoryPath { char b[1024]; return [NSString stringWithUTF8String:getcwd(b, sizeof b) ? b : "/"]; }
- (BOOL)changeCurrentDirectoryPath:(NSString*)p { return chdir([p fileSystemRepresentation]) == 0; }
- (const char*)fileSystemRepresentationWithPath:(NSString*)p { return [p fileSystemRepresentation]; }
- (NSString*)stringWithFileSystemRepresentation:(const char*)s length:(NSUInteger)n {
    return [[[NSString alloc] initWithBytes:s length:n encoding:NSUTF8StringEncoding] autorelease];
}
- (NSArray*)URLsForDirectory:(NSUInteger)d inDomains:(NSUInteger)m {
    NSString* p = [NSSearchPathForDirectoriesInDomains(d, m, YES) objectAtIndex:0];
    [self createDirectoryAtPath:p withIntermediateDirectories:YES attributes:nil error:NULL];
    return [NSArray arrayWithObject:objc_msgSend((id)NSClassFromString(@"NSURL"), @selector(fileURLWithPath:), p)];
}
- (NSURL*)URLForDirectory:(NSUInteger)d inDomain:(NSUInteger)m appropriateForURL:(NSURL*)u create:(BOOL)c error:(NSError**)e {
    (void)u; (void)c; (void)e;
    return [[self URLsForDirectory:d inDomains:m] objectAtIndex:0];
}
- (NSURL*)containerURLForSecurityApplicationGroupIdentifier:(NSString*)g { (void)g; return nil; }
- (id)ubiquityIdentityToken { return nil; }
- (NSURL*)URLForUbiquityContainerIdentifier:(NSString*)i { (void)i; return nil; }
@end

// ===================================================================== NSBundle
@interface NSBundle () {
    NSString* _path;
    NSDictionary* _info;
    NSMutableDictionary* _tables;
}
@end
@implementation NSBundle
static NSBundle* g_main_bundle;
+ (NSBundle*)mainBundle {
    if (!g_main_bundle) {
        char buf[1024];
        uint32_t n = sizeof buf;
        _NSGetExecutablePath(buf, &n);
        NSString* exe = [NSString stringWithUTF8String:buf];
        g_main_bundle = [[NSBundle alloc] initWithPath:[exe stringByDeletingLastPathComponent]];
    }
    return g_main_bundle;
}
+ (NSBundle*)bundleForClass:(Class)c { (void)c; return [self mainBundle]; }
+ (NSBundle*)bundleWithPath:(NSString*)p { return [[[self alloc] initWithPath:p] autorelease]; }
+ (NSBundle*)bundleWithURL:(NSURL*)u { return [self bundleWithPath:(NSString*)[(id)u performSelector:@selector(path)]]; }
+ (NSBundle*)bundleWithIdentifier:(NSString*)i { return [i isEqualToString:@"com.mojang.minecraftpe"] ? [self mainBundle] : nil; }
+ (NSArray*)allBundles { return [NSArray arrayWithObject:[self mainBundle]]; }
+ (NSArray*)allFrameworks { return [NSArray array]; }
+ (NSArray*)preferredLocalizationsFromArray:(NSArray*)a { return [a count] ? [NSArray arrayWithObject:[a objectAtIndex:0]] : a; }
- (instancetype)initWithPath:(NSString*)p {
    BOOL dir = NO;
    if (![[NSFileManager defaultManager] fileExistsAtPath:p isDirectory:&dir] || !dir) { [self release]; return nil; }
    _path = [p copy];
    _tables = [[NSMutableDictionary alloc] init];
    return self;
}
- (void)dealloc { [_path release]; [_info release]; [_tables release]; [super dealloc]; }
- (NSString*)bundlePath { return _path; }
- (NSString*)resourcePath { return _path; }
- (NSString*)executablePath { return [_path stringByAppendingPathComponent:[self objectForInfoDictionaryKey:@"CFBundleExecutable"]]; }
- (NSString*)privateFrameworksPath { return [_path stringByAppendingPathComponent:@"Frameworks"]; }
- (NSString*)builtInPlugInsPath { return [_path stringByAppendingPathComponent:@"PlugIns"]; }
- (NSURL*)bundleURL { return objc_msgSend((id)NSClassFromString(@"NSURL"), @selector(fileURLWithPath:), _path); }
- (NSURL*)resourceURL { return [self bundleURL]; }
- (NSURL*)appStoreReceiptURL { return objc_msgSend((id)NSClassFromString(@"NSURL"), @selector(fileURLWithPath:), [_path stringByAppendingPathComponent:@"StoreKit/receipt"]); }
- (NSDictionary*)infoDictionary {
    if (!_info) {
        NSData* d = [NSData dataWithContentsOfFile:[_path stringByAppendingPathComponent:@"Info.plist"]];
        id o = d ? _emu_plist_parse(d) : nil;
        _info = [([o isKindOfClass:[NSDictionary class]] ? o : [NSDictionary dictionary]) retain];
    }
    return _info;
}
- (NSDictionary*)localizedInfoDictionary { return [self infoDictionary]; }
- (id)objectForInfoDictionaryKey:(NSString*)k { return [[self infoDictionary] objectForKey:k]; }
- (NSString*)bundleIdentifier { return [self objectForInfoDictionaryKey:@"CFBundleIdentifier"]; }
- (NSArray*)preferredLocalizations { return [NSArray arrayWithObject:@"en"]; }
- (NSArray*)localizations { return [NSArray arrayWithObject:@"en"]; }
- (NSString*)developmentLocalization { return @"en"; }
- (BOOL)load { return YES; }
- (BOOL)isLoaded { return YES; }
- (Class)principalClass { return Nil; }
- (Class)classNamed:(NSString*)n { return NSClassFromString(n); }
- (NSString*)pathForResource:(NSString*)name ofType:(NSString*)ext inDirectory:(NSString*)sub forLocalization:(NSString*)loc {
    (void)loc;
    if (!name) return nil;
    NSString* file = ([ext length] && ![name hasSuffix:[@"." stringByAppendingString:ext]]) ? [name stringByAppendingPathExtension:ext] : name;
    NSFileManager* fm = [NSFileManager defaultManager];
    NSString* base = [sub length] ? [_path stringByAppendingPathComponent:sub] : _path;
    NSString* cands[] = {[base stringByAppendingPathComponent:file], [[base stringByAppendingPathComponent:@"en.lproj"] stringByAppendingPathComponent:file],
                         [[base stringByAppendingPathComponent:@"Base.lproj"] stringByAppendingPathComponent:file]};
    for (int i = 0; i < 3; i++)
        if ([fm fileExistsAtPath:cands[i]]) return cands[i];
    NSLog(@"[NSBundle] resource %@ (%@) not found in %@", name, ext, base);
    return nil;
}
- (NSString*)pathForResource:(NSString*)name ofType:(NSString*)ext inDirectory:(NSString*)sub {
    return [self pathForResource:name ofType:ext inDirectory:sub forLocalization:nil];
}
- (NSString*)pathForResource:(NSString*)name ofType:(NSString*)ext { return [self pathForResource:name ofType:ext inDirectory:nil]; }
- (NSArray*)pathsForResourcesOfType:(NSString*)ext inDirectory:(NSString*)sub {
    NSString* base = [sub length] ? [_path stringByAppendingPathComponent:sub] : _path;
    NSMutableArray* r = [NSMutableArray array];
    for (NSString* f in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:base error:NULL])
        if (![ext length] || [[f pathExtension] isEqualToString:ext]) [r addObject:[base stringByAppendingPathComponent:f]];
    return r;
}
- (NSURL*)URLForResource:(NSString*)name withExtension:(NSString*)ext subdirectory:(NSString*)sub {
    NSString* p = [self pathForResource:name ofType:ext inDirectory:sub];
    return p ? objc_msgSend((id)NSClassFromString(@"NSURL"), @selector(fileURLWithPath:), p) : nil;
}
- (NSURL*)URLForResource:(NSString*)name withExtension:(NSString*)ext { return [self URLForResource:name withExtension:ext subdirectory:nil]; }
- (NSString*)localizedStringForKey:(NSString*)key value:(NSString*)value table:(NSString*)table {
    NSString* t = [table length] ? table : @"Localizable";
    NSDictionary* d = [_tables objectForKey:t];
    if (!d) {
        NSString* p = [self pathForResource:t ofType:@"strings"];
        id o = p ? _emu_plist_parse([NSData dataWithContentsOfFile:p]) : nil;
        d = [o isKindOfClass:[NSDictionary class]] ? o : [NSDictionary dictionary];
        [_tables setObject:d forKey:t];
    }
    NSString* v = key ? [d objectForKey:key] : nil;
    if (v) return v;
    return [value length] ? value : key;
}
@end

EMU_EXPORT Class NSClassFromString(NSString* s) {
    extern Class objc_getClass(const char*);
    return s ? objc_getClass([s UTF8String]) : Nil;
}
EMU_EXPORT NSString* NSStringFromClass(Class c) { return c ? [NSString stringWithUTF8String:class_getName(c)] : nil; }
EMU_EXPORT SEL NSSelectorFromString(NSString* s) { return s ? sel_registerName([s UTF8String]) : NULL; }
EMU_EXPORT NSString* NSStringFromSelector(SEL s) { return s ? [NSString stringWithUTF8String:(const char*)s] : nil; }
EMU_EXPORT NSString* NSStringFromProtocol(Protocol* p) {
    extern const char* protocol_getName(Protocol*);
    return [NSString stringWithUTF8String:protocol_getName(p)];
}
EMU_EXPORT Protocol* NSProtocolFromString(NSString* s) {
    extern Protocol* objc_getProtocol(const char*);
    return objc_getProtocol([s UTF8String]);
}
EMU_EXPORT NSString* NSStringFromRange(NSRange r) { return [NSString stringWithFormat:@"{%lu, %lu}", (unsigned long)r.location, (unsigned long)r.length]; }
EMU_EXPORT NSString* NSLocalizedStringFromKey(NSString* k) { return k; }

// ===================================================================== NSUserDefaults
@interface NSUserDefaults () {
    NSMutableDictionary* _values;
    NSMutableDictionary* _registered;
    NSString* _file;
    pthread_mutex_t _lock;
}
@end
@implementation NSUserDefaults
+ (NSUserDefaults*)standardUserDefaults {
    static NSUserDefaults* d;
    if (!d) d = [[NSUserDefaults alloc] init];
    return d;
}
+ (void)resetStandardUserDefaults {}
- (instancetype)init {
    pthread_mutex_init(&_lock, NULL);
    _registered = [[NSMutableDictionary alloc] init];
    NSString* dir = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Preferences"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:NULL];
    _file = [[dir stringByAppendingPathComponent:@"com.mojang.minecraftpe.plist"] retain];
    NSDictionary* d = [NSDictionary dictionaryWithContentsOfFile:_file];
    _values = d ? [d mutableCopy] : [[NSMutableDictionary alloc] init];
    return self;
}
- (instancetype)initWithSuiteName:(NSString*)s { (void)s; [self release]; return [[NSUserDefaults standardUserDefaults] retain]; }
- (void)_save { [_values writeToFile:_file atomically:YES]; }
- (id)objectForKey:(NSString*)k {
    if (!k) return nil;
    pthread_mutex_lock(&_lock);
    id v = [_values objectForKey:k];
    if (!v) v = [_registered objectForKey:k];
    [[v retain] autorelease];
    pthread_mutex_unlock(&_lock);
    return v;
}
- (void)setObject:(id)v forKey:(NSString*)k {
    if (!k) return;
    pthread_mutex_lock(&_lock);
    if (v) [_values setObject:v forKey:k];
    else [_values removeObjectForKey:k];
    [self _save];
    pthread_mutex_unlock(&_lock);
}
- (void)removeObjectForKey:(NSString*)k { [self setObject:nil forKey:k]; }
- (void)registerDefaults:(NSDictionary*)d {
    pthread_mutex_lock(&_lock);
    [_registered addEntriesFromDictionary:d];
    pthread_mutex_unlock(&_lock);
}
- (BOOL)synchronize {
    pthread_mutex_lock(&_lock);
    [self _save];
    pthread_mutex_unlock(&_lock);
    return YES;
}
- (NSDictionary*)dictionaryRepresentation {
    pthread_mutex_lock(&_lock);
    NSMutableDictionary* r = [NSMutableDictionary dictionaryWithDictionary:_registered];
    [r addEntriesFromDictionary:_values];
    pthread_mutex_unlock(&_lock);
    return r;
}
- (NSDictionary*)persistentDomainForName:(NSString*)n { (void)n; return [self dictionaryRepresentation]; }
- (void)setPersistentDomain:(NSDictionary*)d forName:(NSString*)n { (void)n; for (id k in d) [self setObject:[d objectForKey:k] forKey:k]; }
- (void)removePersistentDomainForName:(NSString*)n { (void)n; }
- (NSString*)stringForKey:(NSString*)k { id v = [self objectForKey:k]; return [v isKindOfClass:[NSString class]] ? v : ([v isKindOfClass:[NSNumber class]] ? [v stringValue] : nil); }
- (NSArray*)arrayForKey:(NSString*)k { id v = [self objectForKey:k]; return [v isKindOfClass:[NSArray class]] ? v : nil; }
- (NSArray*)stringArrayForKey:(NSString*)k { return [self arrayForKey:k]; }
- (NSDictionary*)dictionaryForKey:(NSString*)k { id v = [self objectForKey:k]; return [v isKindOfClass:[NSDictionary class]] ? v : nil; }
- (NSData*)dataForKey:(NSString*)k { id v = [self objectForKey:k]; return [v isKindOfClass:[NSData class]] ? v : nil; }
- (NSURL*)URLForKey:(NSString*)k { id v = [self stringForKey:k]; return v ? objc_msgSend((id)NSClassFromString(@"NSURL"), @selector(URLWithString:), v) : nil; }
- (BOOL)boolForKey:(NSString*)k { id v = [self objectForKey:k]; return [v respondsToSelector:@selector(boolValue)] ? [v boolValue] : NO; }
- (NSInteger)integerForKey:(NSString*)k { id v = [self objectForKey:k]; return [v respondsToSelector:@selector(integerValue)] ? [v integerValue] : 0; }
- (float)floatForKey:(NSString*)k { id v = [self objectForKey:k]; return [v respondsToSelector:@selector(floatValue)] ? [v floatValue] : 0; }
- (double)doubleForKey:(NSString*)k { id v = [self objectForKey:k]; return [v respondsToSelector:@selector(doubleValue)] ? [v doubleValue] : 0; }
- (void)setBool:(BOOL)v forKey:(NSString*)k { [self setObject:[NSNumber numberWithBool:v] forKey:k]; }
- (void)setInteger:(NSInteger)v forKey:(NSString*)k { [self setObject:[NSNumber numberWithInteger:v] forKey:k]; }
- (void)setFloat:(float)v forKey:(NSString*)k { [self setObject:[NSNumber numberWithFloat:v] forKey:k]; }
- (void)setDouble:(double)v forKey:(NSString*)k { [self setObject:[NSNumber numberWithDouble:v] forKey:k]; }
- (void)setURL:(NSURL*)u forKey:(NSString*)k { [self setObject:[(id)u performSelector:@selector(absoluteString)] forKey:k]; }
- (void)setValue:(id)v forKey:(NSString*)k { [self setObject:v forKey:k]; }
- (id)valueForKey:(NSString*)k { return [self objectForKey:k]; }
@end

// ===================================================================== NSLocale / NSTimeZone
@interface NSLocale : NSObject <NSCopying> {
    NSString* _ident;
}
@end
@implementation NSLocale
+ (instancetype)currentLocale {
    static NSLocale* l;
    if (!l) l = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US"];
    return l;
}
+ (instancetype)autoupdatingCurrentLocale { return [self currentLocale]; }
+ (instancetype)systemLocale { return [self currentLocale]; }
+ (instancetype)localeWithLocaleIdentifier:(NSString*)i { return [[[self alloc] initWithLocaleIdentifier:i] autorelease]; }
+ (NSArray*)preferredLanguages { return [NSArray arrayWithObject:@"en"]; }
+ (NSArray*)availableLocaleIdentifiers { return [NSArray arrayWithObject:@"en_US"]; }
+ (NSArray*)ISOCountryCodes { return [NSArray arrayWithObject:@"US"]; }
+ (NSArray*)ISOLanguageCodes { return [NSArray arrayWithObject:@"en"]; }
+ (NSString*)canonicalLocaleIdentifierFromString:(NSString*)s { return s; }
+ (NSString*)canonicalLanguageIdentifierFromString:(NSString*)s { return s; }
+ (NSDictionary*)componentsFromLocaleIdentifier:(NSString*)s {
    NSArray* p = [s componentsSeparatedByString:@"_"];
    NSMutableDictionary* d = [NSMutableDictionary dictionary];
    if ([p count] > 0) [d setObject:[p objectAtIndex:0] forKey:NSLocaleLanguageCode];
    if ([p count] > 1) [d setObject:[p objectAtIndex:1] forKey:NSLocaleCountryCode];
    return d;
}
- (instancetype)initWithLocaleIdentifier:(NSString*)i { _ident = [i copy]; return self; }
- (void)dealloc { [_ident release]; [super dealloc]; }
- (id)copyWithZone:(NSZone*)z { (void)z; return [self retain]; }
- (NSString*)localeIdentifier { return _ident; }
- (NSString*)languageCode { return [[_ident componentsSeparatedByString:@"_"] objectAtIndex:0]; }
- (NSString*)countryCode {
    NSArray* p = [_ident componentsSeparatedByString:@"_"];
    return [p count] > 1 ? [p objectAtIndex:1] : @"US";
}
- (id)objectForKey:(id)k {
    if ([k isEqual:NSLocaleIdentifier]) return _ident;
    if ([k isEqual:NSLocaleLanguageCode]) return [self languageCode];
    if ([k isEqual:NSLocaleCountryCode]) return [self countryCode];
    if ([k isEqual:NSLocaleDecimalSeparator]) return @".";
    if ([k isEqual:NSLocaleCurrencyCode]) return @"USD";
    if ([k isEqual:NSLocaleCurrencySymbol]) return @"$";
    return nil;
}
- (NSString*)displayNameForKey:(id)k value:(id)v { (void)k; return [v description]; }
- (NSString*)decimalSeparator { return @"."; }
- (NSString*)groupingSeparator { return @","; }
- (NSString*)currencyCode { return @"USD"; }
- (NSString*)currencySymbol { return @"$"; }
- (BOOL)usesMetricSystem { return NO; }
@end

@interface NSTimeZone : NSObject <NSCopying> {
    NSInteger _offset;
    NSString* _name;
}
@end
@implementation NSTimeZone
+ (instancetype)timeZoneForSecondsFromGMT:(NSInteger)s {
    NSTimeZone* z = [[[self alloc] init] autorelease];
    z->_offset = s;
    z->_name = [(s ? [NSString stringWithFormat:@"GMT%+ld", (long)(s / 3600)] : @"GMT") retain];
    return z;
}
+ (instancetype)localTimeZone {
    static NSTimeZone* z;
    if (!z) {
        time_t t = time(NULL);
        struct tm lt;
        localtime_r(&t, &lt);
        z = [[NSTimeZone timeZoneForSecondsFromGMT:lt.tm_gmtoff] retain];
    }
    return z;
}
+ (instancetype)systemTimeZone { return [self localTimeZone]; }
+ (instancetype)defaultTimeZone { return [self localTimeZone]; }
+ (void)setDefaultTimeZone:(NSTimeZone*)z { (void)z; }
+ (void)resetSystemTimeZone {}
+ (instancetype)timeZoneWithName:(NSString*)n {
    if ([n isEqualToString:@"UTC"] || [n isEqualToString:@"GMT"]) return [self timeZoneForSecondsFromGMT:0];
    return [self localTimeZone];
}
+ (instancetype)timeZoneWithAbbreviation:(NSString*)a { return [self timeZoneWithName:a]; }
- (void)dealloc { [_name release]; [super dealloc]; }
- (id)copyWithZone:(NSZone*)z { (void)z; return [self retain]; }
- (NSInteger)secondsFromGMT { return _offset; }
- (NSInteger)secondsFromGMTForDate:(NSDate*)d { (void)d; return _offset; }
- (NSString*)name { return _name; }
- (NSString*)abbreviation { return _name; }
- (NSString*)abbreviationForDate:(NSDate*)d { (void)d; return _name; }
- (BOOL)isDaylightSavingTime { return NO; }
- (NSString*)description { return _name; }
@end

// ===================================================================== NSNotificationCenter
@interface NSNotification () {
    NSString* _name;
    id _object;
    NSDictionary* _userInfo;
}
@end
@implementation NSNotification
+ (instancetype)notificationWithName:(NSString*)n object:(id)o userInfo:(NSDictionary*)u {
    NSNotification* x = [[[self alloc] init] autorelease];
    x->_name = [n copy];
    x->_object = [o retain];
    x->_userInfo = [u retain];
    return x;
}
+ (instancetype)notificationWithName:(NSString*)n object:(id)o { return [self notificationWithName:n object:o userInfo:nil]; }
- (void)dealloc { [_name release]; [_object release]; [_userInfo release]; [super dealloc]; }
- (NSString*)name { return _name; }
- (id)object { return _object; }
- (NSDictionary*)userInfo { return _userInfo; }
@end

typedef struct observer {
    id target;  // not retained (as on iOS)
    SEL sel;
    NSString* name;
    id object;
    void (^block)(NSNotification*);
    id queue;
    id token;
    BOOL dead;
} observer;

@interface __NSObserverToken : NSObject
@end
@implementation __NSObserverToken
@end

@interface NSNotificationCenter () {
    observer* _obs;
    NSUInteger _n, _cap;
    pthread_mutex_t _lock;
}
@end
@implementation NSNotificationCenter
+ (NSNotificationCenter*)defaultCenter {
    static NSNotificationCenter* c;
    if (!c) c = [[NSNotificationCenter alloc] init];
    return c;
}
- (instancetype)init {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&_lock, &a);
    return self;
}
- (void)_add:(observer)o {
    pthread_mutex_lock(&_lock);
    if (_n == _cap) {
        _cap = _cap ? _cap * 2 : 32;
        _obs = realloc(_obs, _cap * sizeof(observer));
    }
    _obs[_n++] = o;
    pthread_mutex_unlock(&_lock);
}
- (void)addObserver:(id)t selector:(SEL)s name:(NSString*)n object:(id)obj {
    observer o = {t, s, [n copy], obj, nil, nil, nil, NO};
    [self _add:o];
}
- (id)addObserverForName:(NSString*)n object:(id)obj queue:(id)q usingBlock:(void (^)(NSNotification*))b {
    __NSObserverToken* tok = [[__NSObserverToken alloc] init];
    observer o = {nil, NULL, [n copy], obj, [b copy], [q retain], tok, NO};
    [self _add:o];
    return [tok autorelease];
}
- (void)removeObserver:(id)t name:(NSString*)n object:(id)obj {
    pthread_mutex_lock(&_lock);
    for (NSUInteger i = 0; i < _n; i++) {
        observer* o = &_obs[i];
        if (o->dead) continue;
        if (o->target != t && o->token != t) continue;
        if (n && ![n isEqualToString:o->name]) continue;
        if (obj && obj != o->object) continue;
        o->dead = YES;
    }
    pthread_mutex_unlock(&_lock);
}
- (void)removeObserver:(id)t { [self removeObserver:t name:nil object:nil]; }
- (void)postNotification:(NSNotification*)note {
    pthread_mutex_lock(&_lock);
    NSUInteger n = _n;
    observer* snap = malloc((n + 1) * sizeof(observer));
    memcpy(snap, _obs, n * sizeof(observer));
    pthread_mutex_unlock(&_lock);
    NSString* name = [note name];
    id obj = [note object];
    for (NSUInteger i = 0; i < n; i++) {
        observer* o = &snap[i];
        if (o->dead) continue;
        if (o->name && ![o->name isEqualToString:name]) continue;
        if (o->object && o->object != obj) continue;
        // re-check liveness (an earlier observer may have removed this one)
        BOOL alive = NO;
        pthread_mutex_lock(&_lock);
        if (i < _n && !_obs[i].dead) alive = YES;
        pthread_mutex_unlock(&_lock);
        if (!alive) continue;
        if (o->block) o->block(note);
        else if (o->target) objc_msgSend(o->target, o->sel, note);
    }
    free(snap);
}
- (void)postNotificationName:(NSString*)n object:(id)o userInfo:(NSDictionary*)u {
    [self postNotification:[NSNotification notificationWithName:n object:o userInfo:u]];
}
- (void)postNotificationName:(NSString*)n object:(id)o { [self postNotificationName:n object:o userInfo:nil]; }
@end

// ===================================================================== timers & run loop
@interface NSTimer () {
@public
    NSTimeInterval _fire, _interval;
    id _target;
    SEL _sel;
    id _userInfo;
    void (^_block)(NSTimer*);
    BOOL _repeats, _valid;
    id _arg;  // performSelector:afterDelay: argument
    BOOL _isPerform;
}
@end

static pthread_mutex_t g_rl_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
static NSMutableArray* g_timers;

@implementation NSTimer
+ (NSTimer*)timerWithTimeInterval:(NSTimeInterval)ti target:(id)t selector:(SEL)s userInfo:(id)u repeats:(BOOL)r {
    NSTimer* x = [[[self alloc] init] autorelease];
    x->_interval = ti < 0.0001 ? 0.0001 : ti;
    x->_fire = _emu_now() + x->_interval;
    x->_target = [t retain];
    x->_sel = s;
    x->_userInfo = [u retain];
    x->_repeats = r;
    x->_valid = YES;
    return x;
}
+ (NSTimer*)scheduledTimerWithTimeInterval:(NSTimeInterval)ti target:(id)t selector:(SEL)s userInfo:(id)u repeats:(BOOL)r {
    NSTimer* x = [self timerWithTimeInterval:ti target:t selector:s userInfo:u repeats:r];
    [[NSRunLoop mainRunLoop] addTimer:x forMode:NSDefaultRunLoopMode];
    return x;
}
+ (NSTimer*)timerWithTimeInterval:(NSTimeInterval)ti repeats:(BOOL)r block:(void (^)(NSTimer*))b {
    NSTimer* x = [self timerWithTimeInterval:ti target:nil selector:NULL userInfo:nil repeats:r];
    x->_block = [b copy];
    return x;
}
+ (NSTimer*)scheduledTimerWithTimeInterval:(NSTimeInterval)ti repeats:(BOOL)r block:(void (^)(NSTimer*))b {
    NSTimer* x = [self timerWithTimeInterval:ti repeats:r block:b];
    [[NSRunLoop mainRunLoop] addTimer:x forMode:NSDefaultRunLoopMode];
    return x;
}
+ (NSTimer*)scheduledTimerWithTimeInterval:(NSTimeInterval)ti invocation:(id)inv repeats:(BOOL)r {
    (void)inv;
    return [self scheduledTimerWithTimeInterval:ti target:nil selector:NULL userInfo:nil repeats:r];
}
- (instancetype)initWithFireDate:(NSDate*)d interval:(NSTimeInterval)ti target:(id)t selector:(SEL)s userInfo:(id)u repeats:(BOOL)r {
    _interval = ti < 0.0001 ? 0.0001 : ti;
    _fire = _emu_now() + [d timeIntervalSinceNow];
    _target = [t retain];
    _sel = s;
    _userInfo = [u retain];
    _repeats = r;
    _valid = YES;
    return self;
}
- (void)dealloc { [_target release]; [_userInfo release]; [_block release]; [_arg release]; [super dealloc]; }
- (void)invalidate {
    pthread_mutex_lock(&g_rl_lock);
    _valid = NO;
    pthread_mutex_unlock(&g_rl_lock);
}
- (BOOL)isValid { return _valid; }
- (id)userInfo { return _userInfo; }
- (NSTimeInterval)timeInterval { return _repeats ? _interval : 0; }
- (NSDate*)fireDate { return [NSDate dateWithTimeIntervalSinceNow:_fire - _emu_now()]; }
- (void)setFireDate:(NSDate*)d { _fire = _emu_now() + [d timeIntervalSinceNow]; }
- (void)setTolerance:(NSTimeInterval)t { (void)t; }
- (void)fire {
    if (!_valid) return;
    [self retain];
    if (_block) _block(self);
    else if (_isPerform) objc_msgSend(_target, _sel, _arg);
    else if (_target) objc_msgSend(_target, _sel, self);
    if (!_repeats) [self invalidate];
    [self release];
}
@end

@implementation NSRunLoop
+ (NSRunLoop*)mainRunLoop {
    static NSRunLoop* r;
    if (!r) r = [[NSRunLoop alloc] init];
    return r;
}
+ (NSRunLoop*)currentRunLoop { return [self mainRunLoop]; }
- (void)addTimer:(NSTimer*)t forMode:(NSString*)m {
    (void)m;
    pthread_mutex_lock(&g_rl_lock);
    if (!g_timers) g_timers = [[NSMutableArray alloc] init];
    if (![g_timers containsObject:t]) [g_timers addObject:t];
    pthread_mutex_unlock(&g_rl_lock);
}
- (NSString*)currentMode { return NSDefaultRunLoopMode; }
- (void)run {
    if (!pthread_main_np()) {
        for (;;) sleep(3600);  // background run loops have no sources in this emulation
    }
    for (;;) _emu_runloop_iteration(0.01);
}
- (BOOL)runMode:(NSString*)m beforeDate:(NSDate*)d {
    (void)m;
    if (!pthread_main_np()) {
        NSTimeInterval t = [d timeIntervalSinceNow];
        if (t > 0) usleep((useconds_t)((t > 1 ? 1 : t) * 1e6));
        return YES;
    }
    _emu_runloop_iteration([d timeIntervalSinceNow] > 0 ? 0.001 : 0);
    return YES;
}
- (void)runUntilDate:(NSDate*)d {
    while ([d timeIntervalSinceNow] > 0) [self runMode:NSDefaultRunLoopMode beforeDate:d];
}
- (void)performSelector:(SEL)s target:(id)t argument:(id)a order:(NSUInteger)o modes:(NSArray*)m {
    (void)o; (void)m;
    [t performSelector:s withObject:a afterDelay:0];
}
- (void)cancelPerformSelectorsWithTarget:(id)t { [NSObject cancelPreviousPerformRequestsWithTarget:t]; }
- (struct __CFRunLoop*)getCFRunLoop { return (struct __CFRunLoop*)self; }
@end

// Fires due timers and drains the main queue. Returns seconds until the next timer.
EMU_EXPORT NSTimeInterval _emu_runloop_next_timer(void) {
    NSTimeInterval now = _emu_now(), best = 1.0;
    pthread_mutex_lock(&g_rl_lock);
    for (NSTimer* t in g_timers) {
        NSTimeInterval d = t->_fire - now;
        if (d < best) best = d;
    }
    pthread_mutex_unlock(&g_rl_lock);
    return best < 0 ? 0 : best;
}
EMU_EXPORT void _emu_runloop_iteration(NSTimeInterval max_wait) {
    (void)max_wait;
    void* pool = objc_autoreleasePoolPush();
    NSTimeInterval now = _emu_now();
    NSMutableArray* due = [NSMutableArray array];
    pthread_mutex_lock(&g_rl_lock);
    for (NSUInteger i = 0; i < [g_timers count];) {
        NSTimer* t = [g_timers objectAtIndex:i];
        if (!t->_valid) { [g_timers removeObjectAtIndex:i]; continue; }
        if (t->_fire <= now) {
            [due addObject:t];
            if (t->_repeats) {
                t->_fire += t->_interval;
                if (t->_fire < now) t->_fire = now + t->_interval;
            } else {
                [g_timers removeObjectAtIndex:i];
                continue;
            }
        }
        i++;
    }
    pthread_mutex_unlock(&g_rl_lock);
    for (NSTimer* t in due) [t fire];
    _dispatch_main_queue_drain();
    objc_autoreleasePoolPop(pool);
}

@implementation NSObject (EmuPerform)
- (void)performSelector:(SEL)s withObject:(id)a afterDelay:(NSTimeInterval)d {
    NSTimer* t = [NSTimer timerWithTimeInterval:d target:self selector:s userInfo:nil repeats:NO];
    t->_arg = [a retain];
    t->_isPerform = YES;
    if (d <= 0) t->_fire = _emu_now();
    [[NSRunLoop mainRunLoop] addTimer:t forMode:NSDefaultRunLoopMode];
}
- (void)performSelector:(SEL)s withObject:(id)a afterDelay:(NSTimeInterval)d inModes:(NSArray*)m {
    (void)m;
    [self performSelector:s withObject:a afterDelay:d];
}
+ (void)cancelPreviousPerformRequestsWithTarget:(id)target selector:(SEL)s object:(id)a {
    pthread_mutex_lock(&g_rl_lock);
    for (NSTimer* t in g_timers)
        if (t->_isPerform && t->_target == target && (!s || t->_sel == s) && (!a || t->_arg == a || [t->_arg isEqual:a])) t->_valid = NO;
    pthread_mutex_unlock(&g_rl_lock);
}
+ (void)cancelPreviousPerformRequestsWithTarget:(id)target { [self cancelPreviousPerformRequestsWithTarget:target selector:NULL object:nil]; }
- (void)performSelectorOnMainThread:(SEL)s withObject:(id)a waitUntilDone:(BOOL)w {
    if (pthread_main_np()) {
        if (w) { objc_msgSend(self, s, a); return; }
        [self performSelector:s withObject:a afterDelay:0];
        return;
    }
    [self retain];
    [a retain];
    if (w) {
        dispatch_sync(dispatch_get_main_queue(), ^{ objc_msgSend(self, s, a); });
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{ objc_msgSend(self, s, a); [a release]; [self release]; });
        return;
    }
    [a release];
    [self release];
}
- (void)performSelectorOnMainThread:(SEL)s withObject:(id)a waitUntilDone:(BOOL)w modes:(NSArray*)m {
    (void)m;
    [self performSelectorOnMainThread:s withObject:a waitUntilDone:w];
}
- (void)performSelector:(SEL)s onThread:(NSThread*)t withObject:(id)a waitUntilDone:(BOOL)w {
    if ([t isMainThread]) { [self performSelectorOnMainThread:s withObject:a waitUntilDone:w]; return; }
    [self retain];
    [a retain];
    dispatch_block_t b = ^{ objc_msgSend(self, s, a); [a release]; [self release]; };
    if (w) dispatch_sync(dispatch_get_global_queue(0, 0), b);
    else dispatch_async(dispatch_get_global_queue(0, 0), b);
}
- (void)performSelectorInBackground:(SEL)s withObject:(id)a {
    [self retain];
    [a retain];
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
        void* pool = objc_autoreleasePoolPush();
        objc_msgSend(self, s, a);
        objc_autoreleasePoolPop(pool);
        [a release];
        [self release];
    });
}
@end

// ===================================================================== NSThread
@interface NSThread () {
    NSMutableDictionary* _dict;
    NSString* _name;
    id _target;
    SEL _sel;
    id _arg;
    void (^_block)(void);
    BOOL _isMain;
    double _prio;
    NSUInteger _stack;
}
@end
static pthread_key_t g_thread_key;
static pthread_once_t g_thread_once = PTHREAD_ONCE_INIT;
static void thread_key_init(void) { pthread_key_create(&g_thread_key, NULL); }
@implementation NSThread
+ (NSThread*)currentThread {
    pthread_once(&g_thread_once, thread_key_init);
    NSThread* t = pthread_getspecific(g_thread_key);
    if (!t) {
        t = [[NSThread alloc] init];
        t->_isMain = pthread_main_np() != 0;
        if (t->_isMain) t->_name = [@"main" retain];
        pthread_setspecific(g_thread_key, t);
    }
    return t;
}
+ (NSThread*)mainThread {
    static NSThread* m;
    if (!m) {
        if (pthread_main_np()) m = [[self currentThread] retain];
        else { m = [[NSThread alloc] init]; m->_isMain = YES; }
    }
    return m;
}
+ (BOOL)isMainThread { return pthread_main_np() != 0; }
- (BOOL)isMainThread { return _isMain; }
+ (BOOL)isMultiThreaded { return YES; }
+ (void)sleepForTimeInterval:(NSTimeInterval)t { if (t > 0) usleep((useconds_t)(t * 1e6)); }
+ (void)sleepUntilDate:(NSDate*)d { [self sleepForTimeInterval:[d timeIntervalSinceNow]]; }
+ (void)exit { pthread_exit(NULL); }
+ (double)threadPriority { return 0.5; }
+ (BOOL)setThreadPriority:(double)p { (void)p; return YES; }
+ (NSArray*)callStackSymbols { return [NSArray array]; }
+ (NSArray*)callStackReturnAddresses { return [NSArray array]; }
- (NSMutableDictionary*)threadDictionary {
    if (!_dict) _dict = [[NSMutableDictionary alloc] init];
    return _dict;
}
- (NSString*)name { return _name ? _name : @""; }
- (void)setName:(NSString*)n {
    [_name release];
    _name = [n copy];
    if ([NSThread currentThread] == self && n) pthread_setname_np([n UTF8String]);
}
- (double)threadPriority { return _prio; }
- (void)setThreadPriority:(double)p { _prio = p; }
- (NSUInteger)stackSize { return _stack; }
- (void)setStackSize:(NSUInteger)s { _stack = s; }
- (void)setQualityOfService:(NSInteger)q { (void)q; }
- (instancetype)initWithTarget:(id)t selector:(SEL)s object:(id)a {
    _target = [t retain];
    _sel = s;
    _arg = [a retain];
    return self;
}
- (instancetype)initWithBlock:(void (^)(void))b { _block = [b copy]; return self; }
static void* thread_main(void* p) {
    NSThread* t = p;
    pthread_once(&g_thread_once, thread_key_init);
    pthread_setspecific(g_thread_key, t);
    if (t->_name) pthread_setname_np([t->_name UTF8String]);
    void* pool = objc_autoreleasePoolPush();
    [t main];
    objc_autoreleasePoolPop(pool);
    return NULL;
}
- (void)main {
    if (_block) _block();
    else if (_target) objc_msgSend(_target, _sel, _arg);
}
- (void)start {
    [self retain];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (_stack) pthread_attr_setstacksize(&attr, _stack);
    pthread_t th;
    pthread_create(&th, &attr, thread_main, self);
    pthread_attr_destroy(&attr);
}
- (void)cancel {}
- (BOOL)isCancelled { return NO; }
- (BOOL)isExecuting { return YES; }
- (BOOL)isFinished { return NO; }
+ (void)detachNewThreadSelector:(SEL)s toTarget:(id)t withObject:(id)a {
    NSThread* th = [[[NSThread alloc] initWithTarget:t selector:s object:a] autorelease];
    [th start];
}
+ (void)detachNewThreadWithBlock:(void (^)(void))b {
    NSThread* th = [[[NSThread alloc] initWithBlock:b] autorelease];
    [th start];
}
@end

// ===================================================================== NSOperation / NSOperationQueue
@interface NSOperation : NSObject {
@public
    BOOL _cancelled, _finished, _executing;
    void (^_completion)(void);
    NSInteger _prio;
    NSMutableArray* _deps;
    NSString* _name;
}
@end
@implementation NSOperation
- (void)dealloc { [_completion release]; [_deps release]; [_name release]; [super dealloc]; }
- (void)start {
    if (_cancelled) { _finished = YES; if (_completion) _completion(); return; }
    _executing = YES;
    void* pool = objc_autoreleasePoolPush();
    [self main];
    objc_autoreleasePoolPop(pool);
    _executing = NO;
    _finished = YES;
    if (_completion) _completion();
}
- (void)main {}
- (void)cancel { _cancelled = YES; }
- (BOOL)isCancelled { return _cancelled; }
- (BOOL)isFinished { return _finished; }
- (BOOL)isExecuting { return _executing; }
- (BOOL)isReady { return YES; }
- (BOOL)isConcurrent { return NO; }
- (BOOL)isAsynchronous { return NO; }
- (void (^)(void))completionBlock { return _completion; }
- (void)setCompletionBlock:(void (^)(void))b { [_completion release]; _completion = [b copy]; }
- (NSInteger)queuePriority { return _prio; }
- (void)setQueuePriority:(NSInteger)p { _prio = p; }
- (void)setQualityOfService:(NSInteger)q { (void)q; }
- (void)setThreadPriority:(double)p { (void)p; }
- (void)addDependency:(NSOperation*)o {
    if (!_deps) _deps = [[NSMutableArray alloc] init];
    [_deps addObject:o];
}
- (void)removeDependency:(NSOperation*)o { [_deps removeObject:o]; }
- (NSArray*)dependencies { return _deps ? _deps : [NSArray array]; }
- (void)waitUntilFinished { while (!_finished) usleep(1000); }
- (NSString*)name { return _name; }
- (void)setName:(NSString*)n { [_name release]; _name = [n copy]; }
@end
@interface NSBlockOperation : NSOperation {
    NSMutableArray* _blocks;
}
@end
@implementation NSBlockOperation
+ (instancetype)blockOperationWithBlock:(void (^)(void))b {
    NSBlockOperation* o = [[[self alloc] init] autorelease];
    [o addExecutionBlock:b];
    return o;
}
- (void)addExecutionBlock:(void (^)(void))b {
    if (!_blocks) _blocks = [[NSMutableArray alloc] init];
    id c = [b copy];
    [_blocks addObject:c];
    [c release];
}
- (NSArray*)executionBlocks { return _blocks; }
- (void)dealloc { [_blocks release]; [super dealloc]; }
- (void)main {
    for (id b in _blocks) ((void (^)(void))b)();
}
@end

@interface NSOperationQueue : NSObject {
    dispatch_queue_t _q;
    NSMutableArray* _ops;
    NSString* _name;
    NSInteger _max;
    BOOL _main, _suspended;
    pthread_mutex_t _lock;
}
@end
@implementation NSOperationQueue
+ (NSOperationQueue*)mainQueue {
    static NSOperationQueue* m;
    if (!m) {
        m = [[NSOperationQueue alloc] init];
        m->_main = YES;
        m->_q = dispatch_get_main_queue();
        m->_max = 1;
    }
    return m;
}
+ (NSOperationQueue*)currentQueue { return pthread_main_np() ? [self mainQueue] : nil; }
- (instancetype)init {
    pthread_mutex_init(&_lock, NULL);
    _ops = [[NSMutableArray alloc] init];
    _q = dispatch_queue_create("NSOperationQueue", DISPATCH_QUEUE_SERIAL);
    _max = -1;
    return self;
}
- (void)dealloc { [_ops release]; [_name release]; [super dealloc]; }
- (void)addOperation:(NSOperation*)op {
    pthread_mutex_lock(&_lock);
    [_ops addObject:op];
    pthread_mutex_unlock(&_lock);
    [op retain];
    dispatch_async(_q, ^{
        for (NSOperation* d in [op dependencies]) [d waitUntilFinished];
        [op start];
        pthread_mutex_lock(&self->_lock);
        [self->_ops removeObjectIdenticalTo:op];
        pthread_mutex_unlock(&self->_lock);
        [op release];
    });
}
- (void)addOperations:(NSArray*)ops waitUntilFinished:(BOOL)w {
    for (NSOperation* o in ops) [self addOperation:o];
    if (w) [self waitUntilAllOperationsAreFinished];
}
- (void)addOperationWithBlock:(void (^)(void))b { [self addOperation:[NSBlockOperation blockOperationWithBlock:b]]; }
- (NSArray*)operations {
    pthread_mutex_lock(&_lock);
    NSArray* r = [NSArray arrayWithArray:_ops];
    pthread_mutex_unlock(&_lock);
    return r;
}
- (NSUInteger)operationCount { return [[self operations] count]; }
- (void)cancelAllOperations { for (NSOperation* o in [self operations]) [o cancel]; }
- (void)waitUntilAllOperationsAreFinished {
    if (_main && pthread_main_np()) return;
    dispatch_sync(_q, ^{});
}
- (NSInteger)maxConcurrentOperationCount { return _max; }
- (void)setMaxConcurrentOperationCount:(NSInteger)m { _max = m; }
- (void)setName:(NSString*)n { [_name release]; _name = [n copy]; }
- (NSString*)name { return _name; }
- (void)setSuspended:(BOOL)s { _suspended = s; }
- (BOOL)isSuspended { return _suspended; }
- (void)setQualityOfService:(NSInteger)q { (void)q; }
- (void)setUnderlyingQueue:(dispatch_queue_t)q { (void)q; }
@end
