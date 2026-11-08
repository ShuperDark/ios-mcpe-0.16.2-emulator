// libSystem: pthreads, OSAtomic, spinlocks, ulock, mach semaphores.
//
// Darwin's pthread objects live in guest memory. We embed Win32 SRW locks and
// condition variables directly inside them: those are pointer sized and a
// zeroed value means "unlocked", so even statically initialized Darwin objects
// (PTHREAD_MUTEX_INITIALIZER etc.) work without any lazy allocation.
#include "hle_common.h"
#include "profiler.h"
#include "macho.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <synchapi.h>

#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

#pragma comment(lib, "Synchronization.lib")

using namespace hlec;

namespace {

constexpr u64 MUTEX_SIG_INIT = 0x32AAABA7, MUTEX_SIG_ERRCHK_INIT = 0x32AAABA1, MUTEX_SIG_RECURSIVE_INIT = 0x32AAABA2,
              MUTEX_SIG_FIRSTFIT_INIT = 0x32AAABA3, MUTEX_SIG = 0x4D555458;
constexpr int PTHREAD_MUTEX_NORMAL = 0, PTHREAD_MUTEX_ERRORCHECK = 1, PTHREAD_MUTEX_RECURSIVE = 2;

struct GMutex {  // 64 bytes
    u64 sig;
    SRWLOCK srw;
    u32 owner;
    u32 count;
    u32 type;
    u32 inited;
    u8 pad[32];
};
static_assert(sizeof(GMutex) == 64);
struct GCond {  // 48 bytes
    u64 sig;
    CONDITION_VARIABLE cv;
    u8 pad[32];
};
static_assert(sizeof(GCond) == 48);
struct GRwlock {  // 200 bytes
    u64 sig;
    SRWLOCK srw;
    u32 writer;
    u8 pad[180];
};
static_assert(sizeof(GRwlock) == 200);

u32 my_tid() {
    auto* t = threads::current();
    return t ? t->id : 0xffffffff;
}

u32 mutex_type(GMutex* m) {
    if (!m->inited) {
        if (m->sig == MUTEX_SIG_RECURSIVE_INIT) m->type = PTHREAD_MUTEX_RECURSIVE;
        else if (m->sig == MUTEX_SIG_ERRCHK_INIT) m->type = PTHREAD_MUTEX_ERRORCHECK;
        // (racy but benign: every thread computes the same value)
        m->inited = 1;
    }
    return m->type;
}

int mutex_lock(GMutex* m) {
    u32 me = my_tid();
    u32 ty = mutex_type(m);
    if (ty != PTHREAD_MUTEX_NORMAL && m->owner == me) {
        if (ty == PTHREAD_MUTEX_RECURSIVE) { m->count++; return 0; }
        return D_EDEADLK;
    }
    AcquireSRWLockExclusive(&m->srw);
    m->owner = me;
    m->count = 1;
    return 0;
}
int mutex_trylock(GMutex* m) {
    u32 me = my_tid();
    u32 ty = mutex_type(m);
    if (ty == PTHREAD_MUTEX_RECURSIVE && m->owner == me) { m->count++; return 0; }
    if (!TryAcquireSRWLockExclusive(&m->srw)) return D_EBUSY;
    m->owner = me;
    m->count = 1;
    return 0;
}
int mutex_unlock(GMutex* m) {
    u32 ty = mutex_type(m);
    if (ty != PTHREAD_MUTEX_NORMAL && m->owner != my_tid()) return D_EPERM;
    if (ty == PTHREAD_MUTEX_RECURSIVE && m->count > 1) { m->count--; return 0; }
    m->owner = 0;
    m->count = 0;
    ReleaseSRWLockExclusive(&m->srw);
    return 0;
}

int cond_wait(GCond* c, GMutex* m, DWORD ms) {
    u32 saved = m->count;
    m->owner = 0;
    m->count = 0;
    BOOL ok = SleepConditionVariableSRW(&c->cv, &m->srw, ms, 0);
    m->owner = my_tid();
    m->count = saved ? saved : 1;
    if (!ok && GetLastError() == ERROR_TIMEOUT) return D_ETIMEDOUT;
    return 0;
}

u64 unix_now_ns() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    u64 t = (u64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return (t - 116444736000000000ull) * 100;
}

}  // namespace

