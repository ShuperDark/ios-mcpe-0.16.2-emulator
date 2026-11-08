// Property lists (binary, XML, OpenStep/.strings) and JSON.
#import <emu/Foundation.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===================================================================== binary plist
typedef struct bp {
    const uint8_t* d;
    size_t n;
    int offsz, refsz;
    uint64_t nobj, top, tbl;
    id* cache;
} bp;

static uint64_t be(const uint8_t* p, int sz) {
    uint64_t v = 0;
    for (int i = 0; i < sz; i++) v = (v << 8) | p[i];
    return v;
}
static id bp_obj(bp* b, uint64_t idx, int depth);
static uint64_t bp_len(bp* b, const uint8_t** p, uint8_t low) {
    if (low != 0xf) return low;
    uint8_t m = **p;
    (*p)++;
    int sz = 1 << (m & 0xf);
    uint64_t v = be(*p, sz);
    *p += sz;
    (void)b;
    return v;
}
static id bp_obj(bp* b, uint64_t idx, int depth) {
    if (idx >= b->nobj || depth > 64) return nil;
    if (b->cache[idx]) return b->cache[idx];
    uint64_t off = be(b->d + b->tbl + idx * b->offsz, b->offsz);
    if (off >= b->n) return nil;
    const uint8_t* p = b->d + off;
    uint8_t m = *p++;
    uint8_t hi = m >> 4, lo = m & 0xf;
    id r = nil;
    switch (hi) {
    case 0x0:
        if (lo == 8) r = [NSNumber numberWithBool:NO];
        else if (lo == 9) r = [NSNumber numberWithBool:YES];
        else r = [NSNull null];
        break;
    case 0x1: {
        int sz = 1 << lo;
        uint64_t v = be(p, sz);
        if (sz == 8) r = [NSNumber numberWithLongLong:(long long)v];
        else r = [NSNumber numberWithLongLong:(long long)v];
    } break;
    case 0x2: {
        if (lo == 2) { uint32_t v = (uint32_t)be(p, 4); float f; memcpy(&f, &v, 4); r = [NSNumber numberWithFloat:f]; }
        else { uint64_t v = be(p, 8); double d; memcpy(&d, &v, 8); r = [NSNumber numberWithDouble:d]; }
    } break;
    case 0x3: {
        uint64_t v = be(p, 8);
        double d;
        memcpy(&d, &v, 8);
        r = [NSDate dateWithTimeIntervalSinceReferenceDate:d];
    } break;
    case 0x4: {
        uint64_t n = bp_len(b, &p, lo);
        r = [NSData dataWithBytes:p length:n];
    } break;
    case 0x5: {
        uint64_t n = bp_len(b, &p, lo);
        r = [[[NSString alloc] initWithBytes:p length:n encoding:NSISOLatin1StringEncoding] autorelease];
    } break;
    case 0x6: {
        uint64_t n = bp_len(b, &p, lo);
        r = [[[NSString alloc] initWithBytes:p length:n * 2 encoding:NSUTF16BigEndianStringEncoding] autorelease];
    } break;
    case 0x8: {
        r = [NSNumber numberWithUnsignedLongLong:be(p, lo + 1)];
    } break;
    case 0xa: case 0xc: {
        uint64_t n = bp_len(b, &p, lo);
        NSMutableArray* a = [NSMutableArray arrayWithCapacity:n];
        for (uint64_t i = 0; i < n; i++) {
            id o = bp_obj(b, be(p + i * b->refsz, b->refsz), depth + 1);
            if (o) [a addObject:o];
        }
        r = hi == 0xa ? (id)a : (id)[NSSet setWithArray:a];
    } break;
    case 0xd: {
        uint64_t n = bp_len(b, &p, lo);
        NSMutableDictionary* d = [NSMutableDictionary dictionaryWithCapacity:n];
        for (uint64_t i = 0; i < n; i++) {
            id k = bp_obj(b, be(p + i * b->refsz, b->refsz), depth + 1);
            id v = bp_obj(b, be(p + (n + i) * b->refsz, b->refsz), depth + 1);
            if (k && v) [d setObject:v forKey:k];
        }
        r = d;
    } break;
    default: break;
    }
    if (r) b->cache[idx] = [r retain];
    return r;
}
static id parse_binary(const uint8_t* d, size_t n) {
    if (n < 40) return nil;
    const uint8_t* t = d + n - 32;
    bp b = {d, n, t[6], t[7], be(t + 8, 8), be(t + 16, 8), be(t + 24, 8), NULL};
    if (!b.offsz || !b.refsz || b.nobj > 10000000 || b.tbl >= n) return nil;
    b.cache = calloc(b.nobj, sizeof(id));
    id r = [[bp_obj(&b, b.top, 0) retain] autorelease];
    for (uint64_t i = 0; i < b.nobj; i++) [b.cache[i] release];
    free(b.cache);
    return r;
}

