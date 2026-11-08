// Object lifetime: allocation, retain/release, autorelease pools, weak
// references, associated objects, @synchronized and property accessors.
#include "objc_private.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emu/msg.h>

static SEL sel_retain_, sel_release_, sel_autorelease_, sel_dealloc_, sel_cxx_construct_, sel_cxx_destruct_, sel_copy_,
    sel_mutableCopy_;
static void sels(void) {
    if (sel_retain_) return;
    sel_retain_ = _sel_register("retain");
    sel_release_ = _sel_register("release");
    sel_autorelease_ = _sel_register("autorelease");
    sel_dealloc_ = _sel_register("dealloc");
    sel_cxx_construct_ = _sel_register(".cxx_construct");
    sel_cxx_destruct_ = _sel_register(".cxx_destruct");
    sel_copy_ = _sel_register("copy");
    sel_mutableCopy_ = _sel_register("mutableCopy");
}

static inline bool is_heap_obj(id o) { return objc_hdr(o)->magic == OBJ_MAGIC; }

// ===================================================================== allocation
static void cxx_construct(id obj, Class c) {
    if (!c) return;
    class_rt* rt = _cls_realize(c);
    if (!(rt->flags & RT_CXX_CTOR)) return;
    cxx_construct(obj, c->superclass);
    for (uint32_t i = 0; i < rt->nmethods; i++)
        if (rt->methods[i].name == sel_cxx_construct_) {
            ((id(*)(id, SEL))rt->methods[i].imp)(obj, sel_cxx_construct_);
            break;
        }
}
static void cxx_destruct(id obj, Class c) {
    for (; c; c = c->superclass) {
        class_rt* rt = _cls_realize(c);
        if (!(rt->flags & RT_CXX_DTOR)) return;
        for (uint32_t i = 0; i < rt->nmethods; i++)
            if (rt->methods[i].name == sel_cxx_destruct_) {
                ((void (*)(id, SEL))rt->methods[i].imp)(obj, sel_cxx_destruct_);
                break;
            }
    }
}

id class_createInstance(Class cls, size_t extra) {
    sels();
    if (!cls) return nil;
    class_rt* rt = _cls_realize(cls);
    size_t size = rt->instance_size + extra;
    if (size < 8) size = 8;
    char* mem = calloc(1, sizeof(obj_header) + size);
    if (!mem) return nil;
    obj_header* h = (obj_header*)mem;
    h->magic = OBJ_MAGIC;
    id obj = (id)(mem + sizeof(obj_header));
    obj->isa = cls;
    if (rt->flags & RT_CXX_CTOR) cxx_construct(obj, cls);
    return obj;
}
id class_createInstanceFromZone(Class cls, size_t extra, void* z) { (void)z; return class_createInstance(cls, extra); }
id objc_constructInstance(Class cls, void* bytes) {
    sels();
    if (!cls || !bytes) return nil;
    id obj = (id)bytes;
    obj->isa = cls;
    if (_cls_realize(cls)->flags & RT_CXX_CTOR) cxx_construct(obj, cls);
    return obj;
}
void* objc_destructInstance(id obj) {
    sels();
    if (!obj) return NULL;
    Class c = obj->isa;
    if (c && (_cls_realize(c)->flags & RT_CXX_DTOR)) cxx_destruct(obj, c);
    if (is_heap_obj(obj)) {
        obj_header* h = objc_hdr(obj);
        if (h->flags & OBJ_HAS_ASSOC) _objc_assoc_remove(obj);
        if (h->flags & OBJ_WEAKLY_REFERENCED) _objc_weak_clear(obj);
    }
    return obj;
}
id object_dispose(id obj) {
    if (!obj) return nil;
    objc_destructInstance(obj);
    if (is_heap_obj(obj)) {
        obj_header* h = objc_hdr(obj);
        h->magic = 0xdeadbeef;
        free(h);
    }
    return nil;
}
id object_copy(id obj, size_t extra) {
    if (!obj) return nil;
    size_t sz = class_getInstanceSize(obj->isa);
    id n = class_createInstance(obj->isa, extra);
    memcpy((char*)n + 8, (char*)obj + 8, sz > 8 ? sz - 8 : 0);
    return n;
}

// ===================================================================== retain / release
static inline bool custom_rr(id o) {
    class_rt* rt = _cls_rt(o->isa);
    if (!rt) rt = _cls_realize(o->isa);
    return (rt->flags & RT_CUSTOM_RR) || (rt->flags & RT_META);
}

