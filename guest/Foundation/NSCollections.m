// NSArray, NSMutableArray, NSDictionary, NSMutableDictionary, NSSet,
// NSMutableSet, NSEnumerator (concrete classes, manual retain/release).
#import <emu/Foundation.h>

#include <stdlib.h>
#include <string.h>

#include <emu/msg.h>

static unsigned long g_mutations_dummy;

// ===================================================================== enumerators
@interface __NSFastEnumerationEnumerator : NSEnumerator {
    id _coll;
    NSFastEnumerationState _st;
    id _buf[16];
    NSUInteger _n, _i;
}
- (instancetype)initWithCollection:(id)c;
@end
@implementation NSEnumerator
- (id)nextObject { return nil; }
- (NSArray*)allObjects {
    NSMutableArray* r = [NSMutableArray array];
    id o;
    while ((o = [self nextObject])) [r addObject:o];
    return r;
}
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState*)st objects:(id*)buf count:(NSUInteger)n {
    (void)n;
    st->mutationsPtr = &g_mutations_dummy;
    id o = [self nextObject];
    if (!o) return 0;
    buf[0] = o;
    st->itemsPtr = buf;
    return 1;
}
@end
@implementation __NSFastEnumerationEnumerator
- (instancetype)initWithCollection:(id)c {
    _coll = [c retain];
    memset(&_st, 0, sizeof _st);
    return self;
}
- (void)dealloc { [_coll release]; [super dealloc]; }
- (id)nextObject {
    if (_i >= _n) {
        _n = (NSUInteger)[_coll countByEnumeratingWithState:&_st objects:_buf count:16];
        _i = 0;
        if (!_n) return nil;
    }
    return _st.itemsPtr[_i++];
}
@end
@interface __NSArrayReverseEnumerator : NSEnumerator {
    NSArray* _a;
    NSUInteger _i;
}
@end
@implementation __NSArrayReverseEnumerator
- (instancetype)initWithArray:(NSArray*)a { _a = [a retain]; _i = [a count]; return self; }
- (void)dealloc { [_a release]; [super dealloc]; }
- (id)nextObject { return _i ? [_a objectAtIndex:--_i] : nil; }
@end

// ===================================================================== NSArray
@interface NSArray () {
@public
    id* _items;
    NSUInteger _count, _cap;
    unsigned long _mut;
    BOOL _mutable;
}
@end

