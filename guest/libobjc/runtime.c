// Guest Objective-C runtime: images, classes, selectors, method lookup.
#include "objc_private.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- host / dyld services ----
extern uint32_t _dyld_image_count(void);
extern const void* _dyld_get_image_header(uint32_t);
extern const char* _dyld_get_image_name(uint32_t);
extern uint8_t* getsectiondata(const void* mh, const char* seg, const char* sect, unsigned long* size);
extern const uint64_t* emu_objc_synthesized_classes(uint64_t* count);

static pthread_mutex_t g_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
void _objc_lock(void) { pthread_mutex_lock(&g_lock); }
void _objc_unlock(void) { pthread_mutex_unlock(&g_lock); }

static int g_verbose = 0;
void _objc_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[objc] %s\n", buf);
}

// ===================================================================== hashing
static uint64_t str_hash(const char* s) {
    uint64_t h = 1469598103934665603ull;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ull; }
    return h;
}

typedef struct strmap {
    const char** keys;
    void** vals;
    uint64_t cap, count;
} strmap;

static void* strmap_get(strmap* m, const char* k) {
    if (!m->cap) return NULL;
    uint64_t i = str_hash(k) & (m->cap - 1);
    while (m->keys[i]) {
        if (!strcmp(m->keys[i], k)) return m->vals[i];
        i = (i + 1) & (m->cap - 1);
    }
    return NULL;
}
static void strmap_put(strmap* m, const char* k, void* v);
static void strmap_grow(strmap* m) {
    uint64_t oc = m->cap;
    const char** ok = m->keys;
    void** ov = m->vals;
    m->cap = oc ? oc * 2 : 1024;
    m->keys = calloc(m->cap, sizeof(char*));
    m->vals = calloc(m->cap, sizeof(void*));
    m->count = 0;
    for (uint64_t i = 0; i < oc; i++)
        if (ok[i]) strmap_put(m, ok[i], ov[i]);
    free(ok);
    free(ov);
}
static void strmap_put(strmap* m, const char* k, void* v) {
    if ((m->count + 1) * 2 > m->cap) strmap_grow(m);
    uint64_t i = str_hash(k) & (m->cap - 1);
    while (m->keys[i]) {
        if (!strcmp(m->keys[i], k)) { m->vals[i] = v; return; }
        i = (i + 1) & (m->cap - 1);
    }
    m->keys[i] = k;
    m->vals[i] = v;
    m->count++;
}

// ===================================================================== selectors
static strmap g_sels;
SEL _sel_register(const char* name) {
    if (!name) return NULL;
    _objc_lock();
    SEL s = (SEL)strmap_get(&g_sels, name);
    if (!s) {
        s = (SEL)name;
        strmap_put(&g_sels, name, (void*)name);
    }
    _objc_unlock();
    return s;
}
SEL sel_registerName(const char* name) { return _sel_register(name); }
SEL sel_getUid(const char* name) { return _sel_register(name); }
const char* sel_getName(SEL s) { return s ? (const char*)s : "<null selector>"; }
BOOL sel_isEqual(SEL a, SEL b) { return a == b; }
BOOL sel_isMapped(SEL s) { return s != NULL; }

// ===================================================================== classes
static strmap g_classes;
static strmap g_protocols;

static inline bool cls_is_realized(Class c) { return (c->bits & 1) != 0; }
class_rt* _cls_rt(Class c) { return cls_is_realized(c) ? (class_rt*)(c->bits & ~(uintptr_t)7) : NULL; }
static inline const class_ro_t* cls_ro(Class c) {
    class_rt* rt = _cls_rt(c);
    return rt ? rt->ro : (const class_ro_t*)(c->bits & ~(uintptr_t)7);
}

static void rt_add_methods(class_rt* rt, const method_list_t* ml, bool front) {
    if (!ml || !ml->count) return;
    uint32_t n = ml->count;
    uint32_t es = ml->entsize_flags & 0xfffc;
    bool rel = (ml->entsize_flags & 0x80000000u) != 0;
    if (rt->nmethods + n > rt->capmethods) {
        rt->capmethods = (rt->nmethods + n) * 2 + 8;
        rt->methods = realloc(rt->methods, rt->capmethods * sizeof(method_t));
    }
    if (front) {
        memmove(rt->methods + n, rt->methods, rt->nmethods * sizeof(method_t));
    }
    method_t* dst = front ? rt->methods : rt->methods + rt->nmethods;
    const uint8_t* p = (const uint8_t*)(ml + 1);
    for (uint32_t i = 0; i < n; i++, p += es) {
        if (rel) {
            const int32_t* e = (const int32_t*)p;
            SEL* selref = (SEL*)((const char*)&e[0] + e[0]);
            dst[i].name = _sel_register((const char*)*selref);
            dst[i].types = (const char*)&e[1] + e[1];
            dst[i].imp = (IMP)((const char*)&e[2] + e[2]);
        } else {
            const method_t* m = (const method_t*)p;
            dst[i].name = _sel_register((const char*)m->name);
            dst[i].types = m->types;
            dst[i].imp = m->imp;
        }
    }
    rt->nmethods += n;
}