id _objc_rootRetain(id o) {
    if (!o || !is_heap_obj(o)) return o;
    __atomic_add_fetch(&objc_hdr(o)->refcount, 1, __ATOMIC_RELAXED);
    return o;
}
bool _objc_rootReleaseWasZero(id o) {
    if (!o || !is_heap_obj(o)) return false;
    obj_header* h = objc_hdr(o);
    int64_t v = __atomic_sub_fetch(&h->refcount, 1, __ATOMIC_ACQ_REL);
    if (v < 0) {
        if (__atomic_fetch_or(&h->flags, OBJ_DEALLOCATING, __ATOMIC_ACQ_REL) & OBJ_DEALLOCATING) return false;
        return true;
    }
    return false;
}
void _objc_rootRelease(id o) {
    sels();
    if (_objc_rootReleaseWasZero(o)) objc_msgSend(o, sel_dealloc_);
}
uintptr_t _objc_rootRetainCount(id o) {
    if (!o || !is_heap_obj(o)) return (uintptr_t)-1 >> 1;
    return (uintptr_t)(objc_hdr(o)->refcount + 1);
}
bool _objc_rootIsDeallocating(id o) { return o && is_heap_obj(o) && (objc_hdr(o)->flags & OBJ_DEALLOCATING); }
bool _objc_rootTryRetain(id o) {
    if (!o) return false;
    if (!is_heap_obj(o)) return true;
    obj_header* h = objc_hdr(o);
    if (h->flags & OBJ_DEALLOCATING) return false;
    __atomic_add_fetch(&h->refcount, 1, __ATOMIC_RELAXED);
    return true;
}

id objc_retain(id o) {
    sels();
    if (!o) return o;
    if (custom_rr(o)) return objc_msgSend(o, sel_retain_);
    return _objc_rootRetain(o);
}
void objc_release(id o) {
    sels();
    if (!o) return;
    if (custom_rr(o)) { objc_msgSend(o, sel_release_); return; }
    _objc_rootRelease(o);
}

// ===================================================================== autorelease pools
typedef struct pool_page {
    id* objs;
    size_t count, cap;
} pool_page;
static pthread_key_t g_pool_key;
static pthread_once_t g_pool_once = PTHREAD_ONCE_INIT;
static void pool_drain_all(void* p);
static void pool_key_init(void) { pthread_key_create(&g_pool_key, pool_drain_all); }
static pool_page* pool_get(void) {
    pthread_once(&g_pool_once, pool_key_init);
    pool_page* p = pthread_getspecific(g_pool_key);
    if (!p) {
        p = calloc(1, sizeof(pool_page));
        pthread_setspecific(g_pool_key, p);
    }
    return p;
}
#define POOL_BOUNDARY ((id)0)
static void pool_add(id o) {
    pool_page* p = pool_get();
    if (p->count == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 256;
        p->objs = realloc(p->objs, p->cap * sizeof(id));
    }
    p->objs[p->count++] = o;
}
void* objc_autoreleasePoolPush(void) {
    pool_page* p = pool_get();
    pool_add(POOL_BOUNDARY);
    return (void*)(uintptr_t)(p->count);  // token: index after the boundary
}
void objc_autoreleasePoolPop(void* token) {
    pool_page* p = pool_get();
    size_t t = (size_t)(uintptr_t)token;
    if (t == 0 || t > p->count) return;  // stale token
    // release everything above the boundary (objects may autorelease more while we drain)
    while (p->count > t) {
        id o = p->objs[--p->count];
        if (o) objc_release(o);
    }
    p->count = t - 1;  // drop the boundary itself
}
static void pool_drain_all(void* v) {
    pool_page* p = v;
    while (p && p->count) {
        id o = p->objs[--p->count];
        if (o) objc_release(o);
    }
}
id objc_autorelease(id o) {
    sels();
    if (!o) return o;
    if (custom_rr(o)) return objc_msgSend(o, sel_autorelease_);
    pool_add(o);
    return o;
}
id _objc_rootAutorelease(id o) {
    if (o) pool_add(o);
    return o;
}
id objc_autoreleaseReturnValue(id o) { return objc_autorelease(o); }
id objc_retainAutoreleaseReturnValue(id o) { return objc_autorelease(objc_retain(o)); }
id objc_retainAutoreleasedReturnValue(id o) { return objc_retain(o); }
id objc_unsafeClaimAutoreleasedReturnValue(id o) { return o; }
id objc_claimAutoreleasedReturnValue(id o) { return objc_retain(o); }
id objc_retainAutorelease(id o) { return objc_autorelease(objc_retain(o)); }
void objc_storeStrong(id* loc, id v) {
    id old = *loc;
    if (old == v) return;
    objc_retain(v);
    *loc = v;
    objc_release(old);
}
void _objc_autoreleasePoolPrint(void) {}

