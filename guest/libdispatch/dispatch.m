// A small libdispatch implementation on top of pthreads.
//
// Every dispatch object is an Objective-C object (MCPE is compiled with
// OS_OBJECT_USE_OBJC=1 and ARC retains/releases them). Custom "concurrent"
// queues are executed serially, which keeps barrier semantics trivially
// correct; only the global queues run work items in parallel.
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct objc_class* Class;
extern id class_createInstance(Class, size_t);
extern Class objc_getClass(const char*);
extern id objc_retain(id);
extern void objc_release(id);
extern void* _Block_copy(const void*);
extern void _Block_release(const void*);
extern int pthread_main_np(void);
extern void* objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void*);
extern uint64_t mach_absolute_time(void);

#define EXPORT __attribute__((visibility("default")))

typedef void (*dispatch_function_t)(void*);
typedef uint64_t dispatch_time_t;
typedef void (^dispatch_block_t)(void);
#define DISPATCH_TIME_NOW 0ull
#define DISPATCH_TIME_FOREVER (~0ull)

enum { K_QUEUE = 1, K_SEMA, K_GROUP, K_SOURCE };
enum { Q_SERIAL = 0, Q_CONCURRENT = 1, Q_MAIN = 2, Q_GLOBAL = 3 };

typedef struct item {
    struct item* next;
    dispatch_function_t fn;
    void* ctx;
    void* block;  // copied block (released after run) or NULL
    struct sync_wait* waiter;
    struct dgroup* group;
} item;

typedef struct sync_wait {
    pthread_mutex_t m;
    pthread_cond_t c;
    int done;
} sync_wait;

struct dobj {
    Class isa;
    int kind;
    int _pad;
    void* ctx;
    dispatch_function_t finalizer;
};

typedef struct dqueue {
    struct dobj o;
    int type;
    int suspend;
    char label[64];
    pthread_mutex_t lock;
    pthread_cond_t cv;
    item* head;
    item* tail;
    int workers, idle, max_workers;
    struct { const void* key; void* val; } specific[8];
    int nspecific;
} dqueue;

typedef struct dsema {
    struct dobj o;
    long value;
    pthread_mutex_t m;
    pthread_cond_t c;
} dsema;

typedef struct dgroup {
    struct dobj o;
    long count;
    pthread_mutex_t m;
    pthread_cond_t c;
    item* notify;
} dgroup;

typedef struct dsource {
    struct dobj o;
    const void* type;
    dqueue* queue;
    void* handler;  // block
    dispatch_function_t handler_f;
    void* cancel_handler;
    uint64_t start, interval;
    uint64_t next;
    int suspended, cancelled, armed;
    unsigned long pending;
} dsource;

static Class g_cls_queue, g_cls_sema, g_cls_group, g_cls_source;

EXPORT dqueue _dispatch_main_q;
static dqueue g_global[4];
EXPORT char _dispatch_queue_attr_concurrent[64];
EXPORT char _dispatch_source_type_timer[64];
EXPORT char _dispatch_source_type_read[64];
EXPORT char _dispatch_source_type_write[64];
EXPORT char _dispatch_source_type_signal[64];
EXPORT char _dispatch_source_type_proc[64];
EXPORT char _dispatch_source_type_vnode[64];
EXPORT char _dispatch_source_type_mach_recv[64];
EXPORT char _dispatch_source_type_mach_send[64];
EXPORT char _dispatch_source_type_data_add[64];
EXPORT char _dispatch_source_type_data_or[64];
EXPORT char _dispatch_source_type_memorypressure[64];
EXPORT char _dispatch_data_empty[64];
EXPORT const void* _dispatch_data_destructor_free = (void*)1;
EXPORT const void* _dispatch_data_destructor_munmap = (void*)2;

static __thread dqueue* t_current_queue;

static void queue_init(dqueue* q, int type, const char* label) {
    q->o.kind = K_QUEUE;
    q->type = type;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cv, NULL);
    snprintf(q->label, sizeof q->label, "%s", label ? label : "");
    q->max_workers = type == Q_GLOBAL ? 6 : 1;
}