static void rt_add_protocols(class_rt* rt, const protocol_list_t* pl) {
    if (!pl) return;
    for (uintptr_t i = 0; i < pl->count; i++) {
        if (rt->nprotocols == rt->capprotocols) {
            rt->capprotocols = rt->capprotocols * 2 + 4;
            rt->protocols = realloc(rt->protocols, rt->capprotocols * sizeof(protocol_t*));
        }
        rt->protocols[rt->nprotocols++] = pl->list[i];
    }
}
static void rt_add_properties(class_rt* rt, const property_list_t* pl) {
    if (!pl) return;
    const uint8_t* p = (const uint8_t*)&pl->first;
    for (uint32_t i = 0; i < pl->count; i++, p += pl->entsize) {
        if (rt->nproperties == rt->capproperties) {
            rt->capproperties = rt->capproperties * 2 + 8;
            rt->properties = realloc(rt->properties, rt->capproperties * sizeof(property_t));
        }
        rt->properties[rt->nproperties++] = *(const property_t*)p;
    }
}

static bool has_method_named(class_rt* rt, const char* name) {
    SEL s = _sel_register(name);
    for (uint32_t i = 0; i < rt->nmethods; i++)
        if (rt->methods[i].name == s) return true;
    return false;
}

static void update_custom_rr(Class c, class_rt* rt) {
    if (rt->flags & RT_ROOT) return;  // the root class implements the defaults
    class_rt* srt = c->superclass ? _cls_rt(c->superclass) : NULL;
    if (srt && (srt->flags & RT_CUSTOM_RR)) { rt->flags |= RT_CUSTOM_RR; return; }
    if (has_method_named(rt, "retain") || has_method_named(rt, "release") || has_method_named(rt, "autorelease") ||
        has_method_named(rt, "retainCount") || has_method_named(rt, "_tryRetain"))
        rt->flags |= RT_CUSTOM_RR;
}

class_rt* _cls_realize(Class c) {
    if (!c) return NULL;
    if (cls_is_realized(c)) {
        class_rt* r = _cls_rt(c);
        if (__atomic_load_n(&r->flags, __ATOMIC_ACQUIRE) & RT_REALIZED) return r;
    }
    _objc_lock();
    // already realized, or being realized by this thread (root class <-> root metaclass cycle)
    if (cls_is_realized(c)) { _objc_unlock(); return _cls_rt(c); }
    const class_ro_t* ro = (const class_ro_t*)(c->bits & ~(uintptr_t)7);
    class_rt* rt = calloc(1, sizeof(class_rt));
    rt->ro = ro;
    rt->cls = c;
    rt->name = ro->name;
    c->bits = (uintptr_t)rt | 1;
    if (c->superclass) _cls_realize(c->superclass);
    if (c->isa && c->isa != c) _cls_realize(c->isa);
    rt->instance_size = ro->instanceSize;
    if (ro->flags & RO_META) rt->flags |= RT_META;
    if (ro->flags & RO_ROOT) rt->flags |= RT_ROOT;
    if (ro->flags & RO_HAS_CXX_STRUCTORS) rt->flags |= RT_CXX_CTOR | RT_CXX_DTOR;
    if (ro->flags & RO_HAS_CXX_DTOR_ONLY) rt->flags = (rt->flags & ~RT_CXX_CTOR) | RT_CXX_DTOR;
    if (c->superclass) {
        class_rt* srt = _cls_rt(c->superclass);
        if (srt && (srt->flags & RT_CXX_CTOR)) rt->flags |= RT_CXX_CTOR;
        if (srt && (srt->flags & RT_CXX_DTOR)) rt->flags |= RT_CXX_DTOR;
        // non-fragile ivars: slide when the superclass is larger than expected
        if (!(rt->flags & RT_META) && srt && srt->instance_size > ro->instanceStart) {
            int32_t diff = (int32_t)(srt->instance_size - ro->instanceStart);
            diff = (diff + 7) & ~7;
            if (ro->ivars) {
                const uint8_t* p = (const uint8_t*)&ro->ivars->first;
                for (uint32_t i = 0; i < ro->ivars->count; i++, p += ro->ivars->entsize) {
                    const ivar_t* iv = (const ivar_t*)p;
                    if (iv->offset) *iv->offset += diff;
                }
            }
            rt->instance_size += diff;
        }
    }
    rt_add_methods(rt, ro->baseMethods, false);
    rt_add_protocols(rt, ro->baseProtocols);
    rt_add_properties(rt, ro->baseProperties);
    if (!c->cache) c->cache = &_objc_empty_cache;
    update_custom_rr(c, rt);
    if (!(rt->flags & RT_META) && ro->name) {
        if (!strmap_get(&g_classes, ro->name)) strmap_put(&g_classes, ro->name, c);
        if (c->isa) {
            class_rt* mrt = _cls_rt(c->isa);
            if (mrt) mrt->nonmeta = c;
        }
    }
    if (g_verbose) _objc_log("realized %s%s size=%u", rt->flags & RT_META ? "+" : "", ro->name, rt->instance_size);
    __atomic_or_fetch(&rt->flags, RT_REALIZED, __ATOMIC_RELEASE);
    _objc_unlock();
    return rt;
}

