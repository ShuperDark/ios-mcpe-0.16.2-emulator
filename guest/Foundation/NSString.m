// NSString / NSMutableString / constant strings / formatting / paths.
#import <emu/Foundation.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emu/msg.h>
extern Class object_getClass(id);
extern const char* class_getName(Class);

// ===================================================================== UTF helpers
static size_t utf8_to_utf16(const char* s, size_t n, unichar* out) {
    const unsigned char* p = (const unsigned char*)s;
    size_t i = 0, o = 0;
    while (i < n) {
        unsigned c = p[i];
        unsigned cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; extra = 1; }
        else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; extra = 2; }
        else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; extra = 3; }
        else { cp = 0xfffd; extra = 0; }
        i++;
        for (int k = 0; k < extra && i < n; k++, i++) cp = (cp << 6) | (p[i] & 0x3f);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            if (out) { out[o] = (unichar)(0xd800 + (cp >> 10)); out[o + 1] = (unichar)(0xdc00 + (cp & 0x3ff)); }
            o += 2;
        } else {
            if (out) out[o] = (unichar)cp;
            o++;
        }
    }
    return o;
}
static size_t utf16_to_utf8(const unichar* s, size_t n, char* out) {
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned cp = s[i];
        if (cp >= 0xd800 && cp < 0xdc00 && i + 1 < n && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000) {
            cp = 0x10000 + ((cp - 0xd800) << 10) + (s[i + 1] - 0xdc00);
            i++;
        }
        if (cp < 0x80) { if (out) out[o] = (char)cp; o += 1; }
        else if (cp < 0x800) { if (out) { out[o] = (char)(0xc0 | (cp >> 6)); out[o + 1] = (char)(0x80 | (cp & 0x3f)); } o += 2; }
        else if (cp < 0x10000) {
            if (out) { out[o] = (char)(0xe0 | (cp >> 12)); out[o + 1] = (char)(0x80 | ((cp >> 6) & 0x3f)); out[o + 2] = (char)(0x80 | (cp & 0x3f)); }
            o += 3;
        } else {
            if (out) {
                out[o] = (char)(0xf0 | (cp >> 18)); out[o + 1] = (char)(0x80 | ((cp >> 12) & 0x3f));
                out[o + 2] = (char)(0x80 | ((cp >> 6) & 0x3f)); out[o + 3] = (char)(0x80 | (cp & 0x3f));
            }
            o += 4;
        }
    }
    return o;
}

// ===================================================================== concrete classes
@interface __NSCFString : NSMutableString {
@public
    unichar* _buf;
    NSUInteger _len;
    NSUInteger _cap;
    char* _utf8;
    BOOL _mutable;
}
- (instancetype)_initWithChars:(const unichar*)c length:(NSUInteger)n mutable:(BOOL)m;
@end

// Layout of compiler-emitted constant strings: { isa, flags, ptr, length }
@interface __NSCFConstantString : NSString {
    unsigned int _flags;
    unsigned int _pad;
    const void* _ptr;
    long _clen;
}
@end

static Class g_strcls;
static inline __NSCFString* new_str(const unichar* c, NSUInteger n, BOOL m) {
    if (!g_strcls) g_strcls = [__NSCFString class];
    return [[__NSCFString alloc] _initWithChars:c length:n mutable:m];
}
static __NSCFString* new_str_utf8(const char* s, size_t n, BOOL m) {
    unichar stackbuf[256];
    size_t u = utf8_to_utf16(s, n, NULL);
    unichar* b = u <= 256 ? stackbuf : malloc(u * sizeof(unichar));
    utf8_to_utf16(s, n, b);
    __NSCFString* r = new_str(b, u, m);
    if (b != stackbuf) free(b);
    return r;
}
NSString* _emu_string_from_utf8(const char* s) {
    if (!s) return nil;
    return [new_str_utf8(s, strlen(s), NO) autorelease];
}

// (Re)initializes a freshly allocated string object in place, keeping its mutability.
static id reinit(id self, const unichar* c, NSUInteger n) {
    if (object_getClass(self) == [__NSCFString class]) {
        __NSCFString* x = (__NSCFString*)self;
        free(x->_buf);
        free(x->_utf8);
        x->_utf8 = NULL;
        return [x _initWithChars:c length:n mutable:x->_mutable];
    }
    [self release];
    return new_str(c, n, NO);
}
static id reinit_utf8(id self, const char* s, size_t n) {
    unichar stackbuf[256];
    size_t u = utf8_to_utf16(s, n, NULL);
    unichar* b = u <= 256 ? stackbuf : malloc(u * sizeof(unichar));
    utf8_to_utf16(s, n, b);
    id r = reinit(self, b, u);
    if (b != stackbuf) free(b);
    return r;
}

// Copies the whole string into a malloc'd UTF-16 buffer
static unichar* chars_of(NSString* s, NSUInteger* n) {
    NSUInteger l = [s length];
    unichar* b = malloc((l + 1) * sizeof(unichar));
    [s getCharacters:b range:NSMakeRange(0, l)];
    *n = l;
    return b;
}

// ===================================================================== NSString (abstract)
@implementation NSString