_Static_assert(sizeof(dqueue) <= 512 + 8, "dqueue too large");
_Static_assert(sizeof(dsource) <= 512 + 8, "dsource too large");
__attribute__((constructor)) static void dispatch_init(void) {
    g_cls_queue = objc_getClass("OS_dispatch_queue");
    g_cls_sema = objc_getClass("OS_dispatch_semaphore");
    g_cls_group = objc_getClass("OS_dispatch_group");
    g_cls_source = objc_getClass("OS_dispatch_source");
    queue_init(&_dispatch_main_q, Q_MAIN, "com.apple.main-thread");
    _dispatch_main_q.o.isa = g_cls_queue;
    static const char* names[4] = {"com.apple.root.background-qos", "com.apple.root.utility-qos", "com.apple.root.default-qos",
                                   "com.apple.root.user-initiated-qos"};
    for (int i = 0; i < 4; i++) {
        queue_init(&g_global[i], Q_GLOBAL, names[i]);
        g_global[i].o.isa = g_cls_queue;
    }
}

static void* alloc_obj(Class c, size_t size, int kind) {
    (void)size;
    struct dobj* o = (struct dobj*)class_createInstance(c, 0);
    o->kind = kind;
    return o;
}

// ===================================================================== workers
static void run_item(dqueue* q, item* it) {
    dqueue* prev = t_current_queue;
    t_current_queue = q;
    void* pool = objc_autoreleasePoolPush();
    it->fn(it->ctx);
    objc_autoreleasePoolPop(pool);
    t_current_queue = prev;
    if (it->block) _Block_release(it->block);
    if (it->waiter) {
        pthread_mutex_lock(&it->waiter->m);
        it->waiter->done = 1;
        pthread_cond_signal(&it->waiter->c);
        pthread_mutex_unlock(&it->waiter->m);
    }
    if (it->group) {
        extern void dispatch_group_leave(void*);
        dispatch_group_leave(it->group);
    }
    free(it);
}

static void* worker_main(void* arg) {
    dqueue* q = arg;
    char name[80];
    snprintf(name, sizeof name, "dispatch:%s", q->label);
    pthread_setname_np(name);
    pthread_mutex_lock(&q->lock);
    for (;;) {
        while (!q->head || q->suspend) {
            q->idle++;
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 5;
            int r = pthread_cond_timedwait(&q->cv, &q->lock, &ts);
            q->idle--;
            if (r && (!q->head || q->suspend)) {
                q->workers--;
                pthread_mutex_unlock(&q->lock);
                return NULL;
            }
        }
        item* it = q->head;
        q->head = it->next;
        if (!q->head) q->tail = NULL;
        pthread_mutex_unlock(&q->lock);
        run_item(q, it);
        pthread_mutex_lock(&q->lock);
    }
}

static void enqueue(dqueue* q, item* it) {
    it->next = NULL;
    pthread_mutex_lock(&q->lock);
    if (q->tail) q->tail->next = it;
    else q->head = it;
    q->tail = it;
    if (q->type != Q_MAIN && !q->suspend) {
        if (q->idle > 0) pthread_cond_signal(&q->cv);
        else if (q->workers < q->max_workers) {
            q->workers++;
            pthread_attr_t a;
            pthread_attr_init(&a);
            pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
            pthread_attr_setstacksize(&a, 1 << 20);
            pthread_t th;
            pthread_create(&th, &a, worker_main, q);
            pthread_attr_destroy(&a);
        }
    }
    pthread_mutex_unlock(&q->lock);
}

// Called from the main run loop
EXPORT void _dispatch_main_queue_drain(void) {
    dqueue* q = &_dispatch_main_q;
    pthread_mutex_lock(&q->lock);
    item* list = q->head;
    q->head = q->tail = NULL;
    pthread_mutex_unlock(&q->lock);
    while (list) {
        item* n = list->next;
        run_item(q, list);
        list = n;
    }
}
EXPORT int _dispatch_main_queue_pending(void) { return _dispatch_main_q.head != NULL; }

static void call_block(void* b) { ((dispatch_block_t)b)(); }

static item* mk_item(dispatch_function_t fn, void* ctx, void* block) {
    item* it = calloc(1, sizeof(item));
    it->fn = fn;
    it->ctx = ctx;
    it->block = block;
    return it;
}