// ===================================================================== caches
static cache_table* cache_alloc(uint64_t cap) {
    cache_table* t = calloc(1, sizeof(cache_table) + cap * sizeof(cache_bucket));
    t->mask = cap - 1;
    return t;
}
static void cache_insert(Class cls, SEL sel, IMP imp) {
    cache_table* t = cls->cache;
    if (!t || t == &_objc_empty_cache || (t->occupied + 1) * 4 > (t->mask + 1) * 3) {
        uint64_t cap = (!t || t == &_objc_empty_cache) ? 8 : (t->mask + 1) * 2;
        cache_table* n = cache_alloc(cap);
        if (t && t != &_objc_empty_cache) {
            for (uint64_t i = 0; i <= t->mask; i++) {
                if (!t->buckets[i].sel) continue;
                uint64_t j = ((uintptr_t)t->buckets[i].sel >> 3) & n->mask;
                while (n->buckets[j].sel) j = (j + 1) & n->mask;
                n->buckets[j] = t->buckets[i];
                n->occupied++;
            }
        }
        __atomic_store_n(&cls->cache, n, __ATOMIC_RELEASE);
        t = n;  // old table is leaked on purpose (other threads may still read it)
    }
    uint64_t j = ((uintptr_t)sel >> 3) & t->mask;
    while (t->buckets[j].sel) {
        if (t->buckets[j].sel == sel) return;
        j = (j + 1) & t->mask;
    }
    t->buckets[j].imp = imp;
    __atomic_store_n(&t->buckets[j].sel, sel, __ATOMIC_RELEASE);
    t->occupied++;
}
void _objc_flush_caches(Class cls) {
    (void)cls;
    // simplest correct behaviour: reset every realized class's cache
    _objc_lock();
    for (uint64_t i = 0; i < g_classes.cap; i++) {
        Class c = (Class)g_classes.vals[i];
        if (!c) continue;
        c->cache = &_objc_empty_cache;
        if (c->isa) c->isa->cache = &_objc_empty_cache;
    }
    _objc_unlock();
}

// ===================================================================== lookup
static IMP find_method_in(Class c, SEL sel) {
    class_rt* rt = _cls_realize(c);
    for (uint32_t i = 0; i < rt->nmethods; i++)
        if (rt->methods[i].name == sel) return rt->methods[i].imp;
    return NULL;
}
static method_t* find_method_entry(Class c, SEL sel) {
    class_rt* rt = _cls_realize(c);
    for (uint32_t i = 0; i < rt->nmethods; i++)
        if (rt->methods[i].name == sel) return &rt->methods[i];
    return NULL;
}

#include <emu/msg.h>
static SEL s_initialize, s_resolveInstanceMethod, s_resolveClassMethod, s_forwardingTarget, s_load, s_cxx_construct,
    s_cxx_destruct, s_dealloc, s_retain, s_release, s_autorelease;

static void init_selectors(void) {
    if (s_initialize) return;
    s_initialize = _sel_register("initialize");
    s_resolveInstanceMethod = _sel_register("resolveInstanceMethod:");
    s_resolveClassMethod = _sel_register("resolveClassMethod:");
    s_forwardingTarget = _sel_register("forwardingTargetForSelector:");
    s_load = _sel_register("load");
    s_cxx_construct = _sel_register(".cxx_construct");
    s_cxx_destruct = _sel_register(".cxx_destruct");
    s_dealloc = _sel_register("dealloc");
    s_retain = _sel_register("retain");
    s_release = _sel_register("release");
    s_autorelease = _sel_register("autorelease");
}

static void class_initialize(Class c) {
    class_rt* rt = _cls_realize(c);
    if (rt->flags & (RT_INITIALIZED | RT_INITIALIZING)) return;
    if (c->superclass) class_initialize(c->superclass);
    _objc_lock();
    if (rt->flags & (RT_INITIALIZED | RT_INITIALIZING)) { _objc_unlock(); return; }
    rt->flags |= RT_INITIALIZING;
    _objc_unlock();
    // +initialize is only sent if the class (or a superclass) implements it
    Class meta = c->isa;
    IMP init = NULL;
    for (Class k = meta; k; k = k->superclass) {
        init = find_method_in(k, s_initialize);
        if (init) break;
    }
    if (init) ((void (*)(id, SEL))init)((id)c, s_initialize);
    _objc_lock();
    rt->flags = (rt->flags & ~RT_INITIALIZING) | RT_INITIALIZED;
    _objc_unlock();
}

static void report_unrecognized(id receiver, Class cls, SEL sel) {
    static strmap reported;
    class_rt* rt = _cls_rt(cls);
    const char* cn = rt ? rt->name : "?";
    bool meta = rt && (rt->flags & RT_META);
    char key[512];
    snprintf(key, sizeof key, "%c[%s %s]", meta ? '+' : '-', cn, sel_getName(sel));
    _objc_lock();
    bool first = !strmap_get(&reported, key);
    if (first) strmap_put(&reported, strdup(key), (void*)1);
    _objc_unlock();
    if (first) _objc_log("unrecognized selector %s (receiver %p) - returning nil", key, receiver);
}