// ---------------------------------------------------------------- mutex
HLE_FUNC(pthread_mutexattr_init, int, (u64* a)) { a[0] = 0x4D545841; a[1] = PTHREAD_MUTEX_NORMAL; return 0; }
HLE_FUNC(pthread_mutexattr_destroy, int, (u64*)) { return 0; }
HLE_FUNC(pthread_mutexattr_settype, int, (u64* a, int t)) { a[1] = (u64)t; return 0; }
HLE_FUNC(pthread_mutexattr_gettype, int, (u64* a, int* t)) { *t = (int)a[1]; return 0; }
HLE_FUNC(pthread_mutexattr_setpshared, int, (u64*, int)) { return 0; }
HLE_FUNC(pthread_mutexattr_setprotocol, int, (u64*, int)) { return 0; }
HLE_FUNC(pthread_mutexattr_setpolicy_np, int, (u64*, int)) { return 0; }
HLE_FUNC(pthread_mutex_init, int, (GMutex* m, const u64* attr)) {
    memset(m, 0, sizeof *m);
    m->sig = MUTEX_SIG;
    m->type = attr ? (u32)attr[1] : PTHREAD_MUTEX_NORMAL;
    m->inited = 1;
    return 0;
}
HLE_FUNC(pthread_mutex_destroy, int, (GMutex* m)) { m->sig = 0; return 0; }
HLE_FUNC(pthread_mutex_lock, int, (GMutex* m)) { return mutex_lock(m); }
HLE_FUNC(pthread_mutex_trylock, int, (GMutex* m)) { return mutex_trylock(m); }
HLE_FUNC(pthread_mutex_unlock, int, (GMutex* m)) { return mutex_unlock(m); }

// ---------------------------------------------------------------- cond
HLE_FUNC(pthread_condattr_init, int, (u64* a)) { a[0] = 0x434E4441; a[1] = 0; return 0; }
HLE_FUNC(pthread_condattr_destroy, int, (u64*)) { return 0; }
HLE_FUNC(pthread_condattr_setpshared, int, (u64*, int)) { return 0; }
HLE_FUNC(pthread_cond_init, int, (GCond* c, const void*)) { memset(c, 0, sizeof *c); c->sig = 0x434F4E44; return 0; }
HLE_FUNC(pthread_cond_destroy, int, (GCond*)) { return 0; }
HLE_FUNC(pthread_cond_signal, int, (GCond* c)) { WakeConditionVariable(&c->cv); return 0; }
HLE_FUNC(pthread_cond_broadcast, int, (GCond* c)) { WakeAllConditionVariable(&c->cv); return 0; }
HLE_FUNC(pthread_cond_wait, int, (GCond* c, GMutex* m)) { return cond_wait(c, m, INFINITE); }
HLE_FUNC(pthread_cond_timedwait, int, (GCond* c, GMutex* m, const d_timespec* abst)) {
    u64 deadline = u64(abst->tv_sec) * 1000000000ull + u64(abst->tv_nsec);
    u64 now = unix_now_ns();
    DWORD ms = deadline <= now ? 0 : (DWORD)std::min<u64>((deadline - now + 999999) / 1000000, 0xfffffffe);
    return cond_wait(c, m, ms);
}
HLE_FUNC(pthread_cond_timedwait_relative_np, int, (GCond* c, GMutex* m, const d_timespec* rel)) {
    u64 ns = u64(rel->tv_sec) * 1000000000ull + u64(rel->tv_nsec);
    return cond_wait(c, m, (DWORD)std::min<u64>((ns + 999999) / 1000000, 0xfffffffe));
}