// ===================================================================== queues API
EXPORT void* dispatch_get_global_queue(long prio, unsigned long flags) {
    (void)flags;
    switch (prio) {
    case -32768: case 0x09: return &g_global[0];  // background
    case -2: case 0x11: return &g_global[1];      // low / utility
    case 2: case 0x19: case 0x21: return &g_global[3];  // high / user-initiated / interactive
    default: return &g_global[2];
    }
}
EXPORT void* dispatch_get_main_queue(void) { return &_dispatch_main_q; }
EXPORT void* dispatch_get_current_queue(void) { return t_current_queue ? (void*)t_current_queue : (pthread_main_np() ? (void*)&_dispatch_main_q : (void*)&g_global[2]); }
EXPORT void* dispatch_queue_create(const char* label, void* attr) {
    dqueue* q = alloc_obj(g_cls_queue, sizeof(dqueue), K_QUEUE);
    queue_init(q, attr == _dispatch_queue_attr_concurrent ? Q_CONCURRENT : Q_SERIAL, label);
    return q;
}
EXPORT void* dispatch_queue_create_with_target(const char* label, void* attr, void* target) {
    (void)target;
    return dispatch_queue_create(label, attr);
}
EXPORT void* dispatch_queue_attr_make_with_qos_class(void* attr, unsigned qos, int rel) { (void)qos; (void)rel; return attr; }
EXPORT void* dispatch_queue_attr_make_initially_inactive(void* attr) { return attr; }
EXPORT const char* dispatch_queue_get_label(void* q) {
    if (!q) q = dispatch_get_current_queue();
    return ((dqueue*)q)->label;
}
EXPORT void dispatch_set_target_queue(void* o, void* q) { (void)o; (void)q; }
EXPORT void dispatch_activate(void* o) { (void)o; }

EXPORT void dispatch_async_f(void* q, void* ctx, dispatch_function_t fn) { enqueue(q, mk_item(fn, ctx, NULL)); }
EXPORT void dispatch_async(void* q, dispatch_block_t b) {
    void* c = _Block_copy(b);
    enqueue(q, mk_item(call_block, c, c));
}
EXPORT void dispatch_barrier_async_f(void* q, void* ctx, dispatch_function_t fn) { dispatch_async_f(q, ctx, fn); }
EXPORT void dispatch_barrier_async(void* q, dispatch_block_t b) { dispatch_async(q, b); }

EXPORT void dispatch_sync_f(void* qv, void* ctx, dispatch_function_t fn) {
    dqueue* q = qv;
    int direct = q->type == Q_GLOBAL || t_current_queue == q || (q->type == Q_MAIN && pthread_main_np());
    if (direct) {
        dqueue* prev = t_current_queue;
        t_current_queue = q;
        fn(ctx);
        t_current_queue = prev;
        return;
    }
    sync_wait w;
    pthread_mutex_init(&w.m, NULL);
    pthread_cond_init(&w.c, NULL);
    w.done = 0;
    item* it = mk_item(fn, ctx, NULL);
    it->waiter = &w;
    enqueue(q, it);
    pthread_mutex_lock(&w.m);
    while (!w.done) pthread_cond_wait(&w.c, &w.m);
    pthread_mutex_unlock(&w.m);
    pthread_mutex_destroy(&w.m);
    pthread_cond_destroy(&w.c);
}
EXPORT void dispatch_sync(void* q, dispatch_block_t b) { dispatch_sync_f(q, (void*)b, call_block); }
EXPORT void dispatch_barrier_sync_f(void* q, void* ctx, dispatch_function_t fn) { dispatch_sync_f(q, ctx, fn); }
EXPORT void dispatch_barrier_sync(void* q, dispatch_block_t b) { dispatch_sync(q, b); }
EXPORT void dispatch_async_and_wait(void* q, dispatch_block_t b) { dispatch_sync(q, b); }

EXPORT void dispatch_apply_f(size_t n, void* q, void* ctx, void (*fn)(void*, size_t)) {
    (void)q;
    for (size_t i = 0; i < n; i++) fn(ctx, i);
}
EXPORT void dispatch_apply(size_t n, void* q, void (^b)(size_t)) {
    (void)q;
    for (size_t i = 0; i < n; i++) b(i);
}