// ===================================================================== XML plist
typedef struct xp {
    const char* p;
    const char* e;
} xp;
static void xskip(xp* x) {
    for (;;) {
        while (x->p < x->e && isspace((unsigned char)*x->p)) x->p++;
        if (x->e - x->p >= 4 && !strncmp(x->p, "<!--", 4)) {
            const char* c = strstr(x->p, "-->");
            x->p = c ? c + 3 : x->e;
            continue;
        }
        if (x->e - x->p >= 2 && (!strncmp(x->p, "<?", 2) || !strncmp(x->p, "<!", 2))) {
            const char* c = memchr(x->p, '>', (size_t)(x->e - x->p));
            x->p = c ? c + 1 : x->e;
            continue;
        }
        break;
    }
}
static NSString* xunescape(const char* s, size_t n) {
    NSMutableData* out = [NSMutableData dataWithCapacity:n];
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '&') {
            const char* semi = memchr(s + i, ';', n - i);
            if (semi) {
                size_t l = (size_t)(semi - (s + i));
                char c = 0;
                char buf[8];
                size_t bl = 0;
                if (l == 3 && !strncmp(s + i, "&lt", 3)) c = '<';
                else if (l == 3 && !strncmp(s + i, "&gt", 3)) c = '>';
                else if (l == 4 && !strncmp(s + i, "&amp", 4)) c = '&';
                else if (l == 5 && !strncmp(s + i, "&quot", 5)) c = '"';
                else if (l == 5 && !strncmp(s + i, "&apos", 5)) c = '\'';
                else if (s[i + 1] == '#') {
                    unsigned cp = (unsigned)(s[i + 2] == 'x' ? strtoul(s + i + 3, NULL, 16) : strtoul(s + i + 2, NULL, 10));
                    if (cp < 0x80) buf[bl++] = (char)cp;
                    else if (cp < 0x800) { buf[bl++] = (char)(0xc0 | (cp >> 6)); buf[bl++] = (char)(0x80 | (cp & 0x3f)); }
                    else { buf[bl++] = (char)(0xe0 | (cp >> 12)); buf[bl++] = (char)(0x80 | ((cp >> 6) & 0x3f)); buf[bl++] = (char)(0x80 | (cp & 0x3f)); }
                }
                if (c) { [out appendBytes:&c length:1]; i += l; continue; }
                if (bl) { [out appendBytes:buf length:bl]; i += l; continue; }
            }
        }
        [out appendBytes:s + i length:1];
    }
    return [[[NSString alloc] initWithData:out encoding:NSUTF8StringEncoding] autorelease];
}
static id xvalue(xp* x, int depth);
static BOOL xtag(xp* x, char* name, size_t cap, BOOL* empty, BOOL* closing) {
    xskip(x);
    if (x->p >= x->e || *x->p != '<') return NO;
    x->p++;
    *closing = NO;
    if (*x->p == '/') { *closing = YES; x->p++; }
    size_t k = 0;
    while (x->p < x->e && !isspace((unsigned char)*x->p) && *x->p != '>' && *x->p != '/') {
        if (k + 1 < cap) name[k++] = *x->p;
        x->p++;
    }
    name[k] = 0;
    while (x->p < x->e && *x->p != '>') {
        if (*x->p == '/') *empty = YES;
        x->p++;
    }
    if (x->p < x->e) x->p++;
    return YES;
}
static NSString* xtext(xp* x, const char* close) {
    const char* start = x->p;
    const char* end = strstr(x->p, close);
    if (!end || end > x->e) end = x->e;
    x->p = end + strlen(close);
    if (x->p > x->e) x->p = x->e;
    return xunescape(start, (size_t)(end - start));
}
static id xvalue(xp* x, int depth) {
    if (depth > 64) return nil;
    char name[32];
    BOOL empty = NO, closing = NO;
    if (!xtag(x, name, sizeof name, &empty, &closing) || closing) return nil;
    if (!strcmp(name, "plist")) return xvalue(x, depth + 1);
    if (!strcmp(name, "dict")) {
        NSMutableDictionary* d = [NSMutableDictionary dictionary];
        if (empty) return d;
        for (;;) {
            const char* save = x->p;
            BOOL e2 = NO, c2 = NO;
            char n2[32];
            if (!xtag(x, n2, sizeof n2, &e2, &c2)) break;
            if (c2) break;
            if (strcmp(n2, "key")) { x->p = save; xvalue(x, depth + 1); continue; }
            NSString* k = e2 ? @"" : xtext(x, "</key>");
            id v = xvalue(x, depth + 1);
            if (k && v) [d setObject:v forKey:k];
        }
        return d;
    }
    if (!strcmp(name, "array")) {
        NSMutableArray* a = [NSMutableArray array];
        if (empty) return a;
        for (;;) {
            const char* save = x->p;
            BOOL e2 = NO, c2 = NO;
            char n2[32];
            if (!xtag(x, n2, sizeof n2, &e2, &c2)) break;
            if (c2) break;
            x->p = save;
            id v = xvalue(x, depth + 1);
            if (!v) break;
            [a addObject:v];
        }
        return a;
    }
    if (!strcmp(name, "string")) return empty ? @"" : xtext(x, "</string>");
    if (!strcmp(name, "integer")) return [NSNumber numberWithLongLong:[xtext(x, "</integer>") longLongValue]];
    if (!strcmp(name, "real")) return [NSNumber numberWithDouble:[xtext(x, "</real>") doubleValue]];
    if (!strcmp(name, "true")) return [NSNumber numberWithBool:YES];
    if (!strcmp(name, "false")) return [NSNumber numberWithBool:NO];
    if (!strcmp(name, "date")) { xtext(x, "</date>"); return [NSDate date]; }
    if (!strcmp(name, "data")) {
        NSString* t = empty ? @"" : xtext(x, "</data>");
        id d = [[[NSData alloc] performSelector:@selector(initWithBase64EncodedString:options:) withObject:t withObject:nil] autorelease];
        return d ? d : [NSData data];
    }
    return nil;
}