+ (id)allocWithZone:(NSZone*)z {
    if (self == [NSString class]) return [__NSCFString allocWithZone:z];
    if (self == [NSMutableString class]) {
        __NSCFString* s = [__NSCFString allocWithZone:z];
        s->_mutable = YES;
        return s;
    }
    return [super allocWithZone:z];
}
+ (id)alloc { return [self allocWithZone:NULL]; }
+ (instancetype)string { return [[[self alloc] init] autorelease]; }
+ (instancetype)stringWithString:(NSString*)s { return [[[self alloc] initWithString:s] autorelease]; }
+ (instancetype)stringWithUTF8String:(const char*)s { return s ? [[[self alloc] initWithUTF8String:s] autorelease] : nil; }
+ (instancetype)stringWithCString:(const char*)s encoding:(NSStringEncoding)e {
    if (!s) return nil;
    return [[[self alloc] initWithBytes:s length:strlen(s) encoding:e] autorelease];
}
+ (instancetype)stringWithCString:(const char*)s { return [self stringWithUTF8String:s]; }
+ (instancetype)stringWithCharacters:(const unichar*)c length:(NSUInteger)n {
    return [[[self alloc] initWithCharacters:c length:n] autorelease];
}
+ (instancetype)stringWithFormat:(NSString*)fmt, ... {
    va_list ap;
    va_start(ap, fmt);
    id r = [[[self alloc] initWithFormat:fmt arguments:ap] autorelease];
    va_end(ap);
    return r;
}
+ (instancetype)localizedStringWithFormat:(NSString*)fmt, ... {
    va_list ap;
    va_start(ap, fmt);
    id r = [[[self alloc] initWithFormat:fmt arguments:ap] autorelease];
    va_end(ap);
    return r;
}
+ (instancetype)stringWithContentsOfFile:(NSString*)p encoding:(NSStringEncoding)e error:(NSError**)err {
    NSData* d = [NSData dataWithContentsOfFile:p];
    if (!d) {
        if (err) *err = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:260 userInfo:nil];
        return nil;
    }
    return [[[self alloc] initWithData:d encoding:e] autorelease];
}
+ (instancetype)stringWithContentsOfFile:(NSString*)p usedEncoding:(NSStringEncoding*)e error:(NSError**)err {
    if (e) *e = NSUTF8StringEncoding;
    return [self stringWithContentsOfFile:p encoding:NSUTF8StringEncoding error:err];
}
+ (instancetype)stringWithContentsOfFile:(NSString*)p { return [self stringWithContentsOfFile:p encoding:NSUTF8StringEncoding error:NULL]; }
+ (instancetype)stringWithContentsOfURL:(NSURL*)u encoding:(NSStringEncoding)e error:(NSError**)err {
    return [self stringWithContentsOfFile:(NSString*)[(id)u performSelector:@selector(path)] encoding:e error:err];
}
+ (NSStringEncoding)defaultCStringEncoding { return NSUTF8StringEncoding; }
+ (const NSStringEncoding*)availableStringEncodings {
    static const NSStringEncoding e[] = {NSUTF8StringEncoding, NSASCIIStringEncoding, 0};
    return e;
}
+ (NSString*)pathWithComponents:(NSArray*)c {
    NSMutableString* r = [NSMutableString string];
    for (NSUInteger i = 0; i < [c count]; i++) {
        NSString* p = [c objectAtIndex:i];
        if (i > 0 && ![r hasSuffix:@"/"]) [r appendString:@"/"];
        [r appendString:p];
    }
    return r;
}

- (instancetype)init { return reinit(self, NULL, 0); }
- (instancetype)initWithString:(NSString*)s {
    NSUInteger n;
    unichar* b = chars_of(s, &n);
    id r = reinit(self, b, n);
    free(b);
    return r;
}
- (instancetype)initWithUTF8String:(const char*)s {
    if (!s) { [self release]; return nil; }
    return reinit_utf8(self, s, strlen(s));
}
- (instancetype)initWithCString:(const char*)s encoding:(NSStringEncoding)e {
    return s ? [self initWithBytes:s length:strlen(s) encoding:e] : ([self release], nil);
}
- (instancetype)initWithCharacters:(const unichar*)c length:(NSUInteger)n { return reinit(self, c, n); }
- (instancetype)initWithCharactersNoCopy:(unichar*)c length:(NSUInteger)n freeWhenDone:(BOOL)f {
    id r = [self initWithCharacters:c length:n];
    if (f) free(c);
    return r;
}
- (instancetype)initWithBytes:(const void*)b length:(NSUInteger)n encoding:(NSStringEncoding)e {
    if (!b && n) { [self release]; return nil; }
    if (e == NSUnicodeStringEncoding || e == NSUTF16LittleEndianStringEncoding || e == NSUTF16BigEndianStringEncoding) {
        const unichar* u = b;
        NSUInteger cnt = n / 2;
        if (cnt && (u[0] == 0xfeff)) { u++; cnt--; }
        if (e == NSUTF16BigEndianStringEncoding) {
            unichar* t = malloc(cnt * 2 + 2);
            for (NSUInteger i = 0; i < cnt; i++) t[i] = (unichar)((u[i] >> 8) | (u[i] << 8));
            id r = reinit(self, t, cnt);
            free(t);
            return r;
        }
        return reinit(self, u, cnt);
    }
    if (e == NSUTF32StringEncoding || e == NSUTF32LittleEndianStringEncoding) {
        const uint32_t* u = b;
        NSUInteger cnt = n / 4;
        unichar* t = malloc(cnt * 4 + 2);
        NSUInteger o = 0;
        for (NSUInteger i = 0; i < cnt; i++) {
            uint32_t cp = u[i];
            if (cp >= 0x10000) { cp -= 0x10000; t[o++] = (unichar)(0xd800 + (cp >> 10)); t[o++] = (unichar)(0xdc00 + (cp & 0x3ff)); }
            else t[o++] = (unichar)cp;
        }
        id r = reinit(self, t, o);
        free(t);
        return r;
    }
    if (e == NSISOLatin1StringEncoding || e == NSWindowsCP1252StringEncoding || e == NSMacOSRomanStringEncoding) {
        unichar* t = malloc(n * 2 + 2);
        for (NSUInteger i = 0; i < n; i++) t[i] = ((const unsigned char*)b)[i];
        id r = reinit(self, t, n);
        free(t);
        return r;
    }
    // UTF-8 / ASCII (strip BOM)
    const char* s = b;
    if (n >= 3 && (unsigned char)s[0] == 0xef && (unsigned char)s[1] == 0xbb && (unsigned char)s[2] == 0xbf) { s += 3; n -= 3; }
    return reinit_utf8(self, s, n);
}
- (instancetype)initWithBytesNoCopy:(void*)b length:(NSUInteger)n encoding:(NSStringEncoding)e freeWhenDone:(BOOL)f {
    id r = [self initWithBytes:b length:n encoding:e];
    if (f) free(b);
    return r;
}
- (instancetype)initWithData:(NSData*)d encoding:(NSStringEncoding)e {
    return [self initWithBytes:[d bytes] length:[d length] encoding:e];
}
- (instancetype)initWithFormat:(NSString*)fmt, ... {
    va_list ap;
    va_start(ap, fmt);
    id r = [self initWithFormat:fmt arguments:ap];
    va_end(ap);
    return r;
}
- (instancetype)initWithFormat:(NSString*)fmt locale:(id)loc, ... {
    (void)loc;
    va_list ap;
    va_start(ap, loc);
    id r = [self initWithFormat:fmt arguments:ap];
    va_end(ap);
    return r;
}
- (instancetype)initWithFormat:(NSString*)fmt locale:(id)loc arguments:(va_list)ap {
    (void)loc;
    return [self initWithFormat:fmt arguments:ap];
}

extern char* _emu_vformat(const char* fmt, va_list ap);
- (instancetype)initWithFormat:(NSString*)fmt arguments:(va_list)ap {
    if (!fmt) { [self release]; return nil; }
    char* s = _emu_vformat([fmt UTF8String], ap);
    id r = reinit_utf8(self, s, strlen(s));
    free(s);
    return r;
}