IMP _objc_lookup_imp(Class cls, SEL sel, id receiver, bool forward) {
    init_selectors();
    if (!cls) return (IMP)_objc_nil_imp;
    class_rt* rt = _cls_realize(cls);
    // +initialize
    if (receiver) {
        Class rc = receiver->isa;
        class_rt* rrt = _cls_realize(rc);
        Class nonmeta = (rrt->flags & RT_META) ? (Class)receiver : rc;
        class_rt* nrt = _cls_realize(nonmeta);
        if (!(nrt->flags & RT_INITIALIZED) && sel != s_initialize) class_initialize(nonmeta);
    }
    IMP imp = NULL;
    for (Class k = cls; k; k = k->superclass) {
        imp = find_method_in(k, sel);
        if (imp) break;
    }
    if (!imp && forward) {
        // dynamic resolution
        bool meta = (rt->flags & RT_META) != 0;
        Class target = meta ? rt->nonmeta : cls;
        if (target) {
            SEL rs = meta ? s_resolveClassMethod : s_resolveInstanceMethod;
            IMP r = NULL;
            for (Class k = target->isa; k; k = k->superclass) if ((r = find_method_in(k, rs))) break;
            if (r && ((BOOL(*)(id, SEL, SEL))r)((id)target, rs, sel)) {
                for (Class k = cls; k; k = k->superclass) if ((imp = find_method_in(k, sel))) break;
            }
        }
        if (!imp) {
            report_unrecognized(receiver, cls, sel);
            return (IMP)_objc_nil_imp;  // not cached: keeps reporting cheap & allows later class_addMethod
        }
    }
    if (imp) {
        _objc_lock();
        cache_insert(cls, sel, imp);
        _objc_unlock();
    }
    return imp;
}

// called from the msgSend slow path
IMP _objc_msg_lookup(id receiver, SEL sel, Class cls) { return _objc_lookup_imp(cls, sel, receiver, true); }

void _objc_forward_report(void) { _objc_log("_objc_msgForward called"); }

// ===================================================================== public class API
Class objc_getClass(const char* name) {
    if (!name) return Nil;
    _objc_lock();
    Class c = (Class)strmap_get(&g_classes, name);
    _objc_unlock();
    return c;
}
Class objc_lookUpClass(const char* name) { return objc_getClass(name); }
Class objc_getRequiredClass(const char* name) {
    Class c = objc_getClass(name);
    if (!c) _objc_log("objc_getRequiredClass: %s not found", name);
    return c;
}
Class objc_getMetaClass(const char* name) {
    Class c = objc_getClass(name);
    return c ? c->isa : Nil;
}
const char* class_getName(Class c) {
    if (!c) return "nil";
    return cls_ro(c)->name;
}
Class class_getSuperclass(Class c) { return c ? c->superclass : Nil; }
BOOL class_isMetaClass(Class c) { return c && (cls_ro(c)->flags & RO_META) ? YES : NO; }
size_t class_getInstanceSize(Class c) {
    if (!c) return 0;
    return _cls_realize(c)->instance_size;
}
Class object_getClass(id o) { return o ? o->isa : Nil; }
Class object_setClass(id o, Class c) {
    if (!o) return Nil;
    Class old = o->isa;
    o->isa = c;
    return old;
}
const char* object_getClassName(id o) { return class_getName(object_getClass(o)); }
BOOL object_isClass(id o) { return o && class_isMetaClass(o->isa); }

Method class_getInstanceMethod(Class c, SEL sel) {
    for (Class k = c; k; k = k->superclass) {
        method_t* m = find_method_entry(k, sel);
        if (m) return m;
    }
    return NULL;
}
Method class_getClassMethod(Class c, SEL sel) { return c ? class_getInstanceMethod(c->isa, sel) : NULL; }
IMP class_getMethodImplementation(Class c, SEL sel) {
    IMP i = _objc_lookup_imp(c, sel, NULL, false);
    return i ? i : (IMP)_objc_nil_imp;
}
BOOL class_respondsToSelector(Class c, SEL sel) { return c && _objc_lookup_imp(c, sel, NULL, false) ? YES : NO; }
IMP method_getImplementation(Method m) { return m ? m->imp : NULL; }
SEL method_getName(Method m) { return m ? m->name : NULL; }
const char* method_getTypeEncoding(Method m) { return m ? m->types : NULL; }
IMP method_setImplementation(Method m, IMP imp) {
    if (!m) return NULL;
    _objc_lock();
    IMP old = m->imp;
    m->imp = imp;
    _objc_unlock();
    _objc_flush_caches(Nil);
    return old;
}
void method_exchangeImplementations(Method a, Method b) {
    if (!a || !b) return;
    _objc_lock();
    IMP t = a->imp;
    a->imp = b->imp;
    b->imp = t;
    _objc_unlock();
    _objc_flush_caches(Nil);
}
BOOL class_addMethod(Class c, SEL sel, IMP imp, const char* types) {
    if (!c) return NO;
    class_rt* rt = _cls_realize(c);
    _objc_lock();
    for (uint32_t i = 0; i < rt->nmethods; i++)
        if (rt->methods[i].name == sel) { _objc_unlock(); return NO; }
    if (rt->nmethods == rt->capmethods) {
        rt->capmethods = rt->capmethods * 2 + 8;
        rt->methods = realloc(rt->methods, rt->capmethods * sizeof(method_t));
    }
    rt->methods[rt->nmethods].name = sel;
    rt->methods[rt->nmethods].types = types;
    rt->methods[rt->nmethods].imp = imp;
    rt->nmethods++;
    update_custom_rr(c, rt);
    _objc_unlock();
    _objc_flush_caches(c);
    return YES;
}
IMP class_replaceMethod(Class c, SEL sel, IMP imp, const char* types) {
    if (!c) return NULL;
    method_t* m = find_method_entry(c, sel);
    if (m) return method_setImplementation(m, imp);
    class_addMethod(c, sel, imp, types);
    return NULL;
}
Method* class_copyMethodList(Class c, unsigned int* out) {
    class_rt* rt = c ? _cls_realize(c) : NULL;
    unsigned n = rt ? rt->nmethods : 0;
    if (out) *out = n;
    if (!n) return NULL;
    Method* r = malloc((n + 1) * sizeof(Method));
    for (unsigned i = 0; i < n; i++) r[i] = &rt->methods[i];
    r[n] = NULL;
    return r;
}