@implementation NSArray
+ (id)allocWithZone:(NSZone*)z {
    NSArray* a = [super allocWithZone:z];
    a->_mutable = [self isSubclassOfClass:[NSMutableArray class]];
    return a;
}
+ (instancetype)array { return [[[self alloc] init] autorelease]; }
+ (instancetype)arrayWithObject:(id)o { return [[[self alloc] initWithObjects:&o count:1] autorelease]; }
+ (instancetype)arrayWithObjects:(const id*)objs count:(NSUInteger)n { return [[[self alloc] initWithObjects:objs count:n] autorelease]; }
+ (instancetype)arrayWithArray:(NSArray*)a { return [[[self alloc] initWithArray:a] autorelease]; }
+ (instancetype)arrayWithObjects:(id)first, ... {
    NSMutableArray* tmp = [[NSMutableArray alloc] init];
    va_list ap;
    va_start(ap, first);
    for (id o = first; o; o = va_arg(ap, id)) [tmp addObject:o];
    va_end(ap);
    id r = [[[self alloc] initWithArray:tmp] autorelease];
    [tmp release];
    return r;
}
+ (instancetype)arrayWithContentsOfFile:(NSString*)p {
    id o = _emu_plist_parse([NSData dataWithContentsOfFile:p]);
    return [o isKindOfClass:[NSArray class]] ? [self arrayWithArray:o] : nil;
}
- (instancetype)init { return [self initWithObjects:NULL count:0]; }
- (instancetype)initWithObjects:(const id*)objs count:(NSUInteger)n {
    _cap = n ? n : 4;
    _items = malloc(_cap * sizeof(id));
    for (NSUInteger i = 0; i < n; i++) _items[i] = [objs[i] retain];
    _count = n;
    return self;
}
- (instancetype)initWithObjects:(id)first, ... {
    NSMutableArray* tmp = [[NSMutableArray alloc] init];
    va_list ap;
    va_start(ap, first);
    for (id o = first; o; o = va_arg(ap, id)) [tmp addObject:o];
    va_end(ap);
    id r = [self initWithArray:tmp];
    [tmp release];
    return r;
}
- (instancetype)initWithArray:(NSArray*)a {
    NSUInteger n = [a count];
    id* tmp = malloc((n + 1) * sizeof(id));
    for (NSUInteger i = 0; i < n; i++) tmp[i] = [a objectAtIndex:i];
    id r = [self initWithObjects:tmp count:n];
    free(tmp);
    return r;
}
- (instancetype)initWithArray:(NSArray*)a copyItems:(BOOL)c {
    if (!c) return [self initWithArray:a];
    NSUInteger n = [a count];
    id* tmp = malloc((n + 1) * sizeof(id));
    for (NSUInteger i = 0; i < n; i++) tmp[i] = [[a objectAtIndex:i] copy];
    id r = [self initWithObjects:tmp count:n];
    for (NSUInteger i = 0; i < n; i++) [tmp[i] release];
    free(tmp);
    return r;
}
- (instancetype)initWithCapacity:(NSUInteger)n { (void)n; return [self initWithObjects:NULL count:0]; }
- (void)dealloc {
    for (NSUInteger i = 0; i < _count; i++) [_items[i] release];
    free(_items);
    [super dealloc];
}
- (id)copyWithZone:(NSZone*)z {
    (void)z;
    if (!_mutable) return [self retain];
    return [[NSArray alloc] initWithObjects:_items count:_count];
}
- (id)mutableCopyWithZone:(NSZone*)z { (void)z; return [[NSMutableArray alloc] initWithObjects:_items count:_count]; }
- (NSUInteger)count { return _count; }
- (id)objectAtIndex:(NSUInteger)i {
    if (i >= _count) {
        NSLog(@"*** -[NSArray objectAtIndex:]: index %lu beyond bounds [0 .. %ld]", (unsigned long)i, (long)_count - 1);
        return nil;
    }
    return _items[i];
}
- (id)objectAtIndexedSubscript:(NSUInteger)i { return [self objectAtIndex:i]; }
- (id)firstObject { return _count ? _items[0] : nil; }
- (id)lastObject { return _count ? _items[_count - 1] : nil; }
- (void)getObjects:(id*)buf range:(NSRange)r { memcpy(buf, _items + r.location, r.length * sizeof(id)); }
- (NSUInteger)indexOfObject:(id)o {
    for (NSUInteger i = 0; i < _count; i++) if ([_items[i] isEqual:o]) return i;
    return (NSUInteger)NSNotFound;
}
- (NSUInteger)indexOfObjectIdenticalTo:(id)o {
    for (NSUInteger i = 0; i < _count; i++) if (_items[i] == o) return i;
    return (NSUInteger)NSNotFound;
}
- (BOOL)containsObject:(id)o { return [self indexOfObject:o] != (NSUInteger)NSNotFound; }
- (BOOL)isEqualToArray:(NSArray*)o {
    if (o == self) return YES;
    if ([o count] != _count) return NO;
    for (NSUInteger i = 0; i < _count; i++) if (![_items[i] isEqual:[o objectAtIndex:i]]) return NO;
    return YES;
}
- (BOOL)isEqual:(id)o { return [o isKindOfClass:[NSArray class]] && [self isEqualToArray:o]; }
- (NSUInteger)hash { return _count; }
- (NSArray*)arrayByAddingObject:(id)o {
    NSMutableArray* m = [[self mutableCopy] autorelease];
    [m addObject:o];
    return m;
}
- (NSArray*)arrayByAddingObjectsFromArray:(NSArray*)a {
    NSMutableArray* m = [[self mutableCopy] autorelease];
    [m addObjectsFromArray:a];
    return m;
}
- (NSString*)componentsJoinedByString:(NSString*)s {
    NSMutableString* r = [NSMutableString string];
    for (NSUInteger i = 0; i < _count; i++) {
        if (i) [r appendString:s];
        [r appendString:[_items[i] description]];
    }
    return r;
}
- (NSString*)description {
    NSMutableString* r = [NSMutableString stringWithString:@"(\n"];
    for (NSUInteger i = 0; i < _count; i++) [r appendFormat:@"    %@%@\n", _items[i], i + 1 < _count ? @"," : @""];
    [r appendString:@")"];
    return r;
}
- (NSEnumerator*)objectEnumerator { return [[[__NSFastEnumerationEnumerator alloc] initWithCollection:self] autorelease]; }
- (NSEnumerator*)reverseObjectEnumerator { return [[[__NSArrayReverseEnumerator alloc] initWithArray:self] autorelease]; }
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState*)st objects:(id*)buf count:(NSUInteger)n {
    (void)buf; (void)n;
    if (st->state >= _count) return 0;
    st->mutationsPtr = &_mut;
    st->itemsPtr = _items + st->state;
    NSUInteger k = _count - st->state;
    st->state = _count;
    return k;
}
- (NSArray*)subarrayWithRange:(NSRange)r {
    if (NSMaxRange(r) > _count) return nil;
    return [NSArray arrayWithObjects:_items + r.location count:r.length];
}
static NSInteger sel_compare(id a, id b, void* ctx) { return (NSInteger)objc_msgSend(a, (SEL)ctx, b); }
static void merge_sort(id* a, NSUInteger n, NSInteger (*cmp)(id, id, void*), void* ctx, id* tmp) {
    if (n < 2) return;
    NSUInteger m = n / 2;
    merge_sort(a, m, cmp, ctx, tmp);
    merge_sort(a + m, n - m, cmp, ctx, tmp);
    NSUInteger i = 0, j = m, k = 0;
    while (i < m && j < n) tmp[k++] = cmp(a[i], a[j], ctx) <= 0 ? a[i++] : a[j++];
    while (i < m) tmp[k++] = a[i++];
    while (j < n) tmp[k++] = a[j++];
    memcpy(a, tmp, n * sizeof(id));
}
static NSInteger block_compare(id a, id b, void* ctx) { return ((NSComparator)ctx)(a, b); }
- (NSArray*)sortedArrayUsingFunction:(NSInteger (*)(id, id, void*))f context:(void*)ctx {
    NSMutableArray* m = [[self mutableCopy] autorelease];
    id* tmp = malloc((_count + 1) * sizeof(id));
    merge_sort(m->_items, m->_count, f, ctx, tmp);
    free(tmp);
    return m;
}
- (NSArray*)sortedArrayUsingSelector:(SEL)s { return [self sortedArrayUsingFunction:sel_compare context:(void*)s]; }
- (NSArray*)sortedArrayUsingComparator:(NSComparator)c { return [self sortedArrayUsingFunction:block_compare context:(void*)c]; }
- (NSArray*)sortedArrayWithOptions:(NSUInteger)o usingComparator:(NSComparator)c { (void)o; return [self sortedArrayUsingComparator:c]; }
- (NSArray*)sortedArrayUsingDescriptors:(NSArray*)d { (void)d; return self; }
- (void)makeObjectsPerformSelector:(SEL)s { for (NSUInteger i = 0; i < _count; i++) objc_msgSend(_items[i], s); }
- (void)makeObjectsPerformSelector:(SEL)s withObject:(id)o { for (NSUInteger i = 0; i < _count; i++) objc_msgSend(_items[i], s, o); }
- (void)enumerateObjectsUsingBlock:(void (^)(id, NSUInteger, BOOL*))b {
    BOOL stop = NO;
    for (NSUInteger i = 0; i < _count && !stop; i++) b(_items[i], i, &stop);
}
- (void)enumerateObjectsWithOptions:(NSUInteger)opt usingBlock:(void (^)(id, NSUInteger, BOOL*))b {
    BOOL stop = NO;
    if (opt & 2) { for (NSUInteger i = _count; i-- > 0 && !stop;) b(_items[i], i, &stop); }
    else for (NSUInteger i = 0; i < _count && !stop; i++) b(_items[i], i, &stop);
}
- (NSUInteger)indexOfObjectPassingTest:(BOOL (^)(id, NSUInteger, BOOL*))b {
    BOOL stop = NO;
    for (NSUInteger i = 0; i < _count && !stop; i++) if (b(_items[i], i, &stop)) return i;
    return (NSUInteger)NSNotFound;
}
- (NSArray*)filteredArrayUsingPredicate:(id)p {
    NSMutableArray* r = [NSMutableArray array];
    for (NSUInteger i = 0; i < _count; i++)
        if ([p respondsToSelector:@selector(evaluateWithObject:)] && ((BOOL(*)(id, SEL, id))objc_msgSend)(p, @selector(evaluateWithObject:), _items[i]))
            [r addObject:_items[i]];
    return r;
}
- (id)valueForKey:(NSString*)k {
    NSMutableArray* r = [NSMutableArray array];
    for (NSUInteger i = 0; i < _count; i++) {
        id v = [_items[i] valueForKey:k];
        [r addObject:v ? v : (id)[NSNull null]];
    }
    return r;
}
- (BOOL)writeToFile:(NSString*)p atomically:(BOOL)a { return [_emu_plist_xml(self) writeToFile:p atomically:a]; }
@end