- (id)copyWithZone:(NSZone*)z { (void)z; return [self retain]; }
- (id)mutableCopyWithZone:(NSZone*)z {
    (void)z;
    NSUInteger n;
    unichar* b = chars_of(self, &n);
    id r = new_str(b, n, YES);
    free(b);
    return r;
}

// ---- primitives (overridden) ----
- (NSUInteger)length { return 0; }
- (unichar)characterAtIndex:(NSUInteger)i { (void)i; return 0; }
- (void)getCharacters:(unichar*)buf range:(NSRange)r {
    for (NSUInteger i = 0; i < r.length; i++) buf[i] = [self characterAtIndex:r.location + i];
}
- (void)getCharacters:(unichar*)buf { [self getCharacters:buf range:NSMakeRange(0, [self length])]; }

- (const char*)UTF8String {
    NSUInteger n;
    unichar* b = chars_of(self, &n);
    size_t l = utf16_to_utf8(b, n, NULL);
    NSMutableData* d = [NSMutableData dataWithLength:l + 1];
    utf16_to_utf8(b, n, [d mutableBytes]);
    free(b);
    return [d mutableBytes];
}
- (const char*)cString { return [self UTF8String]; }
- (const char*)cStringUsingEncoding:(NSStringEncoding)e {
    if (e == NSUnicodeStringEncoding || e == NSUTF16LittleEndianStringEncoding) {
        NSData* d = [self dataUsingEncoding:e];
        NSMutableData* m = [NSMutableData dataWithData:d];
        [m increaseLengthBy:2];
        return [m bytes];
    }
    return [self UTF8String];
}
- (const char*)fileSystemRepresentation { return [self UTF8String]; }
- (BOOL)getFileSystemRepresentation:(char*)buf maxLength:(NSUInteger)max { return [self getCString:buf maxLength:max encoding:NSUTF8StringEncoding]; }
- (BOOL)getCString:(char*)buf maxLength:(NSUInteger)max encoding:(NSStringEncoding)e {
    (void)e;
    const char* s = [self UTF8String];
    size_t l = strlen(s);
    if (l + 1 > max) return NO;
    memcpy(buf, s, l + 1);
    return YES;
}
- (NSUInteger)lengthOfBytesUsingEncoding:(NSStringEncoding)e {
    if (e == NSUnicodeStringEncoding || e == NSUTF16LittleEndianStringEncoding || e == NSUTF16BigEndianStringEncoding) return [self length] * 2;
    return strlen([self UTF8String]);
}
- (NSUInteger)maximumLengthOfBytesUsingEncoding:(NSStringEncoding)e { return [self length] * (e == NSUTF8StringEncoding ? 3 : 4); }
- (NSData*)dataUsingEncoding:(NSStringEncoding)e allowLossyConversion:(BOOL)l { (void)l; return [self dataUsingEncoding:e]; }
- (NSData*)dataUsingEncoding:(NSStringEncoding)e {
    if (e == NSUnicodeStringEncoding || e == NSUTF16LittleEndianStringEncoding) {
        NSUInteger n;
        unichar* b = chars_of(self, &n);
        NSData* d = [NSData dataWithBytes:b length:n * 2];
        free(b);
        return d;
    }
    if (e == NSUTF16BigEndianStringEncoding) {
        NSUInteger n;
        unichar* b = chars_of(self, &n);
        for (NSUInteger i = 0; i < n; i++) b[i] = (unichar)((b[i] >> 8) | (b[i] << 8));
        NSData* d = [NSData dataWithBytes:b length:n * 2];
        free(b);
        return d;
    }
    const char* s = [self UTF8String];
    return [NSData dataWithBytes:s length:strlen(s)];
}
- (BOOL)canBeConvertedToEncoding:(NSStringEncoding)e { (void)e; return YES; }
- (NSStringEncoding)fastestEncoding { return NSUTF8StringEncoding; }
- (NSStringEncoding)smallestEncoding { return NSUTF8StringEncoding; }

- (NSString*)description { return self; }
- (NSUInteger)hash {
    NSUInteger n = [self length];
    unichar tmp[64];
    NSUInteger k = n < 64 ? n : 64;
    [self getCharacters:tmp range:NSMakeRange(0, k)];
    uint64_t h = 1469598103934665603ull ^ n;
    for (NSUInteger i = 0; i < k; i++) { h ^= tmp[i]; h *= 1099511628211ull; }
    return (NSUInteger)h;
}
- (BOOL)isEqual:(id)o {
    if (o == self) return YES;
    if (!o || ![o isKindOfClass:[NSString class]]) return NO;
    return [self isEqualToString:o];
}
- (BOOL)isEqualToString:(NSString*)o {
    if (o == self) return YES;
    if (!o) return NO;
    NSUInteger n = [self length];
    if ([o length] != n) return NO;
    NSUInteger a, b;
    unichar* x = chars_of(self, &a);
    unichar* y = chars_of(o, &b);
    BOOL eq = memcmp(x, y, n * 2) == 0;
    free(x);
    free(y);
    return eq;
}

static unichar fold(unichar c) { return (c >= 'A' && c <= 'Z') ? (unichar)(c + 32) : c; }
- (NSComparisonResult)compare:(NSString*)o options:(NSUInteger)opt range:(NSRange)r {
    NSUInteger an, bn;
    unichar* a = chars_of(self, &an);
    unichar* b = chars_of(o, &bn);
    unichar* pa = a + r.location;
    an = r.length;
    NSComparisonResult res = NSOrderedSame;
    NSUInteger i = 0, j = 0;
    while (i < an && j < bn) {
        unichar ca = pa[i], cb = b[j];
        if ((opt & NSNumericSearch) && ca >= '0' && ca <= '9' && cb >= '0' && cb <= '9') {
            unsigned long long va = 0, vb = 0;
            while (i < an && pa[i] >= '0' && pa[i] <= '9') va = va * 10 + (pa[i++] - '0');
            while (j < bn && b[j] >= '0' && b[j] <= '9') vb = vb * 10 + (b[j++] - '0');
            if (va != vb) { res = va < vb ? NSOrderedAscending : NSOrderedDescending; goto done; }
            continue;
        }
        if (opt & NSCaseInsensitiveSearch) { ca = fold(ca); cb = fold(cb); }
        if (ca != cb) { res = ca < cb ? NSOrderedAscending : NSOrderedDescending; goto done; }
        i++; j++;
    }
    if (i < an) res = NSOrderedDescending;
    else if (j < bn) res = NSOrderedAscending;
done:
    free(a);
    free(b);
    return res;
}
- (NSComparisonResult)compare:(NSString*)o options:(NSUInteger)opt { return [self compare:o options:opt range:NSMakeRange(0, [self length])]; }
- (NSComparisonResult)compare:(NSString*)o { return [self compare:o options:0]; }
- (NSComparisonResult)caseInsensitiveCompare:(NSString*)o { return [self compare:o options:NSCaseInsensitiveSearch]; }
- (NSComparisonResult)localizedCompare:(NSString*)o { return [self compare:o options:0]; }
- (NSComparisonResult)localizedCaseInsensitiveCompare:(NSString*)o { return [self compare:o options:NSCaseInsensitiveSearch]; }
- (NSComparisonResult)localizedStandardCompare:(NSString*)o { return [self compare:o options:NSCaseInsensitiveSearch | NSNumericSearch]; }