EXPORT void dispatch_suspend(void* o) {
    struct dobj* d = o;
    if (d->kind == K_QUEUE) {
        dqueue* q = o;
        pthread_mutex_lock(&q->lock);
        q->suspend++;
        pthread_mutex_unlock(&q->lock);
    } else if (d->kind == K_SOURCE) ((dsource*)o)->suspended++;
}
static void timer_arm(dsource* s);
EXPORT void dispatch_resume(void* o) {
    struct dobj* d = o;
    if (d->kind == K_QUEUE) {
        dqueue* q = o;
        pthread_mutex_lock(&q->lock);
        if (q->suspend > 0) q->suspend--;
        int kick = !q->suspend && q->head;
        pthread_mutex_unlock(&q->lock);
        if (kick && q->type != Q_MAIN) {
            // poke a worker
            pthread_mutex_lock(&q->lock);
            if (q->idle) pthread_cond_signal(&q->cv);
            else if (q->workers < q->max_workers) {
                q->workers++;
                pthread_t th;
                pthread_attr_t a;
                pthread_attr_init(&a);
                pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
                pthread_create(&th, &a, worker_main, q);
                pthread_attr_destroy(&a);
            }
            pthread_mutex_unlock(&q->lock);
        }
    } else if (d->kind == K_SOURCE) {
        dsource* s = o;
        if (s->suspended > 0) s->suspended--;
        if (!s->suspended) timer_arm(s);
    }
}
EXPORT void* dispatch_retain(void* o) { return objc_retain(o); }
EXPORT void dispatch_release(void* o) { objc_release(o); }
EXPORT void* dispatch_get_context(void* o) { return ((struct dobj*)o)->ctx; }
EXPORT void dispatch_set_context(void* o, void* c) { ((struct dobj*)o)->ctx = c; }
EXPORT void dispatch_set_finalizer_f(void* o, dispatch_function_t f) { ((struct dobj*)o)->finalizer = f; }
EXPORT void dispatch_queue_set_specific(void* qv, const void* key, void* val, dispatch_function_t dtor) {
    (void)dtor;
    dqueue* q = qv;
    pthread_mutex_lock(&q->lock);
    for (int i = 0; i < q->nspecific; i++)
        if (q->specific[i].key == key) { q->specific[i].val = val; pthread_mutex_unlock(&q->lock); return; }
    if (q->nspecific < 8) { q->specific[q->nspecific].key = key; q->specific[q->nspecific].val = val; q->nspecific++; }
    pthread_mutex_unlock(&q->lock);
}
EXPORT void* dispatch_queue_get_specific(void* qv, const void* key) {
    dqueue* q = qv;
    for (int i = 0; i < q->nspecific; i++) if (q->specific[i].key == key) return q->specific[i].val;
    return NULL;
}
EXPORT void* dispatch_get_specific(const void* key) {
    dqueue* q = dispatch_get_current_queue();
    return q ? dispatch_queue_get_specific(q, key) : NULL;
}
EXPORT void dispatch_main(void) {
    extern void _emu_runloop_iteration(double);
    for (;;) { _emu_runloop_iteration(0.01); usleep(1000); }
}