extern void* _Block_copy(const void*);
id objc_retainBlock(id b) { return (id)_Block_copy(b); }

// ===================================================================== weak references
typedef struct weak_entry {
    id obj;
    id** slots;
    size_t n, cap;
} weak_entry;
static pthread_mutex_t g_weak_lock = PTHREAD_MUTEX_INITIALIZER;
static weak_entry* g_weak;
static size_t g_weak_cap, g_weak_count;

static size_t ptr_hash(const void* p) { uintptr_t x = (uintptr_t)p; x ^= x >> 17; x *= 0xed5ad4bbu; x ^= x >> 11; return (size_t)x; }
static weak_entry* weak_find(id o, bool create) {
    if (!g_weak_cap) {
        if (!create) return NULL;
        g_weak_cap = 256;
        g_weak = calloc(g_weak_cap, sizeof(weak_entry));
    }
    size_t i = ptr_hash(o) & (g_weak_cap - 1);
    size_t tomb = (size_t)-1;
    while (g_weak[i].obj || g_weak[i].slots) {
        if (g_weak[i].obj == o) return &g_weak[i];
        if (!g_weak[i].obj && tomb == (size_t)-1) tomb = i;
        i = (i + 1) & (g_weak_cap - 1);
    }
    if (!create) return NULL;
    if ((g_weak_count + 1) * 2 > g_weak_cap) {
        weak_entry* old = g_weak;
        size_t oc = g_weak_cap;
        g_weak_cap *= 2;
        g_weak = calloc(g_weak_cap, sizeof(weak_entry));
        g_weak_count = 0;
        for (size_t k = 0; k < oc; k++) {
            if (!old[k].obj) { free(old[k].slots); continue; }
            size_t j = ptr_hash(old[k].obj) & (g_weak_cap - 1);
            while (g_weak[j].obj || g_weak[j].slots) j = (j + 1) & (g_weak_cap - 1);
            g_weak[j] = old[k];
            g_weak_count++;
        }
        free(old);
        return weak_find(o, true);
    }
    if (tomb != (size_t)-1) i = tomb;
    free(g_weak[i].slots);
    g_weak[i].obj = o;
    g_weak[i].slots = NULL;
    g_weak[i].n = g_weak[i].cap = 0;
    g_weak_count++;
    return &g_weak[i];
}
static void weak_unregister(id o, id* slot) {
    weak_entry* e = weak_find(o, false);
    if (!e) return;
    for (size_t k = 0; k < e->n; k++)
        if (e->slots[k] == slot) { e->slots[k] = e->slots[--e->n]; break; }
}
static void weak_register(id o, id* slot) {
    weak_entry* e = weak_find(o, true);
    if (e->n == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 4;
        e->slots = realloc(e->slots, e->cap * sizeof(id*));
    }
    e->slots[e->n++] = slot;
    if (is_heap_obj(o)) __atomic_or_fetch(&objc_hdr(o)->flags, OBJ_WEAKLY_REFERENCED, __ATOMIC_RELAXED);
}
void _objc_weak_clear(id o) {
    pthread_mutex_lock(&g_weak_lock);
    weak_entry* e = weak_find(o, false);
    if (e) {
        for (size_t k = 0; k < e->n; k++)
            if (*e->slots[k] == o) *e->slots[k] = nil;
        e->n = 0;
        e->obj = nil;  // tombstone (slots != NULL keeps the probe chain)
        if (!e->slots) e->slots = malloc(1);
        g_weak_count--;
    }
    pthread_mutex_unlock(&g_weak_lock);
}
id objc_storeWeak(id* loc, id v) {
    pthread_mutex_lock(&g_weak_lock);
    id old = *loc;
    if (old) weak_unregister(old, loc);
    if (v && _objc_rootIsDeallocating(v)) v = nil;
    if (v) weak_register(v, loc);
    *loc = v;
    pthread_mutex_unlock(&g_weak_lock);
    return v;
}
id objc_initWeak(id* loc, id v) { *loc = nil; return objc_storeWeak(loc, v); }
id objc_initWeakOrNil(id* loc, id v) { return objc_initWeak(loc, v); }
id objc_storeWeakOrNil(id* loc, id v) { return objc_storeWeak(loc, v); }
void objc_destroyWeak(id* loc) { objc_storeWeak(loc, nil); }
id objc_loadWeakRetained(id* loc) {
    pthread_mutex_lock(&g_weak_lock);
    id o = *loc;
    if (o && !_objc_rootTryRetain(o)) o = nil;
    else if (o && custom_rr(o) && is_heap_obj(o)) { /* already retained via root */ }
    pthread_mutex_unlock(&g_weak_lock);
    return o;
}
id objc_loadWeak(id* loc) { return objc_autorelease(objc_loadWeakRetained(loc)); }
void objc_copyWeak(id* to, id* from) {
    id o = objc_loadWeakRetained(from);
    objc_initWeak(to, o);
    objc_release(o);
}
void objc_moveWeak(id* to, id* from) {
    objc_copyWeak(to, from);
    objc_destroyWeak(from);
}