// ===================================================================== OpenStep / .strings
typedef struct op {
    const unichar* p;
    const unichar* e;
} op;
static void oskip(op* o) {
    for (;;) {
        while (o->p < o->e && (*o->p == ' ' || *o->p == '\t' || *o->p == '\n' || *o->p == '\r' || *o->p == 0xfeff)) o->p++;
        if (o->e - o->p >= 2 && o->p[0] == '/' && o->p[1] == '/') { while (o->p < o->e && *o->p != '\n') o->p++; continue; }
        if (o->e - o->p >= 2 && o->p[0] == '/' && o->p[1] == '*') {
            o->p += 2;
            while (o->e - o->p >= 2 && !(o->p[0] == '*' && o->p[1] == '/')) o->p++;
            o->p += 2;
            continue;
        }
        break;
    }
}
static id ovalue(op* o, int depth);
static NSString* ostring(op* o) {
    oskip(o);
    if (o->p >= o->e) return nil;
    NSMutableString* s = [NSMutableString string];
    if (*o->p == '"') {
        o->p++;
        while (o->p < o->e && *o->p != '"') {
            unichar c = *o->p++;
            if (c == '\\' && o->p < o->e) {
                c = *o->p++;
                if (c == 'n') c = '\n';
                else if (c == 't') c = '\t';
                else if (c == 'r') c = '\r';
                else if (c == 'U' || c == 'u') {
                    unsigned v = 0;
                    for (int k = 0; k < 4 && o->p < o->e; k++, o->p++) v = v * 16 + (unsigned)(isdigit(*o->p) ? *o->p - '0' : (tolower(*o->p) - 'a' + 10));
                    c = (unichar)v;
                }
            }
            [s appendString:[NSString stringWithCharacters:&c length:1]];
        }
        if (o->p < o->e) o->p++;
        return s;
    }
    const unichar* st = o->p;
    while (o->p < o->e && (isalnum(*o->p) || *o->p == '_' || *o->p == '.' || *o->p == '$' || *o->p == '/' || *o->p == ':' || *o->p == '-')) o->p++;
    if (o->p == st) return nil;
    return [NSString stringWithCharacters:st length:(NSUInteger)(o->p - st)];
}
static id ovalue(op* o, int depth) {
    oskip(o);
    if (o->p >= o->e || depth > 64) return nil;
    if (*o->p == '{') {
        o->p++;
        NSMutableDictionary* d = [NSMutableDictionary dictionary];
        for (;;) {
            oskip(o);
            if (o->p >= o->e) break;
            if (*o->p == '}') { o->p++; break; }
            NSString* k = ostring(o);
            if (!k) break;
            oskip(o);
            if (o->p < o->e && *o->p == '=') o->p++;
            id v = ovalue(o, depth + 1);
            oskip(o);
            if (o->p < o->e && *o->p == ';') o->p++;
            if (v) [d setObject:v forKey:k];
        }
        return d;
    }
    if (*o->p == '(') {
        o->p++;
        NSMutableArray* a = [NSMutableArray array];
        for (;;) {
            oskip(o);
            if (o->p >= o->e) break;
            if (*o->p == ')') { o->p++; break; }
            id v = ovalue(o, depth + 1);
            if (!v) break;
            [a addObject:v];
            oskip(o);
            if (o->p < o->e && *o->p == ',') o->p++;
        }
        return a;
    }
    return ostring(o);
}
static id parse_openstep(NSString* s) {
    NSUInteger n = [s length];
    unichar* b = malloc((n + 1) * sizeof(unichar));
    [s getCharacters:b range:NSMakeRange(0, n)];
    op o = {b, b + n};
    oskip(&o);
    id r;
    if (o.p < o.e && (*o.p == '{' || *o.p == '(')) r = ovalue(&o, 0);
    else {
        // .strings file: implicit dictionary
        NSMutableDictionary* d = [NSMutableDictionary dictionary];
        for (;;) {
            NSString* k = ostring(&o);
            if (!k) break;
            oskip(&o);
            if (o.p < o.e && *o.p == '=') o.p++;
            id v = ovalue(&o, 1);
            oskip(&o);
            if (o.p < o.e && *o.p == ';') o.p++;
            if (v) [d setObject:v forKey:k];
        }
        r = d;
    }
    free(b);
    return r;
}