// ---------------------------------------------------------------- rwlock
HLE_FUNC(pthread_rwlock_init, int, (GRwlock* r, const void*)) { memset(r, 0, sizeof *r); r->sig = 0x52574C4B; return 0; }
HLE_FUNC(pthread_rwlock_destroy, int, (GRwlock*)) { return 0; }
HLE_FUNC(pthread_rwlock_rdlock, int, (GRwlock* r)) { AcquireSRWLockShared(&r->srw); return 0; }
HLE_FUNC(pthread_rwlock_tryrdlock, int, (GRwlock* r)) { return TryAcquireSRWLockShared(&r->srw) ? 0 : D_EBUSY; }
HLE_FUNC(pthread_rwlock_wrlock, int, (GRwlock* r)) { AcquireSRWLockExclusive(&r->srw); r->writer = my_tid(); return 0; }
HLE_FUNC(pthread_rwlock_trywrlock, int, (GRwlock* r)) {
    if (!TryAcquireSRWLockExclusive(&r->srw)) return D_EBUSY;
    r->writer = my_tid();
    return 0;
}
HLE_FUNC(pthread_rwlock_unlock, int, (GRwlock* r)) {
    if (r->writer == my_tid()) { r->writer = 0; ReleaseSRWLockExclusive(&r->srw); }
    else ReleaseSRWLockShared(&r->srw);
    return 0;
}

// ---------------------------------------------------------------- once
HLE_FUNC_R(pthread_once, int, (u64* once, gaddr fn)) {
    // opaque[0..3] = state: 0 not run, 1 running, 2 done
    auto* st = reinterpret_cast<volatile long*>(&once[1]);
    if (*st == 2) return 0;
    if (_InterlockedCompareExchange(st, 1, 0) == 0) {
        g_cpu->call(fn, {});
        _InterlockedExchange(st, 2);
        WakeByAddressAll((void*)st);
        return 0;
    }
    while (*st != 2) {
        long v = 1;
        WaitOnAddress((void*)st, &v, sizeof v, 10);
    }
    return 0;
}

// ---------------------------------------------------------------- threads
HLE_FUNC(pthread_attr_init, int, (u64* a)) {
    memset(a, 0, 64);
    a[0] = 0x54484441;
    a[1] = 512 * 1024;  // stack size
    a[2] = 1;           // PTHREAD_CREATE_JOINABLE
    return 0;
}
HLE_FUNC(pthread_attr_destroy, int, (u64*)) { return 0; }
HLE_FUNC(pthread_attr_setstacksize, int, (u64* a, size_t s)) { a[1] = s; return 0; }
HLE_FUNC(pthread_attr_getstacksize, int, (u64* a, size_t* s)) { *s = a[1] ? a[1] : 512 * 1024; return 0; }
HLE_FUNC(pthread_attr_setdetachstate, int, (u64* a, int d)) { a[2] = (u64)d; return 0; }
HLE_FUNC(pthread_attr_getdetachstate, int, (u64* a, int* d)) { *d = a[2] ? (int)a[2] : 1; return 0; }
HLE_FUNC(pthread_attr_setschedparam, int, (u64*, const void*)) { return 0; }
HLE_FUNC(pthread_attr_getschedparam, int, (u64*, int* p)) { if (p) { p[0] = 31; p[1] = 0; } return 0; }
HLE_FUNC(pthread_attr_setschedpolicy, int, (u64*, int)) { return 0; }
HLE_FUNC(pthread_attr_getschedpolicy, int, (u64*, int* p)) { *p = 1; return 0; }
HLE_FUNC(pthread_attr_setscope, int, (u64*, int)) { return 0; }
HLE_FUNC(pthread_attr_setinheritsched, int, (u64*, int)) { return 0; }
HLE_FUNC(pthread_attr_setguardsize, int, (u64*, size_t)) { return 0; }
HLE_FUNC(pthread_attr_setstack, int, (u64*, void*, size_t)) { return 0; }