@implementation NSMutableArray
+ (instancetype)arrayWithCapacity:(NSUInteger)n { return [[[self alloc] initWithCapacity:n] autorelease]; }
static void grow(NSArray* a, NSUInteger need) {
    if (need <= a->_cap) return;
    a->_cap = need * 2;
    a->_items = realloc(a->_items, a->_cap * sizeof(id));
}
- (void)addObject:(id)o {
    if (!o) { NSLog(@"*** -[NSMutableArray addObject:]: object cannot be nil"); return; }
    grow(self, _count + 1);
    _items[_count++] = [o retain];
    _mut++;
}
- (void)addObjectsFromArray:(NSArray*)a {
    NSUInteger n = [a count];
    grow(self, _count + n);
    for (NSUInteger i = 0; i < n; i++) _items[_count++] = [[a objectAtIndex:i] retain];
    _mut++;
}
- (void)insertObject:(id)o atIndex:(NSUInteger)i {
    if (!o || i > _count) return;
    grow(self, _count + 1);
    memmove(_items + i + 1, _items + i, (_count - i) * sizeof(id));
    _items[i] = [o retain];
    _count++;
    _mut++;
}
- (void)removeObjectAtIndex:(NSUInteger)i {
    if (i >= _count) return;
    id o = _items[i];
    memmove(_items + i, _items + i + 1, (_count - i - 1) * sizeof(id));
    _count--;
    _mut++;
    [o release];
}
- (void)removeObjectsInRange:(NSRange)r {
    for (NSUInteger k = 0; k < r.length; k++) [self removeObjectAtIndex:r.location];
}
- (void)removeObject:(id)o {
    for (NSUInteger i = _count; i-- > 0;) if ([_items[i] isEqual:o]) [self removeObjectAtIndex:i];
}
- (void)removeObjectIdenticalTo:(id)o {
    for (NSUInteger i = _count; i-- > 0;) if (_items[i] == o) [self removeObjectAtIndex:i];
}
- (void)removeObjectsInArray:(NSArray*)a { for (id o in a) [self removeObject:o]; }
- (void)removeLastObject { if (_count) [self removeObjectAtIndex:_count - 1]; }
- (void)removeAllObjects {
    NSUInteger n = _count;
    id* old = _items;
    _items = malloc(_cap * sizeof(id));
    _count = 0;
    _mut++;
    for (NSUInteger i = 0; i < n; i++) [old[i] release];
    free(old);
}
- (void)replaceObjectAtIndex:(NSUInteger)i withObject:(id)o {
    if (i >= _count || !o) return;
    id old = _items[i];
    _items[i] = [o retain];
    _mut++;
    [old release];
}
- (void)setObject:(id)o atIndexedSubscript:(NSUInteger)i {
    if (i == _count) [self addObject:o];
    else [self replaceObjectAtIndex:i withObject:o];
}
- (void)exchangeObjectAtIndex:(NSUInteger)a withObjectAtIndex:(NSUInteger)b {
    if (a >= _count || b >= _count) return;
    id t = _items[a];
    _items[a] = _items[b];
    _items[b] = t;
    _mut++;
}
- (void)setArray:(NSArray*)a {
    [self removeAllObjects];
    [self addObjectsFromArray:a];
}
- (void)sortUsingFunction:(NSInteger (*)(id, id, void*))f context:(void*)ctx {
    id* tmp = malloc((_count + 1) * sizeof(id));
    merge_sort(_items, _count, f, ctx, tmp);
    free(tmp);
    _mut++;
}
- (void)sortUsingSelector:(SEL)s { [self sortUsingFunction:sel_compare context:(void*)s]; }
- (void)sortUsingComparator:(NSComparator)c { [self sortUsingFunction:block_compare context:(void*)c]; }
@end