id _emu_plist_parse(NSData* data) {
    if (!data || ![data length]) return nil;
    const uint8_t* d = [data bytes];
    size_t n = [data length];
    if (n >= 8 && !memcmp(d, "bplist00", 8)) return parse_binary(d, n);
    // UTF-16 text (e.g. .strings files)
    if (n >= 2 && ((d[0] == 0xff && d[1] == 0xfe) || (d[0] == 0xfe && d[1] == 0xff))) {
        NSString* s = [[[NSString alloc] initWithData:data encoding:d[0] == 0xff ? NSUTF16LittleEndianStringEncoding : NSUTF16BigEndianStringEncoding] autorelease];
        return parse_openstep(s);
    }
    xp x = {(const char*)d, (const char*)d + n};
    xskip(&x);
    if (x.p < x.e && *x.p == '<') {
        id r = xvalue(&x, 0);
        if (r) return r;
    }
    NSString* s = [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] autorelease];
    return s ? parse_openstep(s) : nil;
}

// ===================================================================== XML writer
static void xesc(NSMutableString* out, NSString* s) {
    NSString* t = [[[s stringByReplacingOccurrencesOfString:@"&" withString:@"&amp;"] stringByReplacingOccurrencesOfString:@"<" withString:@"&lt;"]
        stringByReplacingOccurrencesOfString:@">" withString:@"&gt;"];
    [out appendString:t];
}
static void xwrite(NSMutableString* out, id o, int ind) {
    NSString* pad = [@"" stringByPaddingToLength:(NSUInteger)ind withString:@"\t" startingAtIndex:0];
    if ([o isKindOfClass:[NSString class]]) { [out appendFormat:@"%@<string>", pad]; xesc(out, o); [out appendString:@"</string>\n"]; }
    else if ([o isKindOfClass:[NSNumber class]]) {
        const char* t = [o objCType];
        if (o == (id)[NSNumber numberWithBool:YES]) [out appendFormat:@"%@<true/>\n", pad];
        else if (o == (id)[NSNumber numberWithBool:NO]) [out appendFormat:@"%@<false/>\n", pad];
        else if (*t == 'f' || *t == 'd') [out appendFormat:@"%@<real>%.17g</real>\n", pad, [o doubleValue]];
        else [out appendFormat:@"%@<integer>%lld</integer>\n", pad, [o longLongValue]];
    } else if ([o isKindOfClass:[NSDictionary class]]) {
        [out appendFormat:@"%@<dict>\n", pad];
        NSArray* keys = [[o allKeys] sortedArrayUsingSelector:@selector(compare:)];
        for (id k in keys) {
            [out appendFormat:@"%@\t<key>", pad];
            xesc(out, [k description]);
            [out appendString:@"</key>\n"];
            xwrite(out, [o objectForKey:k], ind + 1);
        }
        [out appendFormat:@"%@</dict>\n", pad];
    } else if ([o isKindOfClass:[NSArray class]]) {
        [out appendFormat:@"%@<array>\n", pad];
        for (id v in o) xwrite(out, v, ind + 1);
        [out appendFormat:@"%@</array>\n", pad];
    } else if ([o isKindOfClass:[NSData class]]) {
        [out appendFormat:@"%@<data>%@</data>\n", pad, [o performSelector:@selector(base64EncodedStringWithOptions:) withObject:nil]];
    } else if ([o isKindOfClass:[NSDate class]]) {
        [out appendFormat:@"%@<date>%@</date>\n", pad, [o description]];
    } else {
        [out appendFormat:@"%@<string>", pad];
        xesc(out, [o description]);
        [out appendString:@"</string>\n"];
    }
}
NSData* _emu_plist_xml(id obj) {
    NSMutableString* out = [NSMutableString stringWithString:
        @"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n<plist version=\"1.0\">\n"];
    xwrite(out, obj, 0);
    [out appendString:@"</plist>\n"];
    return [out dataUsingEncoding:NSUTF8StringEncoding];
}