HLE_FUNC(pthread_create, int, (GuestThread** out, const u64* attr, gaddr fn, gaddr arg)) {
    u64 ss = attr && attr[1] ? attr[1] : 512 * 1024;
    if (ss < 256 * 1024) ss = 256 * 1024;
    bool det = attr && attr[2] == 2;  // PTHREAD_CREATE_DETACHED
    GuestThread* t = threads::spawn(fn, arg, ss, det);
    *out = t;
    LOGD("pthread_create -> thread %u fn=%s", t->id, macho::symbolicate(fn).c_str());
    return 0;
}
HLE_FUNC(pthread_join, int, (GuestThread* t, u64* ret)) {
    if (!t) return D_ESRCH;
    {
        std::unique_lock lk(t->mtx);
        t->cv.wait(lk, [&] { return t->finished.load(); });
    }
    if (t->host.joinable()) t->host.join();
    if (ret) *ret = t->result;
    return 0;
}
HLE_FUNC(pthread_detach, int, (GuestThread* t)) {
    if (!t) return D_ESRCH;
    t->detached = true;
    if (t->host.joinable()) t->host.detach();
    return 0;
}
HLE_FUNC(pthread_self, GuestThread*, ()) { return threads::current(); }
HLE_FUNC(pthread_equal, int, (u64 a, u64 b)) { return a == b; }
HLE_FUNC(pthread_exit, void, (u64 v)) {
    auto* t = threads::current();
    if (t) t->result = v;
    if (t && t->id == 1) {
        LOGI("main thread called pthread_exit");
        ExitProcess(0);
    }
    g_cpu->request_exit();
}
HLE_FUNC(pthread_mach_thread_np, u32, (GuestThread* t)) { return t ? 0x1000 + t->id : 0; }
HLE_FUNC(pthread_threadid_np, int, (GuestThread* t, u64* id)) {
    if (!t) t = threads::current();
    if (id) *id = t ? t->id : 0;
    return 0;
}
HLE_FUNC(pthread_main_np, int, ()) { auto* t = threads::current(); return t && t->id == 1; }
HLE_FUNC(pthread_setname_np, int, (const char* n)) {
    auto* t = threads::current();
    if (t && n) {
        t->name = n;
        profiler::set_thread_name(n);
        SetThreadDescription(GetCurrentThread(), widen(n).c_str());
    }
    return 0;
}
HLE_FUNC(pthread_getname_np, int, (GuestThread* t, char* buf, size_t n)) {
    if (!t || !n) return D_ESRCH;
    strncpy(buf, t->name.c_str(), n - 1);
    buf[n - 1] = 0;
    return 0;
}
HLE_FUNC(pthread_get_stacksize_np, size_t, (GuestThread* t)) { return t ? t->stack_size : 0; }
HLE_FUNC(pthread_get_stackaddr_np, gaddr, (GuestThread* t)) { return t ? t->stack_top() : 0; }
HLE_FUNC(pthread_setschedparam, int, (u64, int, const void*)) { return 0; }
HLE_FUNC(pthread_getschedparam, int, (u64, int* pol, int* p)) { if (pol) *pol = 1; if (p) { p[0] = 31; p[1] = 0; } return 0; }
HLE_FUNC(sched_get_priority_min, int, (int)) { return 15; }
HLE_FUNC(sched_get_priority_max, int, (int)) { return 47; }
HLE_FUNC(sched_yield, int, ()) { SwitchToThread(); return 0; }
HLE_FUNC(pthread_yield_np, void, ()) { SwitchToThread(); }
HLE_FUNC(pthread_kill, int, (u64, int)) { return 0; }
HLE_FUNC(pthread_sigmask, int, (int, const void*, u32* old)) { if (old) *old = 0; return 0; }
HLE_FUNC(pthread_setcancelstate, int, (int, int* old)) { if (old) *old = 0; return 0; }
HLE_FUNC(pthread_setcanceltype, int, (int, int* old)) { if (old) *old = 0; return 0; }
HLE_FUNC(pthread_cancel, int, (u64)) { return 0; }
HLE_FUNC(pthread_testcancel, void, ()) {}

HLE_FUNC(pthread_key_create, int, (u64* key, gaddr dtor)) {
    int k = threads::alloc_key();
    if (k < 0) return D_EAGAIN;
    threads::set_key_destructor((u32)k, dtor);
    *key = (u64)k;
    return 0;
}
HLE_FUNC(pthread_key_delete, int, (u64 key)) { threads::free_key((int)key); return 0; }
HLE_FUNC(pthread_getspecific, u64, (u64 key)) {
    auto* t = threads::current();
    return t && key < GuestThread::kTsdSlots ? t->tsd[key] : 0;
}
HLE_FUNC(pthread_setspecific, int, (u64 key, u64 v)) {
    auto* t = threads::current();
    if (!t || key >= GuestThread::kTsdSlots) return D_EINVAL;
    t->tsd[key] = v;
    return 0;
}