- (NSRange)rangeOfString:(NSString*)s options:(NSUInteger)opt range:(NSRange)r {
    NSUInteger hn, nn;
    if (!s || ![s length]) return NSMakeRange(NSNotFound, 0);
    unichar* h = chars_of(self, &hn);
    unichar* n = chars_of(s, &nn);
    NSRange res = NSMakeRange(NSNotFound, 0);
    if (nn <= r.length) {
        NSUInteger last = r.location + r.length - nn;
        BOOL ci = (opt & NSCaseInsensitiveSearch) != 0;
        if (opt & NSBackwardsSearch) {
            for (NSUInteger i = last + 1; i-- > r.location;) {
                if ((opt & NSAnchoredSearch) && i != last) break;
                NSUInteger k = 0;
                for (; k < nn; k++) {
                    unichar a = h[i + k], b = n[k];
                    if (ci) { a = fold(a); b = fold(b); }
                    if (a != b) break;
                }
                if (k == nn) { res = NSMakeRange(i, nn); break; }
            }
        } else {
            for (NSUInteger i = r.location; i <= last; i++) {
                if ((opt & NSAnchoredSearch) && i != r.location) break;
                NSUInteger k = 0;
                for (; k < nn; k++) {
                    unichar a = h[i + k], b = n[k];
                    if (ci) { a = fold(a); b = fold(b); }
                    if (a != b) break;
                }
                if (k == nn) { res = NSMakeRange(i, nn); break; }
            }
        }
    }
    free(h);
    free(n);
    return res;
}
- (NSRange)rangeOfString:(NSString*)s options:(NSUInteger)opt { return [self rangeOfString:s options:opt range:NSMakeRange(0, [self length])]; }
- (NSRange)rangeOfString:(NSString*)s { return [self rangeOfString:s options:0]; }
- (BOOL)containsString:(NSString*)s { return [self rangeOfString:s].location != NSNotFound; }
- (BOOL)localizedCaseInsensitiveContainsString:(NSString*)s { return [self rangeOfString:s options:NSCaseInsensitiveSearch].location != NSNotFound; }
- (NSRange)rangeOfCharacterFromSet:(NSCharacterSet*)set options:(NSUInteger)opt range:(NSRange)r {
    BOOL back = (opt & NSBackwardsSearch) != 0;
    for (NSUInteger k = 0; k < r.length; k++) {
        NSUInteger i = back ? r.location + r.length - 1 - k : r.location + k;
        if ([(id)set characterIsMember:[self characterAtIndex:i]]) return NSMakeRange(i, 1);
    }
    return NSMakeRange(NSNotFound, 0);
}
- (NSRange)rangeOfCharacterFromSet:(NSCharacterSet*)set options:(NSUInteger)opt {
    return [self rangeOfCharacterFromSet:set options:opt range:NSMakeRange(0, [self length])];
}
- (NSRange)rangeOfCharacterFromSet:(NSCharacterSet*)set { return [self rangeOfCharacterFromSet:set options:0]; }
- (NSRange)rangeOfComposedCharacterSequenceAtIndex:(NSUInteger)i {
    unichar c = [self characterAtIndex:i];
    if (c >= 0xd800 && c < 0xdc00 && i + 1 < [self length]) return NSMakeRange(i, 2);
    if (c >= 0xdc00 && c < 0xe000 && i > 0) return NSMakeRange(i - 1, 2);
    return NSMakeRange(i, 1);
}
- (NSRange)rangeOfComposedCharacterSequencesForRange:(NSRange)r { return r; }
- (BOOL)hasPrefix:(NSString*)s {
    NSUInteger n = [s length];
    if (n > [self length]) return NO;
    if (!n) return NO;
    return [self rangeOfString:s options:NSAnchoredSearch range:NSMakeRange(0, n)].location == 0;
}
- (BOOL)hasSuffix:(NSString*)s {
    NSUInteger n = [s length], l = [self length];
    if (n > l || !n) return NO;
    return [self rangeOfString:s options:NSAnchoredSearch range:NSMakeRange(l - n, n)].location != NSNotFound;
}

- (NSString*)substringWithRange:(NSRange)r {
    NSUInteger l = [self length];
    if (r.location > l) r.location = l;
    if (r.location + r.length > l) r.length = l - r.location;
    unichar* b = malloc(r.length * 2 + 2);
    [self getCharacters:b range:r];
    id s = [new_str(b, r.length, NO) autorelease];
    free(b);
    return s;
}
- (NSString*)substringFromIndex:(NSUInteger)i { return [self substringWithRange:NSMakeRange(i, [self length] - (i < [self length] ? i : [self length]))]; }
- (NSString*)substringToIndex:(NSUInteger)i { return [self substringWithRange:NSMakeRange(0, i)]; }