@interface NSPropertyListSerialization : NSObject
@end
@implementation NSPropertyListSerialization
+ (id)propertyListWithData:(NSData*)d options:(NSUInteger)o format:(NSUInteger*)f error:(NSError**)e {
    id r = _emu_plist_parse(d);
    if (f) *f = 100;
    if (!r && e) *e = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:3840 userInfo:nil];
    if (r && (o & 3)) r = [[r mutableCopy] autorelease];
    return r;
}
+ (id)propertyListFromData:(NSData*)d mutabilityOption:(NSUInteger)o format:(NSUInteger*)f errorDescription:(NSString**)e {
    if (e) *e = nil;
    return [self propertyListWithData:d options:o format:f error:NULL];
}
+ (NSData*)dataWithPropertyList:(id)p format:(NSUInteger)f options:(NSUInteger)o error:(NSError**)e {
    (void)f; (void)o; (void)e;
    return _emu_plist_xml(p);
}
+ (NSData*)dataFromPropertyList:(id)p format:(NSUInteger)f errorDescription:(NSString**)e {
    (void)f;
    if (e) *e = nil;
    return _emu_plist_xml(p);
}
+ (BOOL)propertyList:(id)p isValidForFormat:(NSUInteger)f { (void)p; (void)f; return YES; }
@end