// ---------------------------------------------------------------- OSAtomic / spinlocks / ulock
HLE_FUNC(OSSpinLockLock, void, (volatile long* l)) {
    int spins = 0;
    while (_InterlockedCompareExchange(l, 1, 0) != 0) {
        if (++spins > 64) SwitchToThread();
    }
}
HLE_FUNC(OSSpinLockTry, bool, (volatile long* l)) { return _InterlockedCompareExchange(l, 1, 0) == 0; }
HLE_FUNC(OSSpinLockUnlock, void, (volatile long* l)) { _InterlockedExchange(l, 0); }
HLE_FUNC(os_unfair_lock_lock, void, (volatile long* l)) {
    int spins = 0;
    while (_InterlockedCompareExchange(l, 1, 0) != 0) {
        if (++spins > 32) {
            long v = 1;
            WaitOnAddress((void*)l, &v, sizeof v, 1);
        }
    }
}
HLE_FUNC(os_unfair_lock_trylock, bool, (volatile long* l)) { return _InterlockedCompareExchange(l, 1, 0) == 0; }
HLE_FUNC(os_unfair_lock_unlock, void, (volatile long* l)) { _InterlockedExchange(l, 0); WakeByAddressSingle((void*)l); }
HLE_FUNC(os_unfair_lock_assert_owner, void, (void*)) {}
HLE_FUNC(OSMemoryBarrier, void, ()) { MemoryBarrier(); }
HLE_FUNC(OSAtomicAdd32Barrier, s32, (s32 amt, volatile long* p)) { return _InterlockedExchangeAdd(p, amt) + amt; }
HLE_FUNC(OSAtomicAdd32, s32, (s32 amt, volatile long* p)) { return _InterlockedExchangeAdd(p, amt) + amt; }
HLE_FUNC(OSAtomicIncrement32Barrier, s32, (volatile long* p)) { return _InterlockedIncrement(p); }
HLE_FUNC(OSAtomicIncrement32, s32, (volatile long* p)) { return _InterlockedIncrement(p); }
HLE_FUNC(OSAtomicDecrement32Barrier, s32, (volatile long* p)) { return _InterlockedDecrement(p); }
HLE_FUNC(OSAtomicDecrement32, s32, (volatile long* p)) { return _InterlockedDecrement(p); }
HLE_FUNC(OSAtomicAdd64Barrier, s64, (s64 amt, volatile long long* p)) { return _InterlockedExchangeAdd64(p, amt) + amt; }
HLE_FUNC(OSAtomicIncrement64Barrier, s64, (volatile long long* p)) { return _InterlockedIncrement64(p); }
HLE_FUNC(OSAtomicDecrement64Barrier, s64, (volatile long long* p)) { return _InterlockedDecrement64(p); }
HLE_FUNC(OSAtomicCompareAndSwap32Barrier, bool, (s32 o, s32 n, volatile long* p)) { return _InterlockedCompareExchange(p, n, o) == o; }
HLE_FUNC(OSAtomicCompareAndSwap32, bool, (s32 o, s32 n, volatile long* p)) { return _InterlockedCompareExchange(p, n, o) == o; }
HLE_FUNC(OSAtomicCompareAndSwapIntBarrier, bool, (s32 o, s32 n, volatile long* p)) { return _InterlockedCompareExchange(p, n, o) == o; }
HLE_FUNC(OSAtomicCompareAndSwap64Barrier, bool, (s64 o, s64 n, volatile long long* p)) { return _InterlockedCompareExchange64(p, n, o) == o; }
HLE_FUNC(OSAtomicCompareAndSwapPtrBarrier, bool, (s64 o, s64 n, volatile long long* p)) { return _InterlockedCompareExchange64(p, n, o) == o; }
HLE_FUNC(OSAtomicCompareAndSwapLongBarrier, bool, (s64 o, s64 n, volatile long long* p)) { return _InterlockedCompareExchange64(p, n, o) == o; }
HLE_FUNC(OSAtomicOr32Barrier, s32, (u32 m, volatile long* p)) { return _InterlockedOr(p, (long)m) | (long)m; }
HLE_FUNC(OSAtomicAnd32Barrier, s32, (u32 m, volatile long* p)) { return _InterlockedAnd(p, (long)m) & (long)m; }
HLE_FUNC(OSAtomicTestAndSetBarrier, bool, (u32 n, volatile u8* p)) {
    u8 mask = (u8)(0x80 >> (n & 7));
    return (_InterlockedOr8((volatile char*)(p + (n >> 3)), (char)mask) & mask) != 0;
}

