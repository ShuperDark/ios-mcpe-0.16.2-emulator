// NSValue, NSNumber, NSNull, NSDate, NSData, NSMutableData, NSUUID.
#import <emu/Foundation.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// ===================================================================== NSValue
@interface NSValue () {
    char* _type;
    void* _bytes;
    NSUInteger _size;
}
@end

static NSUInteger type_size(const char* t) {
    switch (*t) {
    case 'c': case 'C': case 'B': return 1;
    case 's': case 'S': return 2;
    case 'i': case 'I': case 'f': return 4;
    case 'l': case 'L': case 'q': case 'Q': case 'd': case '@': case '#': case ':': case '^': case '*': return 8;
    case '{': {
        // count doubles / longs: good enough for CG/NS geometry structs
        if (!strncmp(t, "{CGRect", 7) || !strncmp(t, "{UIEdgeInsets", 13)) return 32;
        if (!strncmp(t, "{CGPoint", 8) || !strncmp(t, "{CGSize", 7) || !strncmp(t, "{_NSRange", 9) || !strncmp(t, "{NSRange", 8)) return 16;
        if (!strncmp(t, "{CGAffineTransform", 18)) return 48;
        return 64;
    }
    default: return 8;
    }
}

@implementation NSValue
+ (NSValue*)valueWithBytes:(const void*)v objCType:(const char*)t { return [[[self alloc] initWithBytes:v objCType:t] autorelease]; }
+ (NSValue*)value:(const void*)v withObjCType:(const char*)t { return [self valueWithBytes:v objCType:t]; }
+ (NSValue*)valueWithPointer:(const void*)p { return [self valueWithBytes:&p objCType:"^v"]; }
+ (NSValue*)valueWithNonretainedObject:(id)o { return [self valueWithBytes:&o objCType:"@"]; }
+ (NSValue*)valueWithRange:(NSRange)r { return [self valueWithBytes:&r objCType:"{_NSRange=QQ}"]; }
+ (NSValue*)valueWithCGRect:(CGRect)r { return [self valueWithBytes:&r objCType:"{CGRect={CGPoint=dd}{CGSize=dd}}"]; }
+ (NSValue*)valueWithCGPoint:(CGPoint)p { return [self valueWithBytes:&p objCType:"{CGPoint=dd}"]; }
+ (NSValue*)valueWithCGSize:(CGSize)s { return [self valueWithBytes:&s objCType:"{CGSize=dd}"]; }
+ (NSValue*)valueWithCGAffineTransform:(CGAffineTransform)t { return [self valueWithBytes:&t objCType:"{CGAffineTransform=dddddd}"]; }
+ (NSValue*)valueWithUIEdgeInsets:(UIEdgeInsets)e { return [self valueWithBytes:&e objCType:"{UIEdgeInsets=dddd}"]; }
- (instancetype)initWithBytes:(const void*)v objCType:(const char*)t {
    _type = strdup(t ? t : "?");
    _size = type_size(_type);
    _bytes = calloc(1, _size);
    if (v) memcpy(_bytes, v, _size);
    return self;
}
- (void)dealloc { free(_type); free(_bytes); [super dealloc]; }
- (id)copyWithZone:(NSZone*)z { (void)z; return [self retain]; }
- (void)getValue:(void*)b { memcpy(b, _bytes, _size); }
- (void)getValue:(void*)b size:(NSUInteger)n { memcpy(b, _bytes, n < _size ? n : _size); }
- (const char*)objCType { return _type; }
- (void*)pointerValue { return *(void**)_bytes; }
- (id)nonretainedObjectValue { return *(id*)_bytes; }
- (NSRange)rangeValue { NSRange r; memcpy(&r, _bytes, sizeof r); return r; }
- (CGRect)CGRectValue { CGRect r = {{0, 0}, {0, 0}}; memcpy(&r, _bytes, _size < sizeof r ? _size : sizeof r); return r; }
- (CGPoint)CGPointValue { CGPoint r = {0, 0}; memcpy(&r, _bytes, _size < sizeof r ? _size : sizeof r); return r; }
- (CGSize)CGSizeValue { CGSize r = {0, 0}; memcpy(&r, _bytes, _size < sizeof r ? _size : sizeof r); return r; }
- (UIEdgeInsets)UIEdgeInsetsValue { UIEdgeInsets r = {0, 0, 0, 0}; memcpy(&r, _bytes, _size < sizeof r ? _size : sizeof r); return r; }
- (CGAffineTransform)CGAffineTransformValue { CGAffineTransform r = {1, 0, 0, 1, 0, 0}; memcpy(&r, _bytes, _size < sizeof r ? _size : sizeof r); return r; }
- (BOOL)isEqualToValue:(NSValue*)o {
    return [o isKindOfClass:[NSValue class]] && !strcmp(_type, [o objCType]) && _size == o->_size && !memcmp(_bytes, o->_bytes, _size);
}
- (BOOL)isEqual:(id)o { return o == self || ([o isKindOfClass:[NSValue class]] && [self isEqualToValue:o]); }
- (NSUInteger)hash { return _size ? *(NSUInteger*)_bytes : 0; }
- (NSString*)description { return [NSString stringWithFormat:@"<NSValue %s>", _type]; }
@end