// ===================================================================== JSON
typedef struct jp {
    const char* p;
    const char* e;
    BOOL mutableContainers;
} jp;
static void jskip(jp* j) { while (j->p < j->e && isspace((unsigned char)*j->p)) j->p++; }
static id jvalue(jp* j, int depth);
static NSString* jstring(jp* j) {
    if (j->p >= j->e || *j->p != '"') return nil;
    j->p++;
    NSMutableData* out = [NSMutableData data];
    while (j->p < j->e && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->e) {
            c = *j->p++;
            switch (c) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                unsigned cp = 0;
                for (int k = 0; k < 4 && j->p < j->e; k++, j->p++) cp = cp * 16 + (unsigned)(isdigit((unsigned char)*j->p) ? *j->p - '0' : (tolower((unsigned char)*j->p) - 'a' + 10));
                if (cp >= 0xd800 && cp < 0xdc00 && j->e - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u') {
                    unsigned lo = (unsigned)strtoul((char[]){j->p[2], j->p[3], j->p[4], j->p[5], 0}, NULL, 16);
                    if (lo >= 0xdc00 && lo < 0xe000) { cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00); j->p += 6; }
                }
                char buf[4];
                int bl = 0;
                if (cp < 0x80) buf[bl++] = (char)cp;
                else if (cp < 0x800) { buf[bl++] = (char)(0xc0 | (cp >> 6)); buf[bl++] = (char)(0x80 | (cp & 0x3f)); }
                else if (cp < 0x10000) { buf[bl++] = (char)(0xe0 | (cp >> 12)); buf[bl++] = (char)(0x80 | ((cp >> 6) & 0x3f)); buf[bl++] = (char)(0x80 | (cp & 0x3f)); }
                else { buf[bl++] = (char)(0xf0 | (cp >> 18)); buf[bl++] = (char)(0x80 | ((cp >> 12) & 0x3f)); buf[bl++] = (char)(0x80 | ((cp >> 6) & 0x3f)); buf[bl++] = (char)(0x80 | (cp & 0x3f)); }
                [out appendBytes:buf length:(NSUInteger)bl];
                continue;
            }
            default: break;
            }
        }
        [out appendBytes:&c length:1];
    }
    if (j->p < j->e) j->p++;
    return [[[NSString alloc] initWithData:out encoding:NSUTF8StringEncoding] autorelease];
}
static id jvalue(jp* j, int depth) {
    jskip(j);
    if (j->p >= j->e || depth > 128) return nil;
    char c = *j->p;
    if (c == '{') {
        j->p++;
        NSMutableDictionary* d = [NSMutableDictionary dictionary];
        for (;;) {
            jskip(j);
            if (j->p < j->e && *j->p == '}') { j->p++; break; }
            NSString* k = jstring(j);
            if (!k) return nil;
            jskip(j);
            if (j->p >= j->e || *j->p != ':') return nil;
            j->p++;
            id v = jvalue(j, depth + 1);
            if (!v) return nil;
            [d setObject:v forKey:k];
            jskip(j);
            if (j->p < j->e && *j->p == ',') { j->p++; continue; }
            if (j->p < j->e && *j->p == '}') { j->p++; break; }
            return nil;
        }
        return j->mutableContainers ? (id)d : (id)[[d copy] autorelease];
    }
    if (c == '[') {
        j->p++;
        NSMutableArray* a = [NSMutableArray array];
        for (;;) {
            jskip(j);
            if (j->p < j->e && *j->p == ']') { j->p++; break; }
            id v = jvalue(j, depth + 1);
            if (!v) return nil;
            [a addObject:v];
            jskip(j);
            if (j->p < j->e && *j->p == ',') { j->p++; continue; }
            if (j->p < j->e && *j->p == ']') { j->p++; break; }
            return nil;
        }
        return j->mutableContainers ? (id)a : (id)[[a copy] autorelease];
    }
    if (c == '"') return jstring(j);
    if (j->e - j->p >= 4 && !strncmp(j->p, "true", 4)) { j->p += 4; return [NSNumber numberWithBool:YES]; }
    if (j->e - j->p >= 5 && !strncmp(j->p, "false", 5)) { j->p += 5; return [NSNumber numberWithBool:NO]; }
    if (j->e - j->p >= 4 && !strncmp(j->p, "null", 4)) { j->p += 4; return [NSNull null]; }
    if (c == '-' || isdigit((unsigned char)c)) {
        char buf[64];
        size_t k = 0;
        BOOL fl = NO;
        while (j->p < j->e && k < 63 && (isdigit((unsigned char)*j->p) || strchr("+-.eE", *j->p))) {
            if (strchr(".eE", *j->p)) fl = YES;
            buf[k++] = *j->p++;
        }
        buf[k] = 0;
        if (fl) return [NSNumber numberWithDouble:strtod(buf, NULL)];
        if (buf[0] != '-' && strlen(buf) >= 19) return [NSNumber numberWithUnsignedLongLong:strtoull(buf, NULL, 10)];
        return [NSNumber numberWithLongLong:strtoll(buf, NULL, 10)];
    }
    return nil;
}
static void jesc(NSMutableData* out, NSString* s) {
    const unsigned char* p = (const unsigned char*)[s UTF8String];
    [out appendBytes:"\"" length:1];
    for (; *p; p++) {
        char buf[8];
        switch (*p) {
        case '"': [out appendBytes:"\\\"" length:2]; break;
        case '\\': [out appendBytes:"\\\\" length:2]; break;
        case '\n': [out appendBytes:"\\n" length:2]; break;
        case '\r': [out appendBytes:"\\r" length:2]; break;
        case '\t': [out appendBytes:"\\t" length:2]; break;
        case '/': [out appendBytes:"\\/" length:2]; break;
        default:
            if (*p < 0x20) { snprintf(buf, sizeof buf, "\\u%04x", *p); [out appendBytes:buf length:6]; }
            else [out appendBytes:p length:1];
        }
    }
    [out appendBytes:"\"" length:1];
}
static BOOL jwrite(NSMutableData* out, id o, BOOL pretty, int ind) {
    if ([o isKindOfClass:[NSString class]]) { jesc(out, o); return YES; }
    if ([o isKindOfClass:[NSNull class]]) { [out appendBytes:"null" length:4]; return YES; }
    if ([o isKindOfClass:[NSNumber class]]) {
        if (o == (id)[NSNumber numberWithBool:YES]) { [out appendBytes:"true" length:4]; return YES; }
        if (o == (id)[NSNumber numberWithBool:NO]) { [out appendBytes:"false" length:5]; return YES; }
        const char* t = [o objCType];
        char buf[64];
        if (*t == 'f' || *t == 'd') {
            double d = [o doubleValue];
            if (d == floor(d) && fabs(d) < 1e15) snprintf(buf, sizeof buf, "%.0f", d);
            else snprintf(buf, sizeof buf, "%.17g", d);
        } else if (*t == 'Q') snprintf(buf, sizeof buf, "%llu", [o unsignedLongLongValue]);
        else snprintf(buf, sizeof buf, "%lld", [o longLongValue]);
        [out appendBytes:buf length:strlen(buf)];
        return YES;
    }
    if ([o isKindOfClass:[NSArray class]]) {
        [out appendBytes:"[" length:1];
        NSUInteger i = 0;
        for (id v in o) {
            if (i++) [out appendBytes:"," length:1];
            if (!jwrite(out, v, pretty, ind + 1)) return NO;
        }
        [out appendBytes:"]" length:1];
        return YES;
    }
    if ([o isKindOfClass:[NSDictionary class]]) {
        [out appendBytes:"{" length:1];
        NSUInteger i = 0;
        for (id k in o) {
            if (i++) [out appendBytes:"," length:1];
            jesc(out, [k description]);
            [out appendBytes:":" length:1];
            if (!jwrite(out, [o objectForKey:k], pretty, ind + 1)) return NO;
        }
        [out appendBytes:"}" length:1];
        return YES;
    }
    return NO;
}