- (NSString*)stringByAppendingString:(NSString*)s {
    if (!s) return [[self copy] autorelease];
    NSMutableString* m = [[self mutableCopy] autorelease];
    [m appendString:s];
    return [[m copy] autorelease];
}
- (NSString*)stringByAppendingFormat:(NSString*)fmt, ... {
    va_list ap;
    va_start(ap, fmt);
    NSString* t = [[[NSString alloc] initWithFormat:fmt arguments:ap] autorelease];
    va_end(ap);
    return [self stringByAppendingString:t];
}
- (NSString*)stringByPaddingToLength:(NSUInteger)n withString:(NSString*)pad startingAtIndex:(NSUInteger)start {
    NSMutableString* m = [NSMutableString string];
    NSUInteger l = [self length];
    if (n <= l) return [self substringToIndex:n];
    [m appendString:self];
    NSUInteger pl = [pad length];
    for (NSUInteger i = 0; l + i < n; i++) {
        unichar c = [pad characterAtIndex:(start + i) % pl];
        [m appendString:[NSString stringWithCharacters:&c length:1]];
    }
    return m;
}
- (NSString*)stringByReplacingOccurrencesOfString:(NSString*)a withString:(NSString*)b options:(NSUInteger)opt range:(NSRange)r {
    NSMutableString* m = [[self mutableCopy] autorelease];
    [m replaceOccurrencesOfString:a withString:b options:opt range:r];
    return [[m copy] autorelease];
}
- (NSString*)stringByReplacingOccurrencesOfString:(NSString*)a withString:(NSString*)b {
    return [self stringByReplacingOccurrencesOfString:a withString:b options:0 range:NSMakeRange(0, [self length])];
}
- (NSString*)stringByReplacingCharactersInRange:(NSRange)r withString:(NSString*)s {
    NSMutableString* m = [[self mutableCopy] autorelease];
    [m replaceCharactersInRange:r withString:s];
    return [[m copy] autorelease];
}
- (NSString*)stringByTrimmingCharactersInSet:(NSCharacterSet*)set {
    NSUInteger l = [self length], a = 0, b = l;
    while (a < l && [(id)set characterIsMember:[self characterAtIndex:a]]) a++;
    while (b > a && [(id)set characterIsMember:[self characterAtIndex:b - 1]]) b--;
    return [self substringWithRange:NSMakeRange(a, b - a)];
}
- (NSArray*)componentsSeparatedByString:(NSString*)sep {
    NSMutableArray* r = [NSMutableArray array];
    NSUInteger l = [self length], start = 0;
    if (![sep length]) { [r addObject:self]; return r; }
    while (start <= l) {
        NSRange f = [self rangeOfString:sep options:0 range:NSMakeRange(start, l - start)];
        if (f.location == NSNotFound) { [r addObject:[self substringFromIndex:start]]; break; }
        [r addObject:[self substringWithRange:NSMakeRange(start, f.location - start)]];
        start = f.location + f.length;
    }
    return r;
}
- (NSArray*)componentsSeparatedByCharactersInSet:(NSCharacterSet*)set {
    NSMutableArray* r = [NSMutableArray array];
    NSUInteger l = [self length], start = 0;
    for (NSUInteger i = 0; i <= l; i++) {
        if (i == l || [(id)set characterIsMember:[self characterAtIndex:i]]) {
            [r addObject:[self substringWithRange:NSMakeRange(start, i - start)]];
            start = i + 1;
        }
    }
    return r;
}
- (NSString*)lowercaseString {
    NSUInteger n;
    unichar* b = chars_of(self, &n);
    for (NSUInteger i = 0; i < n; i++) if (b[i] < 128) b[i] = (unichar)tolower(b[i]);
    id r = [new_str(b, n, NO) autorelease];
    free(b);
    return r;
}
- (NSString*)uppercaseString {
    NSUInteger n;
    unichar* b = chars_of(self, &n);
    for (NSUInteger i = 0; i < n; i++) if (b[i] < 128) b[i] = (unichar)toupper(b[i]);
    id r = [new_str(b, n, NO) autorelease];
    free(b);
    return r;
}
- (NSString*)lowercaseStringWithLocale:(id)l { (void)l; return [self lowercaseString]; }
- (NSString*)uppercaseStringWithLocale:(id)l { (void)l; return [self uppercaseString]; }
- (NSString*)capitalizedString {
    NSUInteger n;
    unichar* b = chars_of(self, &n);
    BOOL start = YES;
    for (NSUInteger i = 0; i < n; i++) {
        if (b[i] < 128 && isalpha(b[i])) { b[i] = (unichar)(start ? toupper(b[i]) : tolower(b[i])); start = NO; }
        else start = (b[i] == ' ' || b[i] == '\t' || b[i] == '\n');
    }
    id r = [new_str(b, n, NO) autorelease];
    free(b);
    return r;
}

- (int)intValue { return (int)strtol([self UTF8String], NULL, 10); }
- (NSInteger)integerValue { return (NSInteger)strtoll([self UTF8String], NULL, 10); }
- (long long)longLongValue { return strtoll([self UTF8String], NULL, 10); }
- (unsigned long long)unsignedLongLongValue { return strtoull([self UTF8String], NULL, 10); }
- (double)doubleValue { return strtod([self UTF8String], NULL); }
- (float)floatValue { return strtof([self UTF8String], NULL); }
- (BOOL)boolValue {
    const char* s = [self UTF8String];
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+' || *s == '-') s++;
    while (*s == '0') s++;
    return *s == 'Y' || *s == 'y' || *s == 'T' || *s == 't' || (*s >= '1' && *s <= '9');
}