// ===================================================================== associated objects
typedef struct assoc {
    id obj;
    const void* key;
    id value;
    uintptr_t policy;
    struct assoc* next;
} assoc;
static pthread_mutex_t g_assoc_lock = PTHREAD_MUTEX_INITIALIZER;
static assoc* g_assoc_buckets[1024];
void objc_setAssociatedObject(id obj, const void* key, id value, uintptr_t policy) {
    if (!obj) return;
    id nv = value;
    if (value) {
        if ((policy & 3) == 3) nv = objc_msgSend(value, _sel_register("copy"));
        else if ((policy & 3) == 1) nv = objc_retain(value);
    }
    id oldv = nil;
    uintptr_t oldp = 0;
    pthread_mutex_lock(&g_assoc_lock);
    size_t b = ptr_hash(obj) & 1023;
    assoc** pp = &g_assoc_buckets[b];
    for (; *pp; pp = &(*pp)->next)
        if ((*pp)->obj == obj && (*pp)->key == key) break;
    if (*pp) {
        oldv = (*pp)->value;
        oldp = (*pp)->policy;
        if (value) { (*pp)->value = nv; (*pp)->policy = policy; }
        else { assoc* d = *pp; *pp = d->next; free(d); }
    } else if (value) {
        assoc* a = calloc(1, sizeof(assoc));
        a->obj = obj; a->key = key; a->value = nv; a->policy = policy;
        a->next = g_assoc_buckets[b];
        g_assoc_buckets[b] = a;
        if (is_heap_obj(obj)) objc_hdr(obj)->flags |= OBJ_HAS_ASSOC;
    }
    pthread_mutex_unlock(&g_assoc_lock);
    if (oldv && (oldp & 3)) objc_release(oldv);
}
id objc_getAssociatedObject(id obj, const void* key) {
    id r = nil;
    pthread_mutex_lock(&g_assoc_lock);
    for (assoc* a = g_assoc_buckets[ptr_hash(obj) & 1023]; a; a = a->next)
        if (a->obj == obj && a->key == key) { r = a->value; break; }
    pthread_mutex_unlock(&g_assoc_lock);
    return r;
}
void _objc_assoc_remove(id obj) {
    assoc* dead = NULL;
    pthread_mutex_lock(&g_assoc_lock);
    assoc** pp = &g_assoc_buckets[ptr_hash(obj) & 1023];
    while (*pp) {
        if ((*pp)->obj == obj) { assoc* d = *pp; *pp = d->next; d->next = dead; dead = d; }
        else pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&g_assoc_lock);
    while (dead) {
        assoc* n = dead->next;
        if (dead->policy & 3) objc_release(dead->value);
        free(dead);
        dead = n;
    }
}
void objc_removeAssociatedObjects(id obj) { _objc_assoc_remove(obj); }

// ===================================================================== @synchronized
typedef struct sync_entry {
    id obj;
    pthread_mutex_t mtx;
    struct sync_entry* next;
} sync_entry;
static pthread_mutex_t g_sync_lock = PTHREAD_MUTEX_INITIALIZER;
static sync_entry* g_sync[256];
static pthread_mutex_t* sync_mutex(id obj) {
    pthread_mutex_lock(&g_sync_lock);
    size_t b = ptr_hash(obj) & 255;
    sync_entry* e = g_sync[b];
    for (; e; e = e->next) if (e->obj == obj) break;
    if (!e) {
        e = calloc(1, sizeof(sync_entry));
        e->obj = obj;
        pthread_mutexattr_t a;
        pthread_mutexattr_init(&a);
        pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&e->mtx, &a);
        pthread_mutexattr_destroy(&a);
        e->next = g_sync[b];
        g_sync[b] = e;
    }
    pthread_mutex_unlock(&g_sync_lock);
    return &e->mtx;
}
int objc_sync_enter(id obj) {
    if (!obj) return 0;
    pthread_mutex_lock(sync_mutex(obj));
    return 0;
}
int objc_sync_exit(id obj) {
    if (!obj) return 0;
    pthread_mutex_unlock(sync_mutex(obj));
    return 0;
}