// __ulock_wait(operation, addr, value, timeout_us) / __ulock_wake(operation, addr, wake_value)
HLE_FUNC(__ulock_wait, int, (u32 op, void* addr, u64 value, u32 timeout_us)) {
    bool is64 = (op & 0xff) == 5;  // UL_COMPARE_AND_WAIT64
    DWORD ms = timeout_us ? std::max<DWORD>(1, timeout_us / 1000) : INFINITE;
    if (is64) WaitOnAddress(addr, &value, 8, ms);
    else { u32 v = (u32)value; WaitOnAddress(addr, &v, 4, ms); }
    return 0;
}
HLE_FUNC(__ulock_wait2, int, (u32 op, void* addr, u64 value, u64 timeout_ns, u64)) {
    bool is64 = (op & 0xff) == 5;
    DWORD ms = timeout_ns ? std::max<DWORD>(1, (DWORD)(timeout_ns / 1000000)) : INFINITE;
    if (is64) WaitOnAddress(addr, &value, 8, ms);
    else { u32 v = (u32)value; WaitOnAddress(addr, &v, 4, ms); }
    return 0;
}
HLE_FUNC(__ulock_wake, int, (u32 op, void* addr, u64)) {
    if (op & 0x100) WakeByAddressAll(addr);  // ULF_WAKE_ALL
    else WakeByAddressSingle(addr);
    return 0;
}

// ---------------------------------------------------------------- mach semaphores
namespace {
std::mutex g_sem_lock;
std::unordered_map<u32, HANDLE> g_sems;
u32 g_next_sem = 0x5000;
HANDLE sem_get(u32 s) {
    std::lock_guard lk(g_sem_lock);
    auto it = g_sems.find(s);
    return it == g_sems.end() ? nullptr : it->second;
}
}  // namespace
HLE_FUNC(semaphore_create, int, (u32, u32* out, int, int value)) {
    HANDLE h = CreateSemaphoreW(nullptr, value, 0x7fffffff, nullptr);
    std::lock_guard lk(g_sem_lock);
    u32 id = g_next_sem++;
    g_sems[id] = h;
    *out = id;
    return 0;
}
HLE_FUNC(semaphore_destroy, int, (u32, u32 s)) {
    std::lock_guard lk(g_sem_lock);
    auto it = g_sems.find(s);
    if (it != g_sems.end()) { CloseHandle(it->second); g_sems.erase(it); }
    return 0;
}
HLE_FUNC(semaphore_signal, int, (u32 s)) { if (HANDLE h = sem_get(s)) ReleaseSemaphore(h, 1, nullptr); return 0; }
HLE_FUNC(semaphore_signal_all, int, (u32 s)) { if (HANDLE h = sem_get(s)) ReleaseSemaphore(h, 64, nullptr); return 0; }
HLE_FUNC(semaphore_wait, int, (u32 s)) { if (HANDLE h = sem_get(s)) WaitForSingleObject(h, INFINITE); return 0; }
HLE_FUNC(semaphore_timedwait, int, (u32 s, u64 ts)) {
    // mach_timespec_t {unsigned int tv_sec; clock_res_t tv_nsec;} passed by value in x1
    u32 sec = (u32)ts, nsec = (u32)(ts >> 32);
    HANDLE h = sem_get(s);
    if (!h) return 15;  // KERN_INVALID_NAME
    DWORD r = WaitForSingleObject(h, sec * 1000 + nsec / 1000000);
    return r == WAIT_TIMEOUT ? 49 : 0;  // KERN_OPERATION_TIMED_OUT
}