// ---- paths ----
- (NSArray*)pathComponents {
    NSMutableArray* r = [NSMutableArray array];
    NSUInteger l = [self length];
    if (!l) return r;
    if ([self characterAtIndex:0] == '/') [r addObject:@"/"];
    for (NSString* c in [self componentsSeparatedByString:@"/"])
        if ([c length]) [r addObject:c];
    if (l > 1 && [self characterAtIndex:l - 1] == '/') [r addObject:@"/"];
    return r;
}
- (BOOL)isAbsolutePath { return [self length] && ([self characterAtIndex:0] == '/' || [self characterAtIndex:0] == '~'); }
- (NSString*)lastPathComponent {
    NSString* s = self;
    while ([s length] > 1 && [s hasSuffix:@"/"]) s = [s substringToIndex:[s length] - 1];
    NSRange r = [s rangeOfString:@"/" options:NSBackwardsSearch];
    if (r.location == NSNotFound) return s;
    if ([s length] == 1) return s;
    return [s substringFromIndex:r.location + 1];
}
- (NSString*)stringByDeletingLastPathComponent {
    NSString* s = self;
    while ([s length] > 1 && [s hasSuffix:@"/"]) s = [s substringToIndex:[s length] - 1];
    NSRange r = [s rangeOfString:@"/" options:NSBackwardsSearch];
    if (r.location == NSNotFound) return @"";
    if (r.location == 0) return @"/";
    return [s substringToIndex:r.location];
}
- (NSString*)stringByAppendingPathComponent:(NSString*)c {
    if (![self length]) return [[c copy] autorelease];
    if (![c length]) return [[self copy] autorelease];
    BOOL a = [self hasSuffix:@"/"], b = [c hasPrefix:@"/"];
    if (a && b) return [self stringByAppendingString:[c substringFromIndex:1]];
    if (a || b) return [self stringByAppendingString:c];
    return [NSString stringWithFormat:@"%@/%@", self, c];
}
- (NSString*)pathExtension {
    NSString* last = [self lastPathComponent];
    NSRange r = [last rangeOfString:@"." options:NSBackwardsSearch];
    if (r.location == NSNotFound || r.location == 0) return @"";
    return [last substringFromIndex:r.location + 1];
}
- (NSString*)stringByDeletingPathExtension {
    NSString* last = [self lastPathComponent];
    NSRange r = [last rangeOfString:@"." options:NSBackwardsSearch];
    if (r.location == NSNotFound || r.location == 0) return [[self copy] autorelease];
    return [self substringToIndex:[self length] - ([last length] - r.location)];
}
- (NSString*)stringByAppendingPathExtension:(NSString*)e { return [NSString stringWithFormat:@"%@.%@", self, e]; }
- (NSString*)stringByStandardizingPath {
    NSMutableArray* out = [NSMutableArray array];
    BOOL abs = [self hasPrefix:@"/"];
    for (NSString* c in [self componentsSeparatedByString:@"/"]) {
        if (![c length] || [c isEqualToString:@"."]) continue;
        if ([c isEqualToString:@".."] && [out count]) { [out removeLastObject]; continue; }
        [out addObject:c];
    }
    NSString* j = [out componentsJoinedByString:@"/"];
    return abs ? [@"/" stringByAppendingString:j] : j;
}
- (NSString*)stringByResolvingSymlinksInPath { return [self stringByStandardizingPath]; }
- (NSString*)stringByExpandingTildeInPath {
    if (![self hasPrefix:@"~"]) return self;
    extern NSString* NSHomeDirectory(void);
    return [NSHomeDirectory() stringByAppendingString:[self substringFromIndex:1]];
}
- (NSString*)stringByAbbreviatingWithTildeInPath { return self; }
- (NSArray*)stringsByAppendingPaths:(NSArray*)paths {
    NSMutableArray* r = [NSMutableArray array];
    for (NSString* p in paths) [r addObject:[self stringByAppendingPathComponent:p]];
    return r;
}

// ---- percent encoding ----
- (NSString*)stringByAddingPercentEncodingWithAllowedCharacters:(NSCharacterSet*)set {
    const unsigned char* s = (const unsigned char*)[self UTF8String];
    NSMutableString* r = [NSMutableString string];
    char buf[8];
    for (; *s; s++) {
        if (*s < 128 && [(id)set characterIsMember:*s]) { buf[0] = (char)*s; buf[1] = 0; }
        else snprintf(buf, sizeof buf, "%%%02X", *s);
        [r appendString:[NSString stringWithUTF8String:buf]];
    }
    return r;
}
- (NSString*)stringByAddingPercentEscapesUsingEncoding:(NSStringEncoding)e {
    (void)e;
    const unsigned char* s = (const unsigned char*)[self UTF8String];
    NSMutableString* r = [NSMutableString string];
    char buf[8];
    for (; *s; s++) {
        if (isalnum(*s) || strchr("-_.!~*'()/:?&=;+$,@#", *s)) { buf[0] = (char)*s; buf[1] = 0; }
        else snprintf(buf, sizeof buf, "%%%02X", *s);
        [r appendString:[NSString stringWithUTF8String:buf]];
    }
    return r;
}
- (NSString*)stringByRemovingPercentEncoding {
    const char* s = [self UTF8String];
    size_t n = strlen(s);
    char* o = malloc(n + 1);
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            char h[3] = {s[i + 1], s[i + 2], 0};
            o[k++] = (char)strtol(h, NULL, 16);
            i += 2;
        } else o[k++] = s[i];
    }
    o[k] = 0;
    NSString* r = [NSString stringWithUTF8String:o];
    free(o);
    return r;
}
- (NSString*)stringByReplacingPercentEscapesUsingEncoding:(NSStringEncoding)e { (void)e; return [self stringByRemovingPercentEncoding]; }

- (void)enumerateLinesUsingBlock:(void (^)(NSString* line, BOOL* stop))block {
    BOOL stop = NO;
    for (NSString* l in [self componentsSeparatedByString:@"\n"]) {
        block(l, &stop);
        if (stop) break;
    }
}
- (void)enumerateSubstringsInRange:(NSRange)r options:(NSUInteger)opt usingBlock:(void (^)(NSString*, NSRange, NSRange, BOOL*))block {
    (void)opt;
    BOOL stop = NO;
    for (NSUInteger i = r.location; i < NSMaxRange(r) && !stop;) {
        NSRange cr = [self rangeOfComposedCharacterSequenceAtIndex:i];
        block([self substringWithRange:cr], cr, cr, &stop);
        i = NSMaxRange(cr);
    }
}

- (BOOL)writeToFile:(NSString*)p atomically:(BOOL)a encoding:(NSStringEncoding)e error:(NSError**)err {
    BOOL ok = [[self dataUsingEncoding:e] writeToFile:p atomically:a];
    if (!ok && err) *err = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:512 userInfo:nil];
    return ok;
}
- (BOOL)writeToFile:(NSString*)p atomically:(BOOL)a { return [self writeToFile:p atomically:a encoding:NSUTF8StringEncoding error:NULL]; }
- (BOOL)writeToURL:(NSURL*)u atomically:(BOOL)a encoding:(NSStringEncoding)e error:(NSError**)err {
    return [self writeToFile:(NSString*)[(id)u performSelector:@selector(path)] atomically:a encoding:e error:err];
}
- (id)propertyList { return _emu_plist_parse([self dataUsingEncoding:NSUTF8StringEncoding]); }
@end