// ===================================================================== NSNumber
@interface NSNumber () {
    char _ntype;  // 'B' bool, 'q' signed, 'Q' unsigned, 'd' double, 'f' float, plus originals c,i,s,...
    union {
        long long i;
        unsigned long long u;
        double d;
    } _v;
}
@end

static NSNumber* g_yes;
static NSNumber* g_no;

@implementation NSNumber
static inline NSNumber* mk(char t) {
    NSNumber* n = [NSNumber alloc];
    n->_ntype = t;
    return n;
}
+ (NSNumber*)numberWithBool:(BOOL)v {
    if (!g_yes) {
        g_yes = mk('c'); g_yes->_v.i = 1;
        g_no = mk('c'); g_no->_v.i = 0;
    }
    return v ? g_yes : g_no;
}
+ (NSNumber*)numberWithChar:(char)v { NSNumber* n = mk('c'); n->_v.i = v; return [n autorelease]; }
+ (NSNumber*)numberWithUnsignedChar:(unsigned char)v { NSNumber* n = mk('C'); n->_v.u = v; return [n autorelease]; }
+ (NSNumber*)numberWithShort:(short)v { NSNumber* n = mk('s'); n->_v.i = v; return [n autorelease]; }
+ (NSNumber*)numberWithUnsignedShort:(unsigned short)v { NSNumber* n = mk('S'); n->_v.u = v; return [n autorelease]; }
+ (NSNumber*)numberWithInt:(int)v { NSNumber* n = mk('i'); n->_v.i = v; return [n autorelease]; }
+ (NSNumber*)numberWithUnsignedInt:(unsigned)v { NSNumber* n = mk('I'); n->_v.u = v; return [n autorelease]; }
+ (NSNumber*)numberWithInteger:(NSInteger)v { NSNumber* n = mk('q'); n->_v.i = v; return [n autorelease]; }
+ (NSNumber*)numberWithUnsignedInteger:(NSUInteger)v { NSNumber* n = mk('Q'); n->_v.u = v; return [n autorelease]; }
+ (NSNumber*)numberWithLong:(long)v { NSNumber* n = mk('q'); n->_v.i = v; return [n autorelease]; }
+ (NSNumber*)numberWithUnsignedLong:(unsigned long)v { NSNumber* n = mk('Q'); n->_v.u = v; return [n autorelease]; }
+ (NSNumber*)numberWithLongLong:(long long)v { NSNumber* n = mk('q'); n->_v.i = v; return [n autorelease]; }
+ (NSNumber*)numberWithUnsignedLongLong:(unsigned long long)v { NSNumber* n = mk('Q'); n->_v.u = v; return [n autorelease]; }
+ (NSNumber*)numberWithFloat:(float)v { NSNumber* n = mk('f'); n->_v.d = v; return [n autorelease]; }
+ (NSNumber*)numberWithDouble:(double)v { NSNumber* n = mk('d'); n->_v.d = v; return [n autorelease]; }
- (instancetype)initWithBool:(BOOL)v { [self release]; return [[NSNumber numberWithBool:v] retain]; }
- (instancetype)initWithInt:(int)v { _ntype = 'i'; _v.i = v; return self; }
- (instancetype)initWithInteger:(NSInteger)v { _ntype = 'q'; _v.i = v; return self; }
- (instancetype)initWithLongLong:(long long)v { _ntype = 'q'; _v.i = v; return self; }
- (instancetype)initWithUnsignedLongLong:(unsigned long long)v { _ntype = 'Q'; _v.u = v; return self; }
- (instancetype)initWithUnsignedInteger:(NSUInteger)v { _ntype = 'Q'; _v.u = v; return self; }
- (instancetype)initWithDouble:(double)v { _ntype = 'd'; _v.d = v; return self; }
- (instancetype)initWithFloat:(float)v { _ntype = 'f'; _v.d = v; return self; }
- (id)retain { if (self == g_yes || self == g_no) return self; return [super retain]; }
- (oneway void)release { if (self == g_yes || self == g_no) return; [super release]; }
- (id)autorelease { if (self == g_yes || self == g_no) return self; return [super autorelease]; }
- (id)copyWithZone:(NSZone*)z { (void)z; return [self retain]; }
- (BOOL)_isFloat { return _ntype == 'd' || _ntype == 'f'; }
- (BOOL)_isUnsigned { return _ntype == 'Q' || _ntype == 'I' || _ntype == 'S' || _ntype == 'C'; }
- (double)doubleValue { return [self _isFloat] ? _v.d : [self _isUnsigned] ? (double)_v.u : (double)_v.i; }
- (float)floatValue { return (float)[self doubleValue]; }
- (long long)longLongValue { return [self _isFloat] ? (long long)_v.d : _v.i; }
- (unsigned long long)unsignedLongLongValue { return [self _isFloat] ? (unsigned long long)_v.d : _v.u; }
- (NSInteger)integerValue { return (NSInteger)[self longLongValue]; }
- (NSUInteger)unsignedIntegerValue { return (NSUInteger)[self unsignedLongLongValue]; }
- (long)longValue { return (long)[self longLongValue]; }
- (unsigned long)unsignedLongValue { return (unsigned long)[self unsignedLongLongValue]; }
- (int)intValue { return (int)[self longLongValue]; }
- (unsigned)unsignedIntValue { return (unsigned)[self unsignedLongLongValue]; }
- (short)shortValue { return (short)[self longLongValue]; }
- (unsigned short)unsignedShortValue { return (unsigned short)[self longLongValue]; }
- (char)charValue { return (char)[self longLongValue]; }
- (unsigned char)unsignedCharValue { return (unsigned char)[self longLongValue]; }
- (BOOL)boolValue { return [self _isFloat] ? _v.d != 0 : _v.i != 0; }
- (const char*)objCType {
    static char types[][2] = {"c", "C", "s", "S", "i", "I", "q", "Q", "f", "d"};
    const char* order = "cCsSiIqQfd";
    const char* p = strchr(order, _ntype);
    return p ? types[p - order] : "q";
}
- (void)getValue:(void*)b {
    switch (_ntype) {
    case 'c': case 'C': *(char*)b = (char)_v.i; break;
    case 's': case 'S': *(short*)b = (short)_v.i; break;
    case 'i': case 'I': *(int*)b = (int)_v.i; break;
    case 'f': *(float*)b = (float)_v.d; break;
    case 'd': *(double*)b = _v.d; break;
    default: *(long long*)b = _v.i; break;
    }
}
- (NSString*)stringValue {
    if ([self _isFloat]) return [NSString stringWithFormat:@"%.17g", _v.d];
    if ([self _isUnsigned]) return [NSString stringWithFormat:@"%llu", _v.u];
    return [NSString stringWithFormat:@"%lld", _v.i];
}
- (NSString*)description { return [self stringValue]; }
- (NSString*)descriptionWithLocale:(id)l { (void)l; return [self stringValue]; }
- (NSComparisonResult)compare:(NSNumber*)o {
    if ([self _isFloat] || [o _isFloat]) {
        double a = [self doubleValue], b = [o doubleValue];
        return a < b ? NSOrderedAscending : a > b ? NSOrderedDescending : NSOrderedSame;
    }
    long long a = [self longLongValue], b = [o longLongValue];
    return a < b ? NSOrderedAscending : a > b ? NSOrderedDescending : NSOrderedSame;
}
- (BOOL)isEqualToNumber:(NSNumber*)o { return o && [self compare:o] == NSOrderedSame; }
- (BOOL)isEqual:(id)o { return o == self || ([o isKindOfClass:[NSNumber class]] && [self isEqualToNumber:o]); }
- (NSUInteger)hash { return [self _isFloat] ? (NSUInteger)(long long)_v.d : (NSUInteger)_v.i; }
@end