Ivar class_getInstanceVariable(Class c, const char* name) {
    for (Class k = c; k; k = k->superclass) {
        const class_ro_t* ro = cls_ro(k);
        if (!ro->ivars) continue;
        const uint8_t* p = (const uint8_t*)&ro->ivars->first;
        for (uint32_t i = 0; i < ro->ivars->count; i++, p += ro->ivars->entsize) {
            ivar_t* iv = (ivar_t*)p;
            if (iv->name && !strcmp(iv->name, name)) return iv;
        }
    }
    return NULL;
}
ptrdiff_t ivar_getOffset(Ivar v) { return v && v->offset ? *v->offset : 0; }
const char* ivar_getName(Ivar v) { return v ? v->name : NULL; }
const char* ivar_getTypeEncoding(Ivar v) { return v ? v->type : NULL; }
id object_getIvar(id o, Ivar v) { return o && v ? *(id*)((char*)o + ivar_getOffset(v)) : nil; }
void object_setIvar(id o, Ivar v, id val) { if (o && v) *(id*)((char*)o + ivar_getOffset(v)) = val; }
void* object_getIndexedIvars(id o) { return o ? (char*)o + class_getInstanceSize(o->isa) : NULL; }
Ivar object_getInstanceVariable(id o, const char* name, void** out) {
    Ivar v = o ? class_getInstanceVariable(o->isa, name) : NULL;
    if (out) *out = v ? *(void**)((char*)o + ivar_getOffset(v)) : NULL;
    return v;
}

// ---- properties ----
typedef struct objc_property_attribute_t { const char* name; const char* value; } objc_property_attribute_t;
typedef property_t* objc_property_t;
objc_property_t class_getProperty(Class c, const char* name) {
    for (Class k = c; k; k = k->superclass) {
        class_rt* rt = _cls_realize(k);
        for (uint32_t i = 0; i < rt->nproperties; i++)
            if (!strcmp(rt->properties[i].name, name)) return &rt->properties[i];
    }
    return NULL;
}
objc_property_t* class_copyPropertyList(Class c, unsigned int* out) {
    class_rt* rt = c ? _cls_realize(c) : NULL;
    unsigned n = rt ? rt->nproperties : 0;
    if (out) *out = n;
    if (!n) return NULL;
    objc_property_t* r = malloc((n + 1) * sizeof(objc_property_t));
    for (unsigned i = 0; i < n; i++) r[i] = &rt->properties[i];
    r[n] = NULL;
    return r;
}
const char* property_getName(objc_property_t p) { return p ? p->name : NULL; }
const char* property_getAttributes(objc_property_t p) { return p ? p->attributes : NULL; }
objc_property_attribute_t* property_copyAttributeList(objc_property_t p, unsigned int* out) {
    if (out) *out = 0;
    if (!p || !p->attributes) return NULL;
    // "T@\"NSString\",&,N,V_name" -> entries {T, @"NSString"}, {&, ""}, ...
    const char* a = p->attributes;
    unsigned n = 1;
    for (const char* q = a; *q; q++) if (*q == ',') n++;
    size_t len = strlen(a);
    objc_property_attribute_t* r = malloc((n + 1) * sizeof(*r) + (len + 1) * 2 + n * 2);
    char* buf = (char*)(r + n + 1);
    unsigned k = 0;
    const char* q = a;
    while (*q) {
        const char* e = strchr(q, ',');
        if (!e) e = q + strlen(q);
        // name: first char; value: rest
        buf[0] = *q;
        buf[1] = 0;
        r[k].name = buf;
        buf += 2;
        size_t vl = (size_t)(e - q - 1);
        memcpy(buf, q + 1, vl);
        buf[vl] = 0;
        r[k].value = buf;
        buf += vl + 1;
        k++;
        q = *e ? e + 1 : e;
    }
    r[k].name = r[k].value = NULL;
    if (out) *out = k;
    return r;
}
char* property_copyAttributeValue(objc_property_t p, const char* name) {
    unsigned n = 0;
    objc_property_attribute_t* l = property_copyAttributeList(p, &n);
    char* r = NULL;
    for (unsigned i = 0; i < n; i++)
        if (!strcmp(l[i].name, name)) { r = strdup(l[i].value); break; }
    free(l);
    return r;
}
BOOL class_addProperty(Class c, const char* name, const objc_property_attribute_t* attrs, unsigned int n) {
    (void)attrs; (void)n;
    if (!c || class_getProperty(c, name)) return NO;
    class_rt* rt = _cls_realize(c);
    _objc_lock();
    if (rt->nproperties == rt->capproperties) {
        rt->capproperties = rt->capproperties * 2 + 8;
        rt->properties = realloc(rt->properties, rt->capproperties * sizeof(property_t));
    }
    rt->properties[rt->nproperties].name = strdup(name);
    rt->properties[rt->nproperties].attributes = "";
    rt->nproperties++;
    _objc_unlock();
    return YES;
}
void class_replaceProperty(Class c, const char* name, const objc_property_attribute_t* a, unsigned int n) { class_addProperty(c, name, a, n); }