// ===================================================================== NSMutableString
@implementation NSMutableString
+ (instancetype)stringWithCapacity:(NSUInteger)n { (void)n; return [[[__NSCFString alloc] _initWithChars:NULL length:0 mutable:YES] autorelease]; }
+ (instancetype)string { return [[[__NSCFString alloc] _initWithChars:NULL length:0 mutable:YES] autorelease]; }
+ (instancetype)stringWithString:(NSString*)s { return [[s mutableCopy] autorelease]; }
+ (instancetype)stringWithUTF8String:(const char*)s {
    if (!s) return nil;
    return [new_str_utf8(s, strlen(s), YES) autorelease];
}
+ (instancetype)stringWithFormat:(NSString*)fmt, ... {
    va_list ap;
    va_start(ap, fmt);
    NSString* t = [[NSString alloc] initWithFormat:fmt arguments:ap];
    va_end(ap);
    id r = [[t mutableCopy] autorelease];
    [t release];
    return r;
}
- (instancetype)initWithCapacity:(NSUInteger)n { (void)n; return reinit(self, NULL, 0); }
- (void)appendString:(NSString*)s { [self replaceCharactersInRange:NSMakeRange([self length], 0) withString:s]; }
- (void)appendFormat:(NSString*)fmt, ... {
    va_list ap;
    va_start(ap, fmt);
    NSString* t = [[NSString alloc] initWithFormat:fmt arguments:ap];
    va_end(ap);
    [self appendString:t];
    [t release];
}
- (void)insertString:(NSString*)s atIndex:(NSUInteger)i { [self replaceCharactersInRange:NSMakeRange(i, 0) withString:s]; }
- (void)deleteCharactersInRange:(NSRange)r { [self replaceCharactersInRange:r withString:@""]; }
- (void)setString:(NSString*)s { [self replaceCharactersInRange:NSMakeRange(0, [self length]) withString:s]; }
- (void)replaceCharactersInRange:(NSRange)r withString:(NSString*)s { (void)r; (void)s; }
- (NSUInteger)replaceOccurrencesOfString:(NSString*)a withString:(NSString*)b options:(NSUInteger)opt range:(NSRange)r {
    NSUInteger count = 0;
    NSUInteger bl = [b length];
    for (;;) {
        NSRange f = [self rangeOfString:a options:opt & ~NSBackwardsSearch range:r];
        if (f.location == NSNotFound) break;
        [self replaceCharactersInRange:f withString:b];
        count++;
        NSUInteger end = NSMaxRange(r) - f.length + bl;
        r.location = f.location + bl;
        r.length = end - r.location;
    }
    return count;
}
@end

// ===================================================================== __NSCFString
@implementation __NSCFString
- (instancetype)_initWithChars:(const unichar*)c length:(NSUInteger)n mutable:(BOOL)m {
    _cap = n ? n : 8;
    _buf = malloc(_cap * sizeof(unichar));
    if (n && c) memcpy(_buf, c, n * sizeof(unichar));
    _len = n;
    _mutable = m;
    return self;
}
- (instancetype)init { return [self _initWithChars:NULL length:0 mutable:_mutable]; }
- (void)dealloc {
    free(_buf);
    free(_utf8);
    [super dealloc];
}
- (NSUInteger)length { return _len; }
- (unichar)characterAtIndex:(NSUInteger)i { return i < _len ? _buf[i] : 0; }
- (void)getCharacters:(unichar*)b range:(NSRange)r {
    if (r.location + r.length > _len) {
        if (r.location > _len) return;
        r.length = _len - r.location;
    }
    memcpy(b, _buf + r.location, r.length * sizeof(unichar));
}
- (const char*)UTF8String {
    if (!_utf8) {
        size_t l = utf16_to_utf8(_buf, _len, NULL);
        char* u = malloc(l + 1);
        utf16_to_utf8(_buf, _len, u);
        u[l] = 0;
        _utf8 = u;
    }
    return _utf8;
}
- (id)copyWithZone:(NSZone*)z {
    (void)z;
    if (!_mutable) return [self retain];
    return new_str(_buf, _len, NO);
}
- (void)replaceCharactersInRange:(NSRange)r withString:(NSString*)s {
    if (r.location > _len) r.location = _len;
    if (r.location + r.length > _len) r.length = _len - r.location;
    NSUInteger sl = [s length];
    unichar* sb = NULL;
    if (sl) {
        sb = malloc(sl * sizeof(unichar));
        [s getCharacters:sb range:NSMakeRange(0, sl)];
    }
    NSUInteger nl = _len - r.length + sl;
    if (nl > _cap) {
        _cap = nl * 2;
        _buf = realloc(_buf, _cap * sizeof(unichar));
    }
    memmove(_buf + r.location + sl, _buf + r.location + r.length, (_len - r.location - r.length) * sizeof(unichar));
    if (sl) memcpy(_buf + r.location, sb, sl * sizeof(unichar));
    _len = nl;
    free(sb);
    free(_utf8);
    _utf8 = NULL;
}
- (void)appendString:(NSString*)s { [self replaceCharactersInRange:NSMakeRange(_len, 0) withString:s]; }
- (NSUInteger)hash {
    uint64_t h = 1469598103934665603ull ^ _len;
    NSUInteger k = _len < 64 ? _len : 64;
    for (NSUInteger i = 0; i < k; i++) { h ^= _buf[i]; h *= 1099511628211ull; }
    return (NSUInteger)h;
}
- (BOOL)isEqualToString:(NSString*)o {
    if (o == self) return YES;
    if (!o) return NO;
    if (object_getClass(o) == object_getClass(self)) {
        __NSCFString* x = (__NSCFString*)o;
        return x->_len == _len && memcmp(x->_buf, _buf, _len * 2) == 0;
    }
    return [super isEqualToString:o];
}
@end

// ===================================================================== constant strings
@implementation __NSCFConstantString
- (id)retain { return self; }
- (oneway void)release {}
- (id)autorelease { return self; }
- (NSUInteger)retainCount { return (NSUInteger)-1 >> 1; }
- (void)dealloc {}
- (id)copyWithZone:(NSZone*)z { (void)z; return self; }
- (BOOL)_isUTF16 { return (_flags & 0x10) == 0x10 && (_flags & 0x8) == 0; }
- (NSUInteger)length { return (NSUInteger)_clen; }
- (unichar)characterAtIndex:(NSUInteger)i {
    if (i >= (NSUInteger)_clen) return 0;
    return [self _isUTF16] ? ((const unichar*)_ptr)[i] : ((const unsigned char*)_ptr)[i];
}
- (void)getCharacters:(unichar*)b range:(NSRange)r {
    if ([self _isUTF16]) memcpy(b, (const unichar*)_ptr + r.location, r.length * 2);
    else for (NSUInteger i = 0; i < r.length; i++) b[i] = ((const unsigned char*)_ptr)[r.location + i];
}
- (const char*)UTF8String {
    if (![self _isUTF16]) return (const char*)_ptr;
    return [super UTF8String];
}
- (NSUInteger)hash {
    uint64_t h = 1469598103934665603ull ^ (NSUInteger)_clen;
    NSUInteger k = (NSUInteger)_clen < 64 ? (NSUInteger)_clen : 64;
    BOOL w = [self _isUTF16];
    for (NSUInteger i = 0; i < k; i++) { h ^= w ? ((const unichar*)_ptr)[i] : ((const unsigned char*)_ptr)[i]; h *= 1099511628211ull; }
    return (NSUInteger)h;
}
@end