@interface NSJSONSerialization : NSObject
@end
@implementation NSJSONSerialization
+ (id)JSONObjectWithData:(NSData*)d options:(NSUInteger)o error:(NSError**)e {
    if (!d) { if (e) *e = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:3840 userInfo:nil]; return nil; }
    jp j = {[d bytes], (const char*)[d bytes] + [d length], (o & 1) != 0};
    if (j.e - j.p >= 3 && (unsigned char)j.p[0] == 0xef && (unsigned char)j.p[1] == 0xbb) j.p += 3;
    id r = jvalue(&j, 0);
    if (!r && (o & 4)) {
        jskip(&j);
        r = nil;
    }
    if (!r && e) *e = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:3840 userInfo:nil];
    return r;
}
+ (id)JSONObjectWithStream:(id)s options:(NSUInteger)o error:(NSError**)e { (void)s; (void)o; if (e) *e = nil; return nil; }
+ (NSData*)dataWithJSONObject:(id)obj options:(NSUInteger)o error:(NSError**)e {
    NSMutableData* out = [NSMutableData data];
    if (!jwrite(out, obj, (o & 1) != 0, 0)) {
        if (e) *e = [NSError errorWithDomain:@"NSCocoaErrorDomain" code:3840 userInfo:nil];
        return nil;
    }
    return out;
}
+ (BOOL)isValidJSONObject:(id)o { return [o isKindOfClass:[NSArray class]] || [o isKindOfClass:[NSDictionary class]]; }
@end