// ===================================================================== NSNull
@implementation NSNull
+ (NSNull*)null {
    static NSNull* n;
    if (!n) n = [[NSNull alloc] init];
    return n;
}
- (id)retain { return self; }
- (oneway void)release {}
- (id)autorelease { return self; }
- (id)copyWithZone:(NSZone*)z { (void)z; return self; }
- (NSString*)description { return @"<null>"; }
@end

// ===================================================================== NSDate
#define REF_1970 978307200.0
NSTimeInterval _emu_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}
static NSTimeInterval wall_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9 - REF_1970;
}
@interface NSDate () {
    NSTimeInterval _t;
}
@end
@implementation NSDate
+ (instancetype)date { return [[[self alloc] initWithTimeIntervalSinceReferenceDate:wall_now()] autorelease]; }
+ (instancetype)now { return [self date]; }
+ (instancetype)dateWithTimeIntervalSinceNow:(NSTimeInterval)t { return [[[self alloc] initWithTimeIntervalSinceReferenceDate:wall_now() + t] autorelease]; }
+ (instancetype)dateWithTimeIntervalSince1970:(NSTimeInterval)t { return [[[self alloc] initWithTimeIntervalSinceReferenceDate:t - REF_1970] autorelease]; }
+ (instancetype)dateWithTimeIntervalSinceReferenceDate:(NSTimeInterval)t { return [[[self alloc] initWithTimeIntervalSinceReferenceDate:t] autorelease]; }
+ (instancetype)dateWithTimeInterval:(NSTimeInterval)t sinceDate:(NSDate*)d { return [self dateWithTimeIntervalSinceReferenceDate:d->_t + t]; }
+ (instancetype)distantFuture { return [self dateWithTimeIntervalSinceReferenceDate:63113904000.0]; }
+ (instancetype)distantPast { return [self dateWithTimeIntervalSinceReferenceDate:-63114076800.0]; }
+ (NSTimeInterval)timeIntervalSinceReferenceDate { return wall_now(); }
- (instancetype)init { _t = wall_now(); return self; }
- (instancetype)initWithTimeIntervalSinceReferenceDate:(NSTimeInterval)t { _t = t; return self; }
- (instancetype)initWithTimeIntervalSinceNow:(NSTimeInterval)t { _t = wall_now() + t; return self; }
- (instancetype)initWithTimeIntervalSince1970:(NSTimeInterval)t { _t = t - REF_1970; return self; }
- (instancetype)initWithTimeInterval:(NSTimeInterval)t sinceDate:(NSDate*)d { _t = d->_t + t; return self; }
- (id)copyWithZone:(NSZone*)z { (void)z; return [self retain]; }
- (NSTimeInterval)timeIntervalSinceReferenceDate { return _t; }
- (NSTimeInterval)timeIntervalSince1970 { return _t + REF_1970; }
- (NSTimeInterval)timeIntervalSinceNow { return _t - wall_now(); }
- (NSTimeInterval)timeIntervalSinceDate:(NSDate*)d { return _t - d->_t; }
- (NSDate*)dateByAddingTimeInterval:(NSTimeInterval)t { return [NSDate dateWithTimeIntervalSinceReferenceDate:_t + t]; }
- (NSComparisonResult)compare:(NSDate*)d { return _t < d->_t ? NSOrderedAscending : _t > d->_t ? NSOrderedDescending : NSOrderedSame; }
- (BOOL)isEqualToDate:(NSDate*)d { return d && d->_t == _t; }
- (BOOL)isEqual:(id)o { return [o isKindOfClass:[NSDate class]] && [self isEqualToDate:o]; }
- (NSUInteger)hash { return (NSUInteger)(long long)_t; }
- (NSDate*)earlierDate:(NSDate*)d { return _t <= d->_t ? self : d; }
- (NSDate*)laterDate:(NSDate*)d { return _t >= d->_t ? self : d; }
- (NSString*)description {
    time_t t = (time_t)(_t + REF_1970);
    struct tm tm;
    gmtime_r(&t, &tm);
    char buf[64];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S +0000", &tm);
    return [NSString stringWithUTF8String:buf];
}
@end