// ===================================================================== hash tables (dictionary & set)
typedef struct htab {
    id* keys;
    id* vals;      // NULL for sets
    NSUInteger* hashes;
    uint32_t* index;  // open addressing -> entry+1 (0 empty, 0xffffffff tombstone)
    NSUInteger n, cap, live, icap;
} htab;

static void ht_init(htab* t, BOOL withVals, NSUInteger cap) {
    memset(t, 0, sizeof *t);
    t->cap = cap < 4 ? 4 : cap;
    t->keys = calloc(t->cap, sizeof(id));
    if (withVals) t->vals = calloc(t->cap, sizeof(id));
    t->hashes = calloc(t->cap, sizeof(NSUInteger));
    t->icap = 8;
    while (t->icap < t->cap * 2) t->icap *= 2;
    t->index = calloc(t->icap, sizeof(uint32_t));
}
static void ht_free(htab* t) {
    for (NSUInteger i = 0; i < t->n; i++) {
        if (!t->keys[i]) continue;
        [t->keys[i] release];
        if (t->vals) [t->vals[i] release];
    }
    free(t->keys);
    free(t->vals);
    free(t->hashes);
    free(t->index);
}
static NSInteger ht_find(htab* t, id k, NSUInteger h) {
    NSUInteger m = t->icap - 1, i = h & m;
    for (NSUInteger probes = 0; probes < t->icap; probes++) {
        uint32_t e = t->index[i];
        if (!e) return -1;
        if (e != 0xffffffffu) {
            NSUInteger ei = e - 1;
            if (t->hashes[ei] == h && (t->keys[ei] == k || [t->keys[ei] isEqual:k])) return (NSInteger)ei;
        }
        i = (i + 1) & m;
    }
    return -1;
}
static void ht_rebuild(htab* t, NSUInteger newcap) {
    // compact entries + rebuild index
    NSUInteger k = 0;
    for (NSUInteger i = 0; i < t->n; i++) {
        if (!t->keys[i]) continue;
        t->keys[k] = t->keys[i];
        if (t->vals) t->vals[k] = t->vals[i];
        t->hashes[k] = t->hashes[i];
        k++;
    }
    t->n = k;
    if (newcap > t->cap) {
        t->cap = newcap;
        t->keys = realloc(t->keys, t->cap * sizeof(id));
        if (t->vals) t->vals = realloc(t->vals, t->cap * sizeof(id));
        t->hashes = realloc(t->hashes, t->cap * sizeof(NSUInteger));
    }
    while (t->icap < t->cap * 2) t->icap *= 2;
    free(t->index);
    t->index = calloc(t->icap, sizeof(uint32_t));
    NSUInteger m = t->icap - 1;
    for (NSUInteger i = 0; i < t->n; i++) {
        NSUInteger j = t->hashes[i] & m;
        while (t->index[j]) j = (j + 1) & m;
        t->index[j] = (uint32_t)(i + 1);
    }
}
// takes ownership of +1 key/value references
static void ht_put(htab* t, id k, id v) {
    NSUInteger h = [k hash];
    NSInteger e = ht_find(t, k, h);
    if (e >= 0) {
        [k release];
        if (t->vals) {
            id old = t->vals[e];
            t->vals[e] = v;
            [old release];
        } else [v release];
        return;
    }
    if (t->n + 1 > t->cap) ht_rebuild(t, t->live * 2 + 8);
    NSUInteger i = t->n++;
    t->keys[i] = k;
    if (t->vals) t->vals[i] = v;
    t->hashes[i] = h;
    t->live++;
    NSUInteger m = t->icap - 1, j = h & m;
    while (t->index[j] && t->index[j] != 0xffffffffu) j = (j + 1) & m;
    t->index[j] = (uint32_t)(i + 1);
}
static void ht_remove(htab* t, id k) {
    NSUInteger h = [k hash];
    NSInteger e = ht_find(t, k, h);
    if (e < 0) return;
    NSUInteger m = t->icap - 1, j = h & m;
    while (t->index[j] != (uint32_t)(e + 1)) j = (j + 1) & m;
    t->index[j] = 0xffffffffu;
    id ok = t->keys[e], ov = t->vals ? t->vals[e] : nil;
    t->keys[e] = nil;
    if (t->vals) t->vals[e] = nil;
    t->live--;
    [ok release];
    [ov release];
    if (t->n > 16 && t->live < t->n / 4) ht_rebuild(t, t->cap);
}
static void ht_clear(htab* t, BOOL vals) {
    htab old = *t;
    ht_init(t, vals, 4);
    ht_free(&old);
}