// ---- protocols ----
typedef protocol_t Protocol;
Protocol* objc_getProtocol(const char* name) {
    _objc_lock();
    Protocol* p = strmap_get(&g_protocols, name);
    _objc_unlock();
    return p;
}
const char* protocol_getName(Protocol* p) { return p ? p->name : NULL; }
static bool proto_conforms(protocol_t* p, protocol_t* other) {
    if (!p) return false;
    if (p == other || !strcmp(p->name, other->name)) return true;
    if (p->protocols)
        for (uintptr_t i = 0; i < p->protocols->count; i++)
            if (proto_conforms(p->protocols->list[i], other)) return true;
    return false;
}
BOOL protocol_conformsToProtocol(Protocol* p, Protocol* o) { return p && o && proto_conforms(p, o) ? YES : NO; }
BOOL protocol_isEqual(Protocol* a, Protocol* b) { return a == b || (a && b && !strcmp(a->name, b->name)); }
BOOL class_conformsToProtocol(Class c, Protocol* proto) {
    if (!proto) return NO;
    for (Class k = c; k; k = k->superclass) {
        class_rt* rt = _cls_realize(k);
        for (uint32_t i = 0; i < rt->nprotocols; i++)
            if (proto_conforms(rt->protocols[i], proto)) return YES;
    }
    return NO;
}
BOOL class_addProtocol(Class c, Protocol* p) {
    if (!c || !p || class_conformsToProtocol(c, p)) return NO;
    class_rt* rt = _cls_realize(c);
    _objc_lock();
    protocol_list_t* pl = malloc(sizeof(protocol_list_t) + sizeof(protocol_t*));
    pl->count = 1;
    pl->list[0] = p;
    rt_add_protocols(rt, pl);
    _objc_unlock();
    return YES;
}
Protocol** class_copyProtocolList(Class c, unsigned int* out) {
    class_rt* rt = c ? _cls_realize(c) : NULL;
    unsigned n = rt ? rt->nprotocols : 0;
    if (out) *out = n;
    if (!n) return NULL;
    Protocol** r = malloc((n + 1) * sizeof(Protocol*));
    memcpy(r, rt->protocols, n * sizeof(Protocol*));
    r[n] = NULL;
    return r;
}
struct objc_method_description { SEL name; char* types; };
static struct objc_method_description find_in_mlist(const method_list_t* ml, SEL sel) {
    struct objc_method_description d = {NULL, NULL};
    if (!ml) return d;
    uint32_t es = ml->entsize_flags & 0xfffc;
    bool rel = (ml->entsize_flags & 0x80000000u) != 0;
    const uint8_t* p = (const uint8_t*)(ml + 1);
    for (uint32_t i = 0; i < ml->count; i++, p += es) {
        SEL n;
        const char* t;
        if (rel) {
            const int32_t* e = (const int32_t*)p;
            n = _sel_register((const char*)*(SEL*)((const char*)&e[0] + e[0]));
            t = (const char*)&e[1] + e[1];
        } else {
            const method_t* m = (const method_t*)p;
            n = _sel_register((const char*)m->name);
            t = m->types;
        }
        if (n == sel) { d.name = n; d.types = (char*)t; return d; }
    }
    return d;
}
struct objc_method_description protocol_getMethodDescription(Protocol* p, SEL sel, BOOL required, BOOL instance) {
    struct objc_method_description d = {NULL, NULL};
    if (!p) return d;
    const method_list_t* ml = required ? (instance ? p->instanceMethods : p->classMethods)
                                       : (instance ? p->optionalInstanceMethods : p->optionalClassMethods);
    d = find_in_mlist(ml, sel);
    if (!d.name && p->protocols)
        for (uintptr_t i = 0; i < p->protocols->count && !d.name; i++)
            d = protocol_getMethodDescription(p->protocols->list[i], sel, required, instance);
    return d;
}