// ===================================================================== once
EXPORT void dispatch_once_f(long* pred, void* ctx, dispatch_function_t fn) {
    if (__atomic_load_n(pred, __ATOMIC_ACQUIRE) == ~0l) return;
    long expected = 0;
    if (__atomic_compare_exchange_n(pred, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        fn(ctx);
        __atomic_store_n(pred, ~0l, __ATOMIC_RELEASE);
        return;
    }
    while (__atomic_load_n(pred, __ATOMIC_ACQUIRE) != ~0l) usleep(10);
}
EXPORT void dispatch_once(long* pred, dispatch_block_t b) { dispatch_once_f(pred, (void*)b, call_block); }

// ===================================================================== time
static uint64_t mono_ns(void) { return mach_absolute_time(); }
static uint64_t wall_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
// absolute monotonic nanoseconds for a dispatch_time_t
static uint64_t to_mono(dispatch_time_t t) {
    if (t == DISPATCH_TIME_FOREVER) return ~0ull;
    if (t == DISPATCH_TIME_NOW) return mono_ns();
    if ((int64_t)t < 0) {  // wall clock
        uint64_t w = (uint64_t)(-(int64_t)t);
        uint64_t nw = wall_ns();
        return w <= nw ? mono_ns() : mono_ns() + (w - nw);
    }
    return t;
}
EXPORT dispatch_time_t dispatch_time(dispatch_time_t base, int64_t delta) {
    if (base == DISPATCH_TIME_FOREVER) return base;
    if ((int64_t)base < 0) {
        int64_t w = -(int64_t)base + delta;
        return (dispatch_time_t)(-w);
    }
    uint64_t b = base == DISPATCH_TIME_NOW ? mono_ns() : base;
    int64_t r = (int64_t)b + delta;
    return r < 1 ? 1 : (dispatch_time_t)r;
}
struct timespec_s { long tv_sec; long tv_nsec; };
EXPORT dispatch_time_t dispatch_walltime(const struct timespec_s* when, int64_t delta) {
    uint64_t w = when ? (uint64_t)when->tv_sec * 1000000000ull + (uint64_t)when->tv_nsec : wall_ns();
    int64_t r = (int64_t)w + delta;
    return (dispatch_time_t)(-r);
}

// ===================================================================== timers (dispatch_after + timer sources)
typedef struct timer_ent {
    struct timer_ent* next;
    uint64_t deadline;
    dqueue* q;
    item* it;       // one-shot (dispatch_after)
    dsource* src;   // repeating source
} timer_ent;
static pthread_mutex_t g_tlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_tcv = PTHREAD_COND_INITIALIZER;
static timer_ent* g_timers;
static int g_timer_thread;

static void source_fire(void* p);
static void* timer_main(void* a) {
    (void)a;
    pthread_setname_np("dispatch-timers");
    pthread_mutex_lock(&g_tlock);
    for (;;) {
        uint64_t now = mono_ns();
        timer_ent** pp = &g_timers;
        uint64_t next = now + 1000000000ull;
        while (*pp) {
            timer_ent* t = *pp;
            if (t->deadline <= now) {
                *pp = t->next;
                pthread_mutex_unlock(&g_tlock);
                if (t->it) enqueue(t->q, t->it);
                else if (t->src && !t->src->cancelled) {
                    dsource* s = t->src;
                    if (!s->suspended) {
                        __atomic_add_fetch(&s->pending, 1, __ATOMIC_RELAXED);
                        enqueue(s->queue, mk_item(source_fire, s, NULL));
                    }
                    s->armed = 0;
                    if (s->interval && s->interval != ~0ull && !s->cancelled && !s->suspended) {
                        pthread_mutex_lock(&g_tlock);
                        t->deadline = (t->deadline + s->interval > now) ? t->deadline + s->interval : now + s->interval;
                        t->next = g_timers;
                        g_timers = t;
                        s->armed = 1;
                        pthread_mutex_unlock(&g_tlock);
                        t = NULL;
                    } else {
                        objc_release((id)s);
                    }
                }
                if (t) free(t);
                pthread_mutex_lock(&g_tlock);
                pp = &g_timers;
                now = mono_ns();
                continue;
            }
            if (t->deadline < next) next = t->deadline;
            pp = &t->next;
        }
        uint64_t wait = next > now ? next - now : 0;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        uint64_t abs = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec + wait;
        ts.tv_sec = (long)(abs / 1000000000ull);
        ts.tv_nsec = (long)(abs % 1000000000ull);
        pthread_cond_timedwait(&g_tcv, &g_tlock, &ts);
    }
    return NULL;
}
static void timer_add(timer_ent* t) {
    pthread_mutex_lock(&g_tlock);
    t->next = g_timers;
    g_timers = t;
    if (!g_timer_thread) {
        g_timer_thread = 1;
        pthread_t th;
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
        pthread_create(&th, &a, timer_main, NULL);
        pthread_attr_destroy(&a);
    }
    pthread_cond_signal(&g_tcv);
    pthread_mutex_unlock(&g_tlock);
}
EXPORT void dispatch_after_f(dispatch_time_t when, void* q, void* ctx, dispatch_function_t fn) {
    timer_ent* t = calloc(1, sizeof(timer_ent));
    t->deadline = to_mono(when);
    t->q = q;
    t->it = mk_item(fn, ctx, NULL);
    if (t->deadline == ~0ull) { free(t->it); free(t); return; }
    timer_add(t);
}
EXPORT void dispatch_after(dispatch_time_t when, void* q, dispatch_block_t b) {
    void* c = _Block_copy(b);
    timer_ent* t = calloc(1, sizeof(timer_ent));
    t->deadline = to_mono(when);
    t->q = q;
    t->it = mk_item(call_block, c, c);
    if (t->deadline == ~0ull) { _Block_release(c); free(t->it); free(t); return; }
    timer_add(t);
}

// ===================================================================== sources
EXPORT void* dispatch_source_create(const void* type, uintptr_t handle, unsigned long mask, void* q) {
    (void)handle; (void)mask;
    dsource* s = alloc_obj(g_cls_source, sizeof(dsource), K_SOURCE);
    s->type = type;
    s->queue = q ? q : (dqueue*)dispatch_get_global_queue(0, 0);
    s->suspended = 1;  // sources start suspended
    return s;
}
EXPORT void dispatch_source_set_event_handler(void* sv, dispatch_block_t b) {
    dsource* s = sv;
    if (s->handler) _Block_release(s->handler);
    s->handler = b ? _Block_copy(b) : NULL;
}
EXPORT void dispatch_source_set_event_handler_f(void* sv, dispatch_function_t f) { ((dsource*)sv)->handler_f = f; }
EXPORT void dispatch_source_set_cancel_handler(void* sv, dispatch_block_t b) {
    dsource* s = sv;
    if (s->cancel_handler) _Block_release(s->cancel_handler);
    s->cancel_handler = b ? _Block_copy(b) : NULL;
}
EXPORT void dispatch_source_set_cancel_handler_f(void* sv, dispatch_function_t f) { (void)sv; (void)f; }
EXPORT void dispatch_source_set_registration_handler(void* sv, dispatch_block_t b) { (void)sv; (void)b; }
static void timer_arm(dsource* s) {
    if (s->type != _dispatch_source_type_timer || s->cancelled || s->armed || !s->start) return;
    s->armed = 1;
    objc_retain((id)s);
    timer_ent* t = calloc(1, sizeof(timer_ent));
    t->deadline = s->start;
    t->src = s;
    timer_add(t);
}
EXPORT void dispatch_source_set_timer(void* sv, dispatch_time_t start, uint64_t interval, uint64_t leeway) {
    (void)leeway;
    dsource* s = sv;
    s->start = to_mono(start);
    s->interval = interval;
    if (!s->suspended) timer_arm(s);
}
static void source_fire(void* p) {
    dsource* s = p;
    if (s->cancelled) return;
    if (s->handler) ((dispatch_block_t)s->handler)();
    else if (s->handler_f) s->handler_f(s->o.ctx);
    __atomic_store_n(&s->pending, 0, __ATOMIC_RELAXED);
}
EXPORT void dispatch_source_cancel(void* sv) {
    dsource* s = sv;
    if (s->cancelled) return;
    s->cancelled = 1;
    if (s->cancel_handler) {
        void* c = s->cancel_handler;
        s->cancel_handler = NULL;
        enqueue(s->queue, mk_item(call_block, c, c));
    }
}
EXPORT long dispatch_source_testcancel(void* sv) { return ((dsource*)sv)->cancelled; }
EXPORT unsigned long dispatch_source_get_data(void* sv) { unsigned long p = ((dsource*)sv)->pending; return p ? p : 1; }
EXPORT unsigned long dispatch_source_get_mask(void* sv) { (void)sv; return 0; }
EXPORT uintptr_t dispatch_source_get_handle(void* sv) { (void)sv; return 0; }
EXPORT void dispatch_source_merge_data(void* sv, unsigned long v) {
    dsource* s = sv;
    __atomic_add_fetch(&s->pending, v, __ATOMIC_RELAXED);
    if (!s->suspended && !s->cancelled) enqueue(s->queue, mk_item(source_fire, s, NULL));
}

// ===================================================================== semaphores
EXPORT void* dispatch_semaphore_create(long v) {
    if (v < 0) return NULL;
    dsema* s = alloc_obj(g_cls_sema, sizeof(dsema), K_SEMA);
    s->value = v;
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->c, NULL);
    return s;
}
EXPORT long dispatch_semaphore_signal(void* sv) {
    dsema* s = sv;
    pthread_mutex_lock(&s->m);
    s->value++;
    pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
    return 0;
}
static int wait_until(pthread_cond_t* c, pthread_mutex_t* m, dispatch_time_t t) {
    if (t == DISPATCH_TIME_FOREVER) return pthread_cond_wait(c, m);
    uint64_t dl = to_mono(t), now = mono_ns();
    if (dl <= now) return 60;  // ETIMEDOUT
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t abs = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec + (dl - now);
    ts.tv_sec = (long)(abs / 1000000000ull);
    ts.tv_nsec = (long)(abs % 1000000000ull);
    return pthread_cond_timedwait(c, m, &ts);
}
EXPORT long dispatch_semaphore_wait(void* sv, dispatch_time_t t) {
    dsema* s = sv;
    pthread_mutex_lock(&s->m);
    while (s->value <= 0) {
        if (wait_until(&s->c, &s->m, t) && s->value <= 0) {
            pthread_mutex_unlock(&s->m);
            return 1;  // timed out
        }
    }
    s->value--;
    pthread_mutex_unlock(&s->m);
    return 0;
}