// ===================================================================== NSDictionary
@interface NSDictionary () {
@public
    htab _t;
    unsigned long _mut;
    BOOL _mutable;
}
@end

@implementation NSDictionary
+ (id)allocWithZone:(NSZone*)z {
    NSDictionary* d = [super allocWithZone:z];
    d->_mutable = [self isSubclassOfClass:[NSMutableDictionary class]];
    ht_init(&d->_t, YES, 4);
    return d;
}
+ (instancetype)dictionary { return [[[self alloc] init] autorelease]; }
+ (instancetype)dictionaryWithObject:(id)o forKey:(id)k { return [[[self alloc] initWithObjects:&o forKeys:&k count:1] autorelease]; }
+ (instancetype)dictionaryWithObjects:(const id*)o forKeys:(const id*)k count:(NSUInteger)n {
    return [[[self alloc] initWithObjects:o forKeys:k count:n] autorelease];
}
+ (instancetype)dictionaryWithObjects:(NSArray*)o forKeys:(NSArray*)k {
    return [[[self alloc] initWithObjects:o forKeys:k] autorelease];
}
+ (instancetype)dictionaryWithDictionary:(NSDictionary*)d { return [[[self alloc] initWithDictionary:d] autorelease]; }
+ (instancetype)dictionaryWithObjectsAndKeys:(id)first, ... {
    NSDictionary* d = [[[self alloc] init] autorelease];
    va_list ap;
    va_start(ap, first);
    for (id v = first; v; v = va_arg(ap, id)) {
        id k = va_arg(ap, id);
        if (!k) break;
        ht_put(&d->_t, [k copy], [v retain]);
    }
    va_end(ap);
    return d;
}
+ (instancetype)dictionaryWithContentsOfFile:(NSString*)p {
    NSData* data = [NSData dataWithContentsOfFile:p];
    if (!data) return nil;
    id o = _emu_plist_parse(data);
    return [o isKindOfClass:[NSDictionary class]] ? [self dictionaryWithDictionary:o] : nil;
}
+ (instancetype)dictionaryWithContentsOfURL:(NSURL*)u { return [self dictionaryWithContentsOfFile:(NSString*)[(id)u performSelector:@selector(path)]]; }
- (instancetype)init { return self; }
- (instancetype)initWithCapacity:(NSUInteger)n { (void)n; return self; }
- (instancetype)initWithObjects:(const id*)o forKeys:(const id*)k count:(NSUInteger)n {
    for (NSUInteger i = 0; i < n; i++) if (k[i] && o[i]) ht_put(&_t, [k[i] copy], [o[i] retain]);
    return self;
}
- (instancetype)initWithObjects:(NSArray*)o forKeys:(NSArray*)k {
    NSUInteger n = [o count];
    for (NSUInteger i = 0; i < n; i++) ht_put(&_t, [[k objectAtIndex:i] copy], [[o objectAtIndex:i] retain]);
    return self;
}
- (instancetype)initWithObjectsAndKeys:(id)first, ... {
    va_list ap;
    va_start(ap, first);
    for (id v = first; v; v = va_arg(ap, id)) {
        id k = va_arg(ap, id);
        if (!k) break;
        ht_put(&_t, [k copy], [v retain]);
    }
    va_end(ap);
    return self;
}
- (instancetype)initWithDictionary:(NSDictionary*)d {
    if ([d isKindOfClass:[NSDictionary class]]) {
        htab* s = &d->_t;
        for (NSUInteger i = 0; i < s->n; i++)
            if (s->keys[i]) ht_put(&_t, [s->keys[i] copy], [s->vals[i] retain]);
    }
    return self;
}
- (instancetype)initWithDictionary:(NSDictionary*)d copyItems:(BOOL)c {
    if (!c) return [self initWithDictionary:d];
    htab* s = &d->_t;
    for (NSUInteger i = 0; i < s->n; i++)
        if (s->keys[i]) ht_put(&_t, [s->keys[i] copy], [s->vals[i] copy]);
    return self;
}
- (instancetype)initWithContentsOfFile:(NSString*)p {
    NSDictionary* d = [NSDictionary dictionaryWithContentsOfFile:p];
    if (!d) { [self release]; return nil; }
    return [self initWithDictionary:d];
}
- (void)dealloc {
    ht_free(&_t);
    [super dealloc];
}
- (id)copyWithZone:(NSZone*)z {
    (void)z;
    if (!_mutable) return [self retain];
    return [[NSDictionary alloc] initWithDictionary:self];
}
- (id)mutableCopyWithZone:(NSZone*)z { (void)z; return [[NSMutableDictionary alloc] initWithDictionary:self]; }
- (NSUInteger)count { return _t.live; }
- (id)objectForKey:(id)k {
    if (!k) return nil;
    NSInteger e = ht_find(&_t, k, [k hash]);
    return e >= 0 ? _t.vals[e] : nil;
}
- (id)objectForKeyedSubscript:(id)k { return [self objectForKey:k]; }
- (id)valueForKey:(NSString*)k { return [self objectForKey:k]; }
- (NSArray*)allKeys {
    NSMutableArray* r = [NSMutableArray arrayWithCapacity:_t.live];
    for (NSUInteger i = 0; i < _t.n; i++) if (_t.keys[i]) [r addObject:_t.keys[i]];
    return r;
}
- (NSArray*)allValues {
    NSMutableArray* r = [NSMutableArray arrayWithCapacity:_t.live];
    for (NSUInteger i = 0; i < _t.n; i++) if (_t.keys[i]) [r addObject:_t.vals[i]];
    return r;
}
- (NSArray*)allKeysForObject:(id)o {
    NSMutableArray* r = [NSMutableArray array];
    for (NSUInteger i = 0; i < _t.n; i++) if (_t.keys[i] && [_t.vals[i] isEqual:o]) [r addObject:_t.keys[i]];
    return r;
}
- (NSArray*)objectsForKeys:(NSArray*)keys notFoundMarker:(id)m {
    NSMutableArray* r = [NSMutableArray array];
    for (id k in keys) { id v = [self objectForKey:k]; [r addObject:v ? v : m]; }
    return r;
}
- (NSEnumerator*)keyEnumerator { return [[self allKeys] objectEnumerator]; }
- (NSEnumerator*)objectEnumerator { return [[self allValues] objectEnumerator]; }
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState*)st objects:(id*)buf count:(NSUInteger)n {
    st->mutationsPtr = &_mut;
    NSUInteger k = 0;
    while (st->state < _t.n && k < n) {
        id key = _t.keys[st->state++];
        if (key) buf[k++] = key;
    }
    st->itemsPtr = buf;
    return k;
}
- (void)enumerateKeysAndObjectsUsingBlock:(void (^)(id, id, BOOL*))b {
    BOOL stop = NO;
    NSArray* keys = [self allKeys];
    for (id k in keys) {
        b(k, [self objectForKey:k], &stop);
        if (stop) break;
    }
}
- (void)enumerateKeysAndObjectsWithOptions:(NSUInteger)o usingBlock:(void (^)(id, id, BOOL*))b { (void)o; [self enumerateKeysAndObjectsUsingBlock:b]; }
- (NSArray*)keysSortedByValueUsingSelector:(SEL)s {
    (void)s;
    return [self allKeys];
}
- (BOOL)isEqualToDictionary:(NSDictionary*)o {
    if (o == self) return YES;
    if ([o count] != [self count]) return NO;
    for (NSUInteger i = 0; i < _t.n; i++) {
        if (!_t.keys[i]) continue;
        if (![_t.vals[i] isEqual:[o objectForKey:_t.keys[i]]]) return NO;
    }
    return YES;
}
- (BOOL)isEqual:(id)o { return [o isKindOfClass:[NSDictionary class]] && [self isEqualToDictionary:o]; }
- (NSUInteger)hash { return _t.live; }
- (NSString*)description {
    NSMutableString* r = [NSMutableString stringWithString:@"{\n"];
    for (NSUInteger i = 0; i < _t.n; i++)
        if (_t.keys[i]) [r appendFormat:@"    %@ = %@;\n", _t.keys[i], _t.vals[i]];
    [r appendString:@"}"];
    return r;
}
- (BOOL)writeToFile:(NSString*)p atomically:(BOOL)a { return [_emu_plist_xml(self) writeToFile:p atomically:a]; }
- (BOOL)writeToURL:(NSURL*)u atomically:(BOOL)a { return [self writeToFile:(NSString*)[(id)u performSelector:@selector(path)] atomically:a]; }
@end