// Compiler-emitted @"..." literals point their isa at this symbol. It is
// turned into a copy of the __NSCFConstantString class during early init.
EMU_EXPORT void* __CFConstantStringClassReference[16];

EMU_EXPORT void _emu_foundation_early_init(void) {
    extern Class objc_getClass(const char*);
    Class c = objc_getClass("__NSCFConstantString");
    if (c) memcpy(__CFConstantStringClassReference, (void*)c, 5 * sizeof(void*));
}

// ===================================================================== formatting
typedef struct sbuf {
    char* p;
    size_t n, cap;
} sbuf;
static void sb_put(sbuf* b, const char* s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        b->cap = (b->n + n + 1) * 2;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}

char* _emu_vformat(const char* fmt, va_list ap) {
    sbuf b = {malloc(64), 0, 64};
    b.p[0] = 0;
    const char* f = fmt;
    while (*f) {
        const char* pct = strchr(f, '%');
        if (!pct) { sb_put(&b, f, strlen(f)); break; }
        sb_put(&b, f, (size_t)(pct - f));
        f = pct + 1;
        if (*f == '%') { sb_put(&b, "%", 1); f++; continue; }
        // collect spec
        char spec[64];
        size_t sl = 0;
        spec[sl++] = '%';
        // positional args are not supported: skip "n$"
        const char* q = f;
        while (*q >= '0' && *q <= '9') q++;
        if (*q == '$') f = q + 1;
        int width_star = 0, prec_star = 0;
        while (*f && strchr("-+ #0'", *f)) { if (*f != '\'' && sl < 40) spec[sl++] = *f; f++; }
        int wv = 0, pv = 0;
        if (*f == '*') { width_star = 1; wv = va_arg(ap, int); f++; }
        while (*f >= '0' && *f <= '9') { if (sl < 40) spec[sl++] = *f; f++; }
        if (width_star) sl += (size_t)snprintf(spec + sl, sizeof spec - sl, "%d", wv);
        if (*f == '.') {
            spec[sl++] = '.';
            f++;
            if (*f == '*') { prec_star = 1; pv = va_arg(ap, int); f++; sl += (size_t)snprintf(spec + sl, sizeof spec - sl, "%d", pv); }
            while (*f >= '0' && *f <= '9') { if (sl < 50) spec[sl++] = *f; f++; }
        }
        (void)prec_star;
        char len[3] = {0, 0, 0};
        int li = 0;
        while (*f && strchr("hlLqjzt", *f)) { if (li < 2) len[li++] = *f; f++; }
        char conv = *f ? *f++ : 0;
        char out[512];
        int is64 = len[0] == 'l' || len[0] == 'q' || len[0] == 'j' || len[0] == 'z' || len[0] == 't' || len[0] == 'L';
        switch (conv) {
        case '@': {
            id o = va_arg(ap, id);
            const char* s = o ? [[o description] UTF8String] : "(null)";
            spec[sl] = 's';
            spec[sl + 1] = 0;
            char* tmp = NULL;
            int n = snprintf(NULL, 0, spec, s);
            tmp = malloc((size_t)n + 1);
            snprintf(tmp, (size_t)n + 1, spec, s);
            sb_put(&b, tmp, (size_t)n);
            free(tmp);
            continue;
        }
        case 'd': case 'i': case 'D': {
            long long v = is64 || conv == 'D' ? va_arg(ap, long long) : (long long)va_arg(ap, int);
            if (len[0] == 'h' && len[1] == 'h') v = (signed char)v;
            else if (len[0] == 'h') v = (short)v;
            memcpy(spec + sl, "lld", 4);
            snprintf(out, sizeof out, spec, v);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': case 'U': case 'O': {
            unsigned long long v = is64 || conv == 'U' || conv == 'O' ? va_arg(ap, unsigned long long) : (unsigned long long)va_arg(ap, unsigned);
            if (len[0] == 'h' && len[1] == 'h') v = (unsigned char)v;
            else if (len[0] == 'h') v = (unsigned short)v;
            spec[sl] = 'l';
            spec[sl + 1] = 'l';
            spec[sl + 2] = conv == 'U' ? 'u' : conv == 'O' ? 'o' : conv;
            spec[sl + 3] = 0;
            snprintf(out, sizeof out, spec, v);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
            double v = va_arg(ap, double);
            spec[sl] = conv;
            spec[sl + 1] = 0;
            snprintf(out, sizeof out, spec, v);
            break;
        }
        case 'c': {
            int v = va_arg(ap, int);
            if (len[0] == 'l') {
                unichar u = (unichar)v;
                size_t k = utf16_to_utf8(&u, 1, out);
                out[k] = 0;
            } else {
                spec[sl] = 'c';
                spec[sl + 1] = 0;
                snprintf(out, sizeof out, spec, v);
            }
            break;
        }
        case 'C': {
            unichar u = (unichar)va_arg(ap, int);
            size_t k = utf16_to_utf8(&u, 1, out);
            out[k] = 0;
            break;
        }
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            spec[sl] = 's';
            spec[sl + 1] = 0;
            int n = snprintf(NULL, 0, spec, s);
            char* tmp = malloc((size_t)n + 1);
            snprintf(tmp, (size_t)n + 1, spec, s);
            sb_put(&b, tmp, (size_t)n);
            free(tmp);
            continue;
        }
        case 'S': {
            const unichar* s = va_arg(ap, const unichar*);
            if (!s) { sb_put(&b, "(null)", 6); continue; }
            size_t n = 0;
            while (s[n]) n++;
            size_t k = utf16_to_utf8(s, n, NULL);
            char* tmp = malloc(k + 1);
            utf16_to_utf8(s, n, tmp);
            sb_put(&b, tmp, k);
            free(tmp);
            continue;
        }
        case 'p': {
            void* v = va_arg(ap, void*);
            snprintf(out, sizeof out, "%p", v);
            break;
        }
        case 'n': {
            int* p = va_arg(ap, int*);
            if (p) *p = (int)b.n;
            out[0] = 0;
            break;
        }
        default:
            out[0] = 0;
            break;
        }
        sb_put(&b, out, strlen(out));
    }
    return b.p;
}

EMU_EXPORT void NSLogv(NSString* fmt, va_list ap) {
    char* s = _emu_vformat(fmt ? [fmt UTF8String] : "(null)", ap);
    size_t n = strlen(s);
    while (n && s[n - 1] == '\n') s[--n] = 0;
    fprintf(stderr, "[NSLog] %s\n", s);
    free(s);
}
EMU_EXPORT void NSLog(NSString* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    NSLogv(fmt, ap);
    va_end(ap);
}