// ===================================================================== NSData
@interface NSData () {
@public
    unsigned char* _bytes;
    NSUInteger _len, _cap;
    BOOL _freeWhenDone;
    BOOL _mutable;
}
@end
@implementation NSData
+ (id)allocWithZone:(NSZone*)z {
    NSData* d = [super allocWithZone:z];
    d->_mutable = [self isSubclassOfClass:[NSMutableData class]];
    return d;
}
+ (instancetype)data { return [[[self alloc] init] autorelease]; }
+ (instancetype)dataWithBytes:(const void*)b length:(NSUInteger)n { return [[[self alloc] initWithBytes:b length:n] autorelease]; }
+ (instancetype)dataWithBytesNoCopy:(void*)b length:(NSUInteger)n { return [[[self alloc] initWithBytesNoCopy:b length:n freeWhenDone:YES] autorelease]; }
+ (instancetype)dataWithBytesNoCopy:(void*)b length:(NSUInteger)n freeWhenDone:(BOOL)f { return [[[self alloc] initWithBytesNoCopy:b length:n freeWhenDone:f] autorelease]; }
+ (instancetype)dataWithContentsOfFile:(NSString*)p { return [[[self alloc] initWithContentsOfFile:p] autorelease]; }
+ (instancetype)dataWithContentsOfFile:(NSString*)p options:(NSUInteger)o error:(NSError**)e {
    (void)o;
    id d = [self dataWithContentsOfFile:p];
    if (!d && e) *e = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:260 userInfo:nil];
    return d;
}
+ (instancetype)dataWithContentsOfURL:(NSURL*)u {
    id p = [(id)u performSelector:@selector(path)];
    return p ? [self dataWithContentsOfFile:p] : nil;
}
+ (instancetype)dataWithContentsOfURL:(NSURL*)u options:(NSUInteger)o error:(NSError**)e {
    (void)o;
    id d = [self dataWithContentsOfURL:u];
    if (!d && e) *e = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:260 userInfo:nil];
    return d;
}
+ (instancetype)dataWithData:(NSData*)d { return [[[self alloc] initWithData:d] autorelease]; }
- (instancetype)init { return [self initWithBytes:NULL length:0]; }
- (instancetype)initWithBytes:(const void*)b length:(NSUInteger)n {
    _cap = n ? n : 16;
    _bytes = malloc(_cap);
    if (b && n) memcpy(_bytes, b, n);
    _len = n;
    _freeWhenDone = YES;
    return self;
}
- (instancetype)initWithBytesNoCopy:(void*)b length:(NSUInteger)n { return [self initWithBytesNoCopy:b length:n freeWhenDone:YES]; }
- (instancetype)initWithBytesNoCopy:(void*)b length:(NSUInteger)n freeWhenDone:(BOOL)f {
    if (_mutable) {
        [self initWithBytes:b length:n];
        if (f) free(b);
        return self;
    }
    _bytes = b;
    _len = _cap = n;
    _freeWhenDone = f;
    return self;
}
- (instancetype)initWithBytesNoCopy:(void*)b length:(NSUInteger)n deallocator:(void (^)(void*, NSUInteger))d {
    [self initWithBytes:b length:n];
    if (d) d(b, n);
    return self;
}
- (instancetype)initWithData:(NSData*)d { return [self initWithBytes:[d bytes] length:[d length]]; }
- (instancetype)initWithContentsOfFile:(NSString*)p {
    int fd = open([p fileSystemRepresentation], O_RDONLY);
    if (fd < 0) { [self release]; return nil; }
    struct stat st;
    if (fstat(fd, &st) != 0 || (st.st_mode & S_IFDIR)) { close(fd); [self release]; return nil; }
    NSUInteger n = (NSUInteger)st.st_size;
    [self initWithBytes:NULL length:0];
    if (n > _cap) { _cap = n; _bytes = realloc(_bytes, _cap); }
    NSUInteger got = 0;
    while (got < n) {
        ssize_t r = read(fd, _bytes + got, n - got);
        if (r <= 0) break;
        got += (NSUInteger)r;
    }
    _len = got;
    close(fd);
    return self;
}
- (instancetype)initWithContentsOfURL:(NSURL*)u { return [self initWithContentsOfFile:(NSString*)[(id)u performSelector:@selector(path)]]; }
- (void)dealloc {
    if (_freeWhenDone) free(_bytes);
    [super dealloc];
}
- (id)copyWithZone:(NSZone*)z { (void)z; return _mutable ? [[NSData alloc] initWithBytes:_bytes length:_len] : [self retain]; }
- (id)mutableCopyWithZone:(NSZone*)z { (void)z; return [[NSMutableData alloc] initWithBytes:_bytes length:_len]; }
- (const void*)bytes { return _bytes; }
- (NSUInteger)length { return _len; }
- (void)getBytes:(void*)b length:(NSUInteger)n { memcpy(b, _bytes, n < _len ? n : _len); }
- (void)getBytes:(void*)b range:(NSRange)r { if (NSMaxRange(r) <= _len) memcpy(b, _bytes + r.location, r.length); }
- (void)getBytes:(void*)b { memcpy(b, _bytes, _len); }
- (NSData*)subdataWithRange:(NSRange)r {
    if (NSMaxRange(r) > _len) return nil;
    return [NSData dataWithBytes:_bytes + r.location length:r.length];
}
- (NSRange)rangeOfData:(NSData*)d options:(NSUInteger)o range:(NSRange)r {
    (void)o;
    NSUInteger n = [d length];
    const unsigned char* p = [d bytes];
    for (NSUInteger i = r.location; n && i + n <= NSMaxRange(r); i++)
        if (!memcmp(_bytes + i, p, n)) return NSMakeRange(i, n);
    return NSMakeRange(NSNotFound, 0);
}
- (BOOL)isEqualToData:(NSData*)d { return d && [d length] == _len && !memcmp([d bytes], _bytes, _len); }
- (BOOL)isEqual:(id)o { return [o isKindOfClass:[NSData class]] && [self isEqualToData:o]; }
- (NSUInteger)hash {
    uint64_t h = 1469598103934665603ull;
    for (NSUInteger i = 0; i < _len && i < 80; i++) { h ^= _bytes[i]; h *= 1099511628211ull; }
    return (NSUInteger)h;
}
- (BOOL)writeToFile:(NSString*)p atomically:(BOOL)a {
    NSString* target = a ? [p stringByAppendingString:@".tmp~"] : p;
    int fd = open([target fileSystemRepresentation], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return NO;
    NSUInteger put = 0;
    while (put < _len) {
        ssize_t r = write(fd, _bytes + put, _len - put);
        if (r <= 0) break;
        put += (NSUInteger)r;
    }
    close(fd);
    if (put != _len) return NO;
    if (a && rename([target fileSystemRepresentation], [p fileSystemRepresentation]) != 0) return NO;
    return YES;
}
- (BOOL)writeToFile:(NSString*)p options:(NSUInteger)o error:(NSError**)e {
    BOOL ok = [self writeToFile:p atomically:(o & 1) != 0];
    if (!ok && e) *e = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:512 userInfo:nil];
    return ok;
}
- (BOOL)writeToURL:(NSURL*)u atomically:(BOOL)a { return [self writeToFile:(NSString*)[(id)u performSelector:@selector(path)] atomically:a]; }
- (BOOL)writeToURL:(NSURL*)u options:(NSUInteger)o error:(NSError**)e { return [self writeToFile:(NSString*)[(id)u performSelector:@selector(path)] options:o error:e]; }
- (NSString*)description {
    NSMutableString* s = [NSMutableString stringWithString:@"<"];
    for (NSUInteger i = 0; i < _len && i < 64; i++) {
        [s appendFormat:@"%02x", _bytes[i]];
        if ((i & 3) == 3 && i + 1 < _len) [s appendString:@" "];
    }
    [s appendString:@">"];
    return s;
}
// base64
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
- (NSString*)base64EncodedStringWithOptions:(NSUInteger)o {
    (void)o;
    NSUInteger n = _len, outn = (n + 2) / 3 * 4;
    char* out = malloc(outn + 1);
    NSUInteger k = 0;
    for (NSUInteger i = 0; i < n; i += 3) {
        unsigned v = (unsigned)_bytes[i] << 16 | (i + 1 < n ? (unsigned)_bytes[i + 1] << 8 : 0) | (i + 2 < n ? _bytes[i + 2] : 0);
        out[k++] = b64[(v >> 18) & 63];
        out[k++] = b64[(v >> 12) & 63];
        out[k++] = i + 1 < n ? b64[(v >> 6) & 63] : '=';
        out[k++] = i + 2 < n ? b64[v & 63] : '=';
    }
    out[k] = 0;
    NSString* r = [NSString stringWithUTF8String:out];
    free(out);
    return r;
}
- (NSString*)base64Encoding { return [self base64EncodedStringWithOptions:0]; }
- (NSData*)base64EncodedDataWithOptions:(NSUInteger)o { return [[self base64EncodedStringWithOptions:o] dataUsingEncoding:NSUTF8StringEncoding]; }
static int b64v(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
- (instancetype)initWithBase64EncodedString:(NSString*)s options:(NSUInteger)o {
    (void)o;
    const char* p = [s UTF8String];
    size_t n = strlen(p);
    unsigned char* out = malloc(n / 4 * 3 + 4);
    size_t k = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int v = b64v(p[i]);
        if (v < 0) continue;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out[k++] = (unsigned char)(acc >> bits); }
    }
    [self initWithBytes:out length:k];
    free(out);
    return self;
}
- (instancetype)initWithBase64EncodedData:(NSData*)d options:(NSUInteger)o {
    NSString* s = [[[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] autorelease];
    return [self initWithBase64EncodedString:s options:o];
}
- (instancetype)initWithBase64Encoding:(NSString*)s { return [self initWithBase64EncodedString:s options:0]; }
@end

@implementation NSMutableData
+ (instancetype)dataWithCapacity:(NSUInteger)n { return [[[self alloc] initWithCapacity:n] autorelease]; }
+ (instancetype)dataWithLength:(NSUInteger)n { return [[[self alloc] initWithLength:n] autorelease]; }
- (instancetype)initWithCapacity:(NSUInteger)n {
    [self initWithBytes:NULL length:0];
    if (n > _cap) { _cap = n; _bytes = realloc(_bytes, _cap); }
    return self;
}
- (instancetype)initWithLength:(NSUInteger)n {
    [self initWithCapacity:n];
    memset(_bytes, 0, n);
    _len = n;
    return self;
}
static void dgrow(NSData* d, NSUInteger need) {
    if (need <= d->_cap) return;
    d->_cap = need * 2;
    d->_bytes = realloc(d->_bytes, d->_cap);
}
- (void*)mutableBytes { return _bytes; }
- (void)setLength:(NSUInteger)n {
    dgrow(self, n);
    if (n > _len) memset(_bytes + _len, 0, n - _len);
    _len = n;
}
- (void)increaseLengthBy:(NSUInteger)n { [self setLength:_len + n]; }
- (void)appendBytes:(const void*)b length:(NSUInteger)n {
    dgrow(self, _len + n);
    memcpy(_bytes + _len, b, n);
    _len += n;
}
- (void)appendData:(NSData*)d { [self appendBytes:[d bytes] length:[d length]]; }
- (void)replaceBytesInRange:(NSRange)r withBytes:(const void*)b {
    if (NSMaxRange(r) > _len) [self setLength:NSMaxRange(r)];
    memcpy(_bytes + r.location, b, r.length);
}
- (void)replaceBytesInRange:(NSRange)r withBytes:(const void*)b length:(NSUInteger)n {
    if (r.location > _len) return;
    if (NSMaxRange(r) > _len) r.length = _len - r.location;
    NSUInteger nl = _len - r.length + n;
    dgrow(self, nl);
    memmove(_bytes + r.location + n, _bytes + NSMaxRange(r), _len - NSMaxRange(r));
    memcpy(_bytes + r.location, b, n);
    _len = nl;
}
- (void)resetBytesInRange:(NSRange)r {
    if (NSMaxRange(r) > _len) [self setLength:NSMaxRange(r)];
    memset(_bytes + r.location, 0, r.length);
}
- (void)setData:(NSData*)d { _len = 0; [self appendData:d]; }
@end

// ===================================================================== NSUUID
@interface NSUUID : NSObject <NSCopying> {
    unsigned char _u[16];
}
@end
@implementation NSUUID
+ (instancetype)UUID { return [[[self alloc] init] autorelease]; }
- (instancetype)init {
    arc4random_buf(_u, 16);
    _u[6] = (_u[6] & 0x0f) | 0x40;
    _u[8] = (_u[8] & 0x3f) | 0x80;
    return self;
}
- (instancetype)initWithUUIDBytes:(const unsigned char*)b { memcpy(_u, b, 16); return self; }
- (instancetype)initWithUUIDString:(NSString*)s {
    const char* p = [s UTF8String];
    int k = 0;
    for (; *p && k < 16; p++) {
        if (*p == '-') continue;
        char h[3] = {p[0], p[1], 0};
        if (!p[1]) break;
        _u[k++] = (unsigned char)strtol(h, NULL, 16);
        p++;
    }
    if (k != 16) { [self release]; return nil; }
    return self;
}
- (id)copyWithZone:(NSZone*)z { (void)z; return [self retain]; }
- (void)getUUIDBytes:(unsigned char*)b { memcpy(b, _u, 16); }
- (NSString*)UUIDString {
    return [NSString stringWithFormat:@"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", _u[0], _u[1], _u[2],
                                      _u[3], _u[4], _u[5], _u[6], _u[7], _u[8], _u[9], _u[10], _u[11], _u[12], _u[13], _u[14], _u[15]];
}
- (NSString*)description { return [self UUIDString]; }
- (BOOL)isEqual:(id)o { return [o isKindOfClass:[NSUUID class]] && !memcmp(_u, ((NSUUID*)o)->_u, 16); }
- (NSUInteger)hash { return *(NSUInteger*)_u; }
@end