@implementation NSMutableDictionary
+ (instancetype)dictionaryWithCapacity:(NSUInteger)n { return [[[self alloc] initWithCapacity:n] autorelease]; }
- (void)setObject:(id)o forKey:(id)k {
    if (!k) return;
    if (!o) { [self removeObjectForKey:k]; return; }
    ht_put(&_t, [k copy], [o retain]);
    _mut++;
}
- (void)setObject:(id)o forKeyedSubscript:(id)k { [self setObject:o forKey:k]; }
- (void)setValue:(id)o forKey:(NSString*)k { [self setObject:o forKey:k]; }
- (void)removeObjectForKey:(id)k {
    if (!k) return;
    ht_remove(&_t, k);
    _mut++;
}
- (void)removeObjectsForKeys:(NSArray*)keys { for (id k in keys) [self removeObjectForKey:k]; }
- (void)removeAllObjects {
    ht_clear(&_t, YES);
    _mut++;
}
- (void)addEntriesFromDictionary:(NSDictionary*)d {
    htab* s = &d->_t;
    for (NSUInteger i = 0; i < s->n; i++)
        if (s->keys[i]) ht_put(&_t, [s->keys[i] copy], [s->vals[i] retain]);
    _mut++;
}
- (void)setDictionary:(NSDictionary*)d {
    [self removeAllObjects];
    [self addEntriesFromDictionary:d];
}
@end