// ===================================================================== groups
EXPORT void* dispatch_group_create(void) {
    dgroup* g = alloc_obj(g_cls_group, sizeof(dgroup), K_GROUP);
    pthread_mutex_init(&g->m, NULL);
    pthread_cond_init(&g->c, NULL);
    return g;
}
EXPORT void dispatch_group_enter(void* gv) {
    dgroup* g = gv;
    pthread_mutex_lock(&g->m);
    g->count++;
    pthread_mutex_unlock(&g->m);
}
EXPORT void dispatch_group_leave(void* gv) {
    dgroup* g = gv;
    item* notify = NULL;
    pthread_mutex_lock(&g->m);
    if (--g->count <= 0) {
        g->count = 0;
        notify = g->notify;
        g->notify = NULL;
        pthread_cond_broadcast(&g->c);
    }
    pthread_mutex_unlock(&g->m);
    while (notify) {
        item* n = notify->next;
        dqueue* q = (dqueue*)notify->group;
        notify->group = NULL;
        enqueue(q, notify);
        notify = n;
    }
}
EXPORT long dispatch_group_wait(void* gv, dispatch_time_t t) {
    dgroup* g = gv;
    pthread_mutex_lock(&g->m);
    while (g->count > 0) {
        if (wait_until(&g->c, &g->m, t) && g->count > 0) {
            pthread_mutex_unlock(&g->m);
            return 1;
        }
    }
    pthread_mutex_unlock(&g->m);
    return 0;
}
static void group_notify_item(dgroup* g, void* q, item* it) {
    pthread_mutex_lock(&g->m);
    if (g->count == 0) {
        pthread_mutex_unlock(&g->m);
        enqueue(q, it);
        return;
    }
    it->group = (dgroup*)q;  // reuse field to remember the target queue
    it->next = g->notify;
    g->notify = it;
    pthread_mutex_unlock(&g->m);
}
EXPORT void dispatch_group_notify_f(void* g, void* q, void* ctx, dispatch_function_t fn) { group_notify_item(g, q, mk_item(fn, ctx, NULL)); }
EXPORT void dispatch_group_notify(void* g, void* q, dispatch_block_t b) {
    void* c = _Block_copy(b);
    group_notify_item(g, q, mk_item(call_block, c, c));
}
EXPORT void dispatch_group_async_f(void* g, void* q, void* ctx, dispatch_function_t fn) {
    dispatch_group_enter(g);
    item* it = mk_item(fn, ctx, NULL);
    it->group = g;
    enqueue(q, it);
}
EXPORT void dispatch_group_async(void* g, void* q, dispatch_block_t b) {
    dispatch_group_enter(g);
    void* c = _Block_copy(b);
    item* it = mk_item(call_block, c, c);
    it->group = g;
    enqueue(q, it);
}

// ===================================================================== ObjC classes
typedef unsigned long NSUInteger;
__attribute__((objc_root_class)) @interface NSObject {
    Class isa;
}
+ (id)alloc;
- (void)dealloc;
@end
@interface OS_object : NSObject {
    uint8_t _payload[512];
}
@end
@implementation OS_object
- (void)dealloc {
    struct dobj* o = (struct dobj*)self;
    if (o->finalizer) o->finalizer(o->ctx);
    if (o->kind == K_SOURCE) {
        dsource* s = (dsource*)o;
        if (s->handler) _Block_release(s->handler);
        if (s->cancel_handler) _Block_release(s->cancel_handler);
    }
    [super dealloc];
}
@end
@interface OS_dispatch_object : OS_object
@end
@implementation OS_dispatch_object
@end
@interface OS_dispatch_queue : OS_dispatch_object
@end
@implementation OS_dispatch_queue
@end
@interface OS_dispatch_semaphore : OS_dispatch_object
@end
@implementation OS_dispatch_semaphore
@end
@interface OS_dispatch_group : OS_dispatch_object
@end
@implementation OS_dispatch_group
@end
@interface OS_dispatch_source : OS_dispatch_object
@end
@implementation OS_dispatch_source
@end