// ===================================================================== property accessors
static pthread_mutex_t g_prop_lock = PTHREAD_MUTEX_INITIALIZER;
id objc_getProperty(id self, SEL _cmd, ptrdiff_t off, BOOL atomic) {
    (void)_cmd;
    if (!self) return nil;
    id* slot = (id*)((char*)self + off);
    if (!atomic) return *slot;
    pthread_mutex_lock(&g_prop_lock);
    id v = objc_retain(*slot);
    pthread_mutex_unlock(&g_prop_lock);
    return objc_autorelease(v);
}
static void set_prop(id self, ptrdiff_t off, id v, bool atomic, int copy) {
    sels();
    if (copy == 1) v = objc_msgSend(v, sel_copy_);
    else if (copy == 2) v = objc_msgSend(v, sel_mutableCopy_);
    else v = objc_retain(v);
    id* slot = (id*)((char*)self + off);
    if (atomic) pthread_mutex_lock(&g_prop_lock);
    id old = *slot;
    *slot = v;
    if (atomic) pthread_mutex_unlock(&g_prop_lock);
    objc_release(old);
}
void objc_setProperty(id self, SEL _cmd, ptrdiff_t off, id v, BOOL atomic, signed char shouldCopy) {
    (void)_cmd;
    set_prop(self, off, v, atomic, shouldCopy == 0 ? 0 : (shouldCopy == 2 ? 2 : 1));
}
void objc_setProperty_atomic(id self, SEL _cmd, id v, ptrdiff_t off) { (void)_cmd; set_prop(self, off, v, true, 0); }
void objc_setProperty_nonatomic(id self, SEL _cmd, id v, ptrdiff_t off) { (void)_cmd; set_prop(self, off, v, false, 0); }
void objc_setProperty_atomic_copy(id self, SEL _cmd, id v, ptrdiff_t off) { (void)_cmd; set_prop(self, off, v, true, 1); }
void objc_setProperty_nonatomic_copy(id self, SEL _cmd, id v, ptrdiff_t off) { (void)_cmd; set_prop(self, off, v, false, 1); }
void objc_copyStruct(void* dest, const void* src, ptrdiff_t size, BOOL atomic, BOOL strong) {
    (void)strong;
    if (atomic) pthread_mutex_lock(&g_prop_lock);
    memmove(dest, src, (size_t)size);
    if (atomic) pthread_mutex_unlock(&g_prop_lock);
}
void objc_copyCppObjectAtomic(void* dest, const void* src, void (*copyHelper)(void* dest, const void* source)) {
    pthread_mutex_lock(&g_prop_lock);
    copyHelper(dest, src);
    pthread_mutex_unlock(&g_prop_lock);
}

// ===================================================================== misc
void objc_enumerationMutation(id obj) {
    fprintf(stderr, "[objc] collection %p mutated while being enumerated\n", (void*)obj);
}
void objc_setEnumerationMutationHandler(void (*h)(id)) { (void)h; }
id objc_alloc(Class c) { return objc_msgSend((id)c, _sel_register("alloc")); }
id objc_allocWithZone(Class c) { return objc_msgSend((id)c, _sel_register("allocWithZone:"), (void*)0); }
id objc_alloc_init(Class c) { return objc_msgSend(objc_alloc(c), _sel_register("init")); }
id objc_opt_new(Class c) { return objc_msgSend((id)c, _sel_register("new")); }
id objc_opt_self(id o) { return o; }
Class objc_opt_class(id o) { return o ? (Class)objc_msgSend(o, _sel_register("class")) : Nil; }
BOOL objc_opt_isKindOfClass(id o, Class c) { return o ? (BOOL)(uintptr_t)objc_msgSend(o, _sel_register("isKindOfClass:"), c) : NO; }
BOOL objc_opt_respondsToSelector(id o, SEL s) { return o ? (BOOL)(uintptr_t)objc_msgSend(o, _sel_register("respondsToSelector:"), s) : NO; }