// ===================================================================== NSSet
@interface NSSet () {
@public
    htab _t;
    unsigned long _mut;
    BOOL _mutable;
}
@end

@implementation NSSet
+ (id)allocWithZone:(NSZone*)z {
    NSSet* s = [super allocWithZone:z];
    s->_mutable = [self isSubclassOfClass:[NSMutableSet class]];
    ht_init(&s->_t, NO, 4);
    return s;
}
+ (instancetype)set { return [[[self alloc] init] autorelease]; }
+ (instancetype)setWithObject:(id)o { return [[[self alloc] initWithObjects:&o count:1] autorelease]; }
+ (instancetype)setWithObjects:(const id*)o count:(NSUInteger)n { return [[[self alloc] initWithObjects:o count:n] autorelease]; }
+ (instancetype)setWithArray:(NSArray*)a { return [[[self alloc] initWithArray:a] autorelease]; }
+ (instancetype)setWithSet:(NSSet*)s { return [[[self alloc] initWithSet:s] autorelease]; }
+ (instancetype)setWithObjects:(id)first, ... {
    NSSet* s = [[[self alloc] init] autorelease];
    va_list ap;
    va_start(ap, first);
    for (id o = first; o; o = va_arg(ap, id)) ht_put(&s->_t, [o retain], nil);
    va_end(ap);
    return s;
}
- (instancetype)init { return self; }
- (instancetype)initWithCapacity:(NSUInteger)n { (void)n; return self; }
- (instancetype)initWithObjects:(const id*)o count:(NSUInteger)n {
    for (NSUInteger i = 0; i < n; i++) if (o[i]) ht_put(&_t, [o[i] retain], nil);
    return self;
}
- (instancetype)initWithArray:(NSArray*)a {
    for (id o in a) ht_put(&_t, [o retain], nil);
    return self;
}
- (instancetype)initWithSet:(NSSet*)s {
    for (NSUInteger i = 0; i < s->_t.n; i++) if (s->_t.keys[i]) ht_put(&_t, [s->_t.keys[i] retain], nil);
    return self;
}
- (void)dealloc { ht_free(&_t); [super dealloc]; }
- (id)copyWithZone:(NSZone*)z { (void)z; return _mutable ? [[NSSet alloc] initWithSet:self] : [self retain]; }
- (id)mutableCopyWithZone:(NSZone*)z { (void)z; return [[NSMutableSet alloc] initWithSet:self]; }
- (NSUInteger)count { return _t.live; }
- (id)member:(id)o {
    if (!o) return nil;
    NSInteger e = ht_find(&_t, o, [o hash]);
    return e >= 0 ? _t.keys[e] : nil;
}
- (BOOL)containsObject:(id)o { return [self member:o] != nil; }
- (id)anyObject {
    for (NSUInteger i = 0; i < _t.n; i++) if (_t.keys[i]) return _t.keys[i];
    return nil;
}
- (NSArray*)allObjects {
    NSMutableArray* r = [NSMutableArray array];
    for (NSUInteger i = 0; i < _t.n; i++) if (_t.keys[i]) [r addObject:_t.keys[i]];
    return r;
}
- (NSEnumerator*)objectEnumerator { return [[self allObjects] objectEnumerator]; }
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState*)st objects:(id*)buf count:(NSUInteger)n {
    st->mutationsPtr = &_mut;
    NSUInteger k = 0;
    while (st->state < _t.n && k < n) {
        id key = _t.keys[st->state++];
        if (key) buf[k++] = key;
    }
    st->itemsPtr = buf;
    return k;
}
- (BOOL)isSubsetOfSet:(NSSet*)o {
    for (NSUInteger i = 0; i < _t.n; i++) if (_t.keys[i] && ![o containsObject:_t.keys[i]]) return NO;
    return YES;
}
- (BOOL)isSupersetOfSet:(NSSet*)o { return [o isSubsetOfSet:self]; }
- (BOOL)intersectsSet:(NSSet*)o {
    for (NSUInteger i = 0; i < _t.n; i++) if (_t.keys[i] && [o containsObject:_t.keys[i]]) return YES;
    return NO;
}
- (BOOL)isEqualToSet:(NSSet*)o { return [o count] == [self count] && [self isSubsetOfSet:o]; }
- (BOOL)isEqual:(id)o { return [o isKindOfClass:[NSSet class]] && [self isEqualToSet:o]; }
- (NSUInteger)hash { return _t.live; }
- (NSSet*)setByAddingObject:(id)o {
    NSMutableSet* m = [[self mutableCopy] autorelease];
    [m addObject:o];
    return m;
}
- (void)makeObjectsPerformSelector:(SEL)s { for (id o in [self allObjects]) objc_msgSend(o, s); }
- (void)enumerateObjectsUsingBlock:(void (^)(id, BOOL*))b {
    BOOL stop = NO;
    for (id o in [self allObjects]) { b(o, &stop); if (stop) break; }
}
- (NSString*)description { return [NSString stringWithFormat:@"{(%@)}", [[self allObjects] componentsJoinedByString:@", "]]; }
@end

@implementation NSMutableSet
+ (instancetype)setWithCapacity:(NSUInteger)n { return [[[self alloc] initWithCapacity:n] autorelease]; }
- (void)addObject:(id)o { if (o) { ht_put(&_t, [o retain], nil); _mut++; } }
- (void)removeObject:(id)o { if (o) { ht_remove(&_t, o); _mut++; } }
- (void)removeAllObjects { ht_clear(&_t, NO); _mut++; }
- (void)addObjectsFromArray:(NSArray*)a { for (id o in a) [self addObject:o]; }
- (void)unionSet:(NSSet*)s { for (id o in [s allObjects]) [self addObject:o]; }
- (void)minusSet:(NSSet*)s { for (id o in [s allObjects]) [self removeObject:o]; }
- (void)intersectSet:(NSSet*)s {
    for (id o in [self allObjects]) if (![s containsObject:o]) [self removeObject:o];
}
- (void)setSet:(NSSet*)s { [self removeAllObjects]; [self unionSet:s]; }
@end

@interface NSCountedSet : NSMutableSet
@end
@implementation NSCountedSet
- (NSUInteger)countForObject:(id)o { return [self containsObject:o] ? 1 : 0; }
@end