// ---- dynamic class creation ----
Class objc_allocateClassPair(Class super, const char* name, size_t extra) {
    if (objc_getClass(name)) return Nil;
    Class cls = calloc(1, sizeof(struct objc_class) + extra);
    Class meta = calloc(1, sizeof(struct objc_class) + extra);
    class_ro_t* ro = calloc(1, sizeof(class_ro_t));
    class_ro_t* mro = calloc(1, sizeof(class_ro_t));
    char* n = strdup(name);
    ro->name = mro->name = n;
    uint32_t ss = super ? (uint32_t)class_getInstanceSize(super) : 8;
    ro->instanceStart = ro->instanceSize = ss;
    mro->flags = RO_META;
    mro->instanceStart = mro->instanceSize = sizeof(struct objc_class);
    if (!super) { ro->flags |= RO_ROOT; mro->flags |= RO_ROOT; }
    cls->isa = meta;
    cls->superclass = super;
    cls->cache = &_objc_empty_cache;
    cls->bits = (uintptr_t)ro;
    Class rootmeta = super ? super->isa->isa : meta;
    meta->isa = rootmeta;
    meta->superclass = super ? super->isa : cls;
    meta->cache = &_objc_empty_cache;
    meta->bits = (uintptr_t)mro;
    return cls;
}
void objc_registerClassPair(Class c) {
    _cls_realize(c->isa);
    _cls_realize(c);
}
Class objc_readClassPair(Class c, const void* info) {
    (void)info;
    objc_registerClassPair(c);
    return c;
}
Class objc_initializeClassPair(Class super, const char* name, Class cls, Class meta) {
    class_ro_t* ro = calloc(1, sizeof(class_ro_t));
    class_ro_t* mro = calloc(1, sizeof(class_ro_t));
    ro->name = mro->name = strdup(name);
    uint32_t ss = super ? (uint32_t)class_getInstanceSize(super) : 8;
    ro->instanceStart = ro->instanceSize = ss;
    mro->flags = RO_META;
    mro->instanceStart = mro->instanceSize = sizeof(struct objc_class);
    cls->isa = meta;
    cls->superclass = super;
    cls->cache = &_objc_empty_cache;
    cls->bits = (uintptr_t)ro;
    meta->isa = super ? super->isa->isa : meta;
    meta->superclass = super ? super->isa : cls;
    meta->cache = &_objc_empty_cache;
    meta->bits = (uintptr_t)mro;
    return cls;
}
BOOL class_addIvar(Class c, const char* name, size_t size, uint8_t align, const char* types) {
    (void)name; (void)types;
    if (!c || cls_is_realized(c)) return NO;
    class_ro_t* ro = (class_ro_t*)(c->bits & ~(uintptr_t)7);
    uint32_t a = 1u << align;
    ro->instanceSize = (ro->instanceSize + a - 1) & ~(a - 1);
    ro->instanceSize += (uint32_t)size;
    return YES;
}
void objc_disposeClassPair(Class c) { (void)c; }

int objc_getClassList(Class* buf, int n) {
    _objc_lock();
    int k = 0;
    for (uint64_t i = 0; i < g_classes.cap; i++) {
        if (!g_classes.vals[i]) continue;
        if (buf && k < n) buf[k] = (Class)g_classes.vals[i];
        k++;
    }
    _objc_unlock();
    return k;
}
Class* objc_copyClassList(unsigned int* out) {
    int n = objc_getClassList(NULL, 0);
    Class* r = malloc((n + 1) * sizeof(Class));
    n = objc_getClassList(r, n);
    r[n] = NULL;
    if (out) *out = n;
    return r;
}
const char** objc_copyClassNamesForImage(const char* image, unsigned int* out) {
    // used by crash reporters only
    (void)image;
    if (out) *out = 0;
    return calloc(1, sizeof(char*));
}
const char** objc_copyImageNames(unsigned int* out) { if (out) *out = 0; return calloc(1, sizeof(char*)); }
const char* class_getImageName(Class c) { (void)c; return "/usr/lib/libobjc.A.dylib"; }
int class_getVersion(Class c) { (void)c; return 0; }
void class_setVersion(Class c, int v) { (void)c; (void)v; }
const uint8_t* class_getIvarLayout(Class c) { return c ? cls_ro(c)->ivarLayout : NULL; }
const uint8_t* class_getWeakIvarLayout(Class c) { return c ? cls_ro(c)->weakIvarLayout : NULL; }
void class_setIvarLayout(Class c, const uint8_t* l) { (void)c; (void)l; }
void class_setWeakIvarLayout(Class c, const uint8_t* l) { (void)c; (void)l; }
Ivar* class_copyIvarList(Class c, unsigned int* out) {
    const class_ro_t* ro = c ? cls_ro(c) : NULL;
    unsigned n = ro && ro->ivars ? ro->ivars->count : 0;
    if (out) *out = n;
    if (!n) return NULL;
    Ivar* r = malloc((n + 1) * sizeof(Ivar));
    const uint8_t* p = (const uint8_t*)&ro->ivars->first;
    for (unsigned i = 0; i < n; i++, p += ro->ivars->entsize) r[i] = (Ivar)p;
    r[n] = NULL;
    return r;
}

// ===================================================================== images
typedef struct image_info {
    const void* mh;
    Class* classlist;
    unsigned long nclasses;
    Class* nlclasses;
    unsigned long nnlclasses;
    category_t** cats;
    unsigned long ncats;
    category_t** nlcats;
    unsigned long nnlcats;
} image_info;

static void* sect(const void* mh, const char* name, unsigned long* size) {
    unsigned long s = 0;
    void* p = getsectiondata(mh, "__DATA", name, &s);
    if (!p) p = getsectiondata(mh, "__DATA_CONST", name, &s);
    if (!p) p = getsectiondata(mh, "__DATA_DIRTY", name, &s);
    *size = p ? s : 0;
    return p;
}

static void attach_category(category_t* cat) {
    Class c = cat->cls;
    if (!c) return;
    class_rt* rt = _cls_realize(c);
    class_rt* mrt = _cls_realize(c->isa);
    _objc_lock();
    rt_add_methods(rt, cat->instanceMethods, true);
    rt_add_methods(mrt, cat->classMethods, true);
    rt_add_protocols(rt, cat->protocols);
    rt_add_properties(rt, cat->instanceProperties);
    update_custom_rr(c, rt);
    c->cache = &_objc_empty_cache;
    c->isa->cache = &_objc_empty_cache;
    _objc_unlock();
}

static void map_image(image_info* ii) {
    const void* mh = ii->mh;
    unsigned long n;
    // selectors
    SEL* selrefs = sect(mh, "__objc_selrefs", &n);
    for (unsigned long i = 0; i < n / 8; i++) selrefs[i] = _sel_register((const char*)selrefs[i]);
    // protocols
    protocol_t** protos = sect(mh, "__objc_protolist", &n);
    for (unsigned long i = 0; i < n / 8; i++) {
        protocol_t* p = protos[i];
        _objc_lock();
        protocol_t* ex = strmap_get(&g_protocols, p->name);
        if (!ex) strmap_put(&g_protocols, p->name, p);
        _objc_unlock();
    }
    protocol_t** prefs = sect(mh, "__objc_protorefs", &n);
    for (unsigned long i = 0; i < n / 8; i++) {
        protocol_t* canon = objc_getProtocol(prefs[i]->name);
        if (canon) prefs[i] = canon;
    }
    ii->classlist = sect(mh, "__objc_classlist", &ii->nclasses);
    ii->nclasses /= 8;
    ii->nlclasses = sect(mh, "__objc_nlclslist", &ii->nnlclasses);
    ii->nnlclasses /= 8;
    ii->cats = sect(mh, "__objc_catlist", &ii->ncats);
    ii->ncats /= 8;
    ii->nlcats = sect(mh, "__objc_nlcatlist", &ii->nnlcats);
    ii->nnlcats /= 8;
}

static void realize_image_classes(image_info* ii) {
    for (unsigned long i = 0; i < ii->nclasses; i++) {
        Class c = ii->classlist[i];
        if (!c) continue;
        _cls_realize(c);
    }
}

static void call_load(Class c) {
    class_rt* rt = _cls_realize(c);
    if (rt->flags & RT_LOADED) return;
    if (c->superclass) call_load(c->superclass);
    rt->flags |= RT_LOADED;
    // only the class's own +load (not inherited)
    class_rt* mrt = _cls_realize(c->isa);
    const class_ro_t* mro = mrt->ro;
    if (!mro->baseMethods) return;
    method_list_t* ml = mro->baseMethods;
    uint32_t es = ml->entsize_flags & 0xfffc;
    bool rel = (ml->entsize_flags & 0x80000000u) != 0;
    const uint8_t* p = (const uint8_t*)(ml + 1);
    for (uint32_t i = 0; i < ml->count; i++, p += es) {
        SEL n;
        IMP imp;
        if (rel) {
            const int32_t* e = (const int32_t*)p;
            n = _sel_register((const char*)*(SEL*)((const char*)&e[0] + e[0]));
            imp = (IMP)((const char*)&e[2] + e[2]);
        } else {
            n = _sel_register((const char*)((const method_t*)p)->name);
            imp = ((const method_t*)p)->imp;
        }
        if (n == s_load) {
            ((void (*)(id, SEL))imp)((id)c, s_load);
            return;
        }
    }
}

extern void _objc_blocks_init(void);
extern void _objc_root_init(void);

__attribute__((constructor)) static void objc_init(void) {
    init_selectors();
    g_verbose = getenv("OBJC_VERBOSE") != NULL;
    uint32_t count = _dyld_image_count();
    image_info* infos = calloc(count, sizeof(image_info));
    for (uint32_t i = 0; i < count; i++) {
        infos[i].mh = _dyld_get_image_header(i);
        map_image(&infos[i]);
    }
    // realize: runtime libraries first (superclasses live there)
    for (uint32_t i = count; i-- > 0;) realize_image_classes(&infos[i]);
    // let Foundation turn __CFConstantStringClassReference into a real class before any code runs
    extern void* dlsym(void*, const char*);
    void (*early)(void) = (void (*)(void))dlsym((void*)-2, "_emu_foundation_early_init");
    if (early) early();
    uint64_t nsyn = 0;
    const uint64_t* syn = emu_objc_synthesized_classes(&nsyn);
    for (uint64_t i = 0; i < nsyn; i++) _cls_realize((Class)syn[i]);
    _objc_blocks_init();
    _objc_root_init();
    for (uint32_t i = count; i-- > 0;)
        for (unsigned long k = 0; k < infos[i].ncats; k++) attach_category(infos[i].cats[k]);
    // +load: classes then categories
    for (uint32_t i = count; i-- > 0;)
        for (unsigned long k = 0; k < infos[i].nnlclasses; k++) call_load(infos[i].nlclasses[k]);
    for (uint32_t i = count; i-- > 0;)
        for (unsigned long k = 0; k < infos[i].nnlcats; k++) {
            category_t* cat = infos[i].nlcats[k];
            if (!cat->cls) continue;
            IMP imp = NULL;
            method_list_t* ml = cat->classMethods;
            if (ml) {
                struct objc_method_description d = find_in_mlist(ml, s_load);
                if (d.name) {
                    // fetch imp (find_in_mlist does not return it): search the flattened metaclass list
                    method_t* m = find_method_entry(cat->cls->isa, s_load);
                    imp = m ? m->imp : NULL;
                }
            }
            if (imp) ((void (*)(id, SEL))imp)((id)cat->cls, s_load);
        }
    free(infos);
    if (g_verbose) _objc_log("runtime initialized: %llu classes", (unsigned long long)g_classes.count);
}
