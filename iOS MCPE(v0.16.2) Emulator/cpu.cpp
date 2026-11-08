#include "cpu.h"
#include "profiler.h"
#include "hle.h"
#include "macho.h"
#include "vmem.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <dynarmic/interface/exclusive_monitor.h>

#include <mutex>

thread_local Cpu* g_cpu = nullptr;

namespace {

constexpr size_t kMaxProcessors = 128;
Dynarmic::ExclusiveMonitor& monitor() {
    static Dynarmic::ExclusiveMonitor m(kMaxProcessors);
    return m;
}
std::mutex g_proc_lock;
bool g_proc_used[kMaxProcessors];

size_t alloc_processor() {
    std::lock_guard lk(g_proc_lock);
    for (size_t i = 0; i < kMaxProcessors; i++)
        if (!g_proc_used[i]) { g_proc_used[i] = true; return i; }
    fatal("too many guest threads");
}
void free_processor(size_t i) {
    std::lock_guard lk(g_proc_lock);
    g_proc_used[i] = false;
}

u64 qpc_freq() {
    static u64 f = [] { LARGE_INTEGER li; QueryPerformanceFrequency(&li); return (u64)li.QuadPart; }();
    return f;
}

template <class T>
T rd(u64 a) { T v; memcpy(&v, gptr(a), sizeof(T)); return v; }
template <class T>
void wr(u64 a, T v) { memcpy(gptr(a), &v, sizeof(T)); }

}  // namespace

Cpu::Cpu(GuestThread* t, bool main_thread) : thread_(t) {
    processor_id_ = alloc_processor();
    Dynarmic::A64::UserConfig cfg{};
    cfg.callbacks = this;
    cfg.processor_id = processor_id_;
    cfg.global_monitor = &monitor();
    cfg.tpidrro_el0 = &tpidrro_el0;
    cfg.tpidr_el0 = &tpidr_el0;
    cfg.cntfrq_el0 = 24000000;
    cfg.dczid_el0 = 4;
    cfg.ctr_el0 = 0x8444c004;
    cfg.fastmem_pointer = 0;  // identity mapping: guest VA == host VA
    cfg.fastmem_address_space_bits = 48;
    cfg.silently_mirror_fastmem = false;
    cfg.recompile_on_fastmem_failure = true;
    // Exclusive loads/stores (atomics, shared_ptr reference counts, locks) inline
    // through fastmem instead of calling back into C++ for every access.
    cfg.fastmem_exclusive_access = true;
    cfg.recompile_on_exclusive_fastmem_failure = true;
    // Faster floating point: skip exact NaN propagation / FPCR emulation. The game
    // only uses standard IEEE behaviour. EMU_ACCURATE_FP=1 turns this off.
    static const bool accurate_fp = getenv("EMU_ACCURATE_FP") != nullptr;
    if (!accurate_fp) {
        cfg.unsafe_optimizations = true;
        cfg.optimizations = Dynarmic::all_safe_optimizations | Dynarmic::OptimizationFlag::Unsafe_ReducedErrorFP |
                            Dynarmic::OptimizationFlag::Unsafe_InaccurateNaN |
                            Dynarmic::OptimizationFlag::Unsafe_IgnoreStandardFPCRValue;
    }
    cfg.enable_cycle_counting = false;
    cfg.wall_clock_cntpct = true;
    cfg.define_unpredictable_behaviour = true;
    cfg.code_cache_size = main_thread ? 192ull * 1024 * 1024 : 32ull * 1024 * 1024;
    jit_ = std::make_unique<Dynarmic::A64::Jit>(cfg);
    jit_->SetFpcr(0);
}

Cpu::~Cpu() {
    jit_.reset();
    free_processor(processor_id_);
}

double Cpu::d(int i) const {
    auto v = jit_->GetVector(i);
    double r; memcpy(&r, &v[0], 8); return r;
}
float Cpu::s(int i) const {
    auto v = jit_->GetVector(i);
    float r; memcpy(&r, &v[0], 4); return r;
}
void Cpu::set_d(int i, double val) {
    Dynarmic::A64::Vector v{0, 0};
    memcpy(&v[0], &val, 8);
    jit_->SetVector(i, v);
}
void Cpu::set_s(int i, float val) {
    Dynarmic::A64::Vector v{0, 0};
    memcpy(&v[0], &val, 4);
    jit_->SetVector(i, v);
}

// ---------------------------------------------------------------- execution

void Cpu::run_until_return() {
    const int my_depth = call_depth_;
    for (;;) {
        Dynarmic::HaltReason hr = jit_->Run();
        (void)hr;
        if (exiting_) return;
        if (pending_hle_ >= 0) {
            u32 idx = static_cast<u32>(pending_hle_);
            pending_hle_ = -1;
            hle::run_deferred(*this, idx);
            if (exiting_) return;
        }
        if (returned_depth_ == my_depth) {
            returned_depth_ = -1;
            return;
        }
    }
}

u64 Cpu::call_fp(gaddr fn, std::initializer_list<u64> args, std::initializer_list<double> fargs) {
    // Preserve the caller's state (we may be nested inside an HLE handler).
    auto regs = jit_->GetRegisters();
    auto vecs = jit_->GetVectors();
    u64 saved_sp = jit_->GetSP(), saved_pc = jit_->GetPC();
    u32 pstate = jit_->GetPstate(), fpcr = jit_->GetFpcr(), fpsr = jit_->GetFpsr();

    int i = 0;
    for (u64 a : args) {
        if (i >= 8) fatal("Cpu::call: too many args");
        jit_->SetRegister(i++, a);
    }
    i = 0;
    for (double f : fargs) set_d(i++, f);
    u64 newsp = (saved_sp - 256) & ~u64(15);  // red zone safety margin
    if (call_depth_ == 0 && thread_) newsp = thread_->stack_top() & ~u64(15);
    jit_->SetSP(newsp);
    jit_->SetRegister(30, hle::return_trap());
    jit_->SetRegister(29, 0);
    jit_->SetPC(fn);

    call_depth_++;
    run_until_return();
    call_depth_--;
    u64 ret = jit_->GetRegister(0);
    if (!exiting_) {
        jit_->SetRegisters(regs);
        jit_->SetVectors(vecs);
        jit_->SetSP(saved_sp);
        jit_->SetPC(saved_pc);
        jit_->SetPstate(pstate);
        jit_->SetFpcr(fpcr);
        jit_->SetFpsr(fpsr);
    }
    return ret;
}

u64 Cpu::call(gaddr fn, std::initializer_list<u64> args) { return call_fp(fn, args, {}); }

void Cpu::halt_return() {
    returned_depth_ = call_depth_;
    jit_->HaltExecution(Dynarmic::HaltReason::UserDefined1);
}

void Cpu::request_exit() {
    exiting_ = true;
    jit_->HaltExecution(Dynarmic::HaltReason::UserDefined3);
}

void Cpu::defer_hle(u32 idx) {
    pending_hle_ = idx;
    jit_->HaltExecution(Dynarmic::HaltReason::UserDefined2);
}

void Cpu::invalidate_code(u64 addr, u64 size) { jit_->InvalidateCacheRange(addr, size); }

void Cpu::dump_state(const char* why) {
    LOGE("==== guest CPU state (%s) thread %u '%s' ====", why, thread_ ? thread_->id : 0,
         thread_ ? thread_->name.c_str() : "");
    if (const char* hn = hle::name_of_stub(pc())) LOGE("pc = HLE %s", hn);
    else LOGE("pc = %s", macho::symbolicate(pc()).c_str());
    LOGE("lr = %s", macho::symbolicate(x(30)).c_str());
    for (int i = 0; i < 31; i += 4) {
        char line[256];
        int n = 0;
        for (int k = i; k < i + 4 && k < 31; k++)
            n += snprintf(line + n, sizeof(line) - n, "x%-2d=%016llx ", k, (unsigned long long)x(k));
        LOGE("%s", line);
    }
    LOGE("sp=%016llx", (unsigned long long)sp());
    if (thread_ == threads::current()) hle::dump_history();
    // frame-pointer backtrace
    u64 fp = x(29);
    for (int depth = 0; depth < 32 && fp; depth++) {
        if (!vmem::is_mapped(fp, 16)) break;
        u64 next = rd<u64>(fp), ret = rd<u64>(fp + 8);
        if (!ret) break;
        LOGE("  #%02d %s", depth, macho::symbolicate(ret).c_str());
        if (next <= fp) break;
        fp = next;
    }
}

// ---------------------------------------------------------------- callbacks

std::optional<std::uint32_t> Cpu::MemoryReadCode(u64 vaddr) {
    if (!vmem::code_mapped(vaddr)) return std::nullopt;
    return rd<u32>(vaddr);
}
std::uint8_t Cpu::MemoryRead8(u64 a) { return rd<u8>(a); }
std::uint16_t Cpu::MemoryRead16(u64 a) { return rd<u16>(a); }
std::uint32_t Cpu::MemoryRead32(u64 a) { return rd<u32>(a); }
std::uint64_t Cpu::MemoryRead64(u64 a) { return rd<u64>(a); }
Dynarmic::A64::Vector Cpu::MemoryRead128(u64 a) { return {rd<u64>(a), rd<u64>(a + 8)}; }
void Cpu::MemoryWrite8(u64 a, std::uint8_t v) { wr(a, v); }
void Cpu::MemoryWrite16(u64 a, std::uint16_t v) { wr(a, v); }
void Cpu::MemoryWrite32(u64 a, std::uint32_t v) { wr(a, v); }
void Cpu::MemoryWrite64(u64 a, std::uint64_t v) { wr(a, v); }
void Cpu::MemoryWrite128(u64 a, Dynarmic::A64::Vector v) { wr(a, v[0]); wr(a + 8, v[1]); }

bool Cpu::MemoryWriteExclusive8(u64 a, std::uint8_t v, std::uint8_t e) {
    return _InterlockedCompareExchange8(gptr<char>(a), (char)v, (char)e) == (char)e;
}
bool Cpu::MemoryWriteExclusive16(u64 a, std::uint16_t v, std::uint16_t e) {
    return _InterlockedCompareExchange16(gptr<short>(a), (short)v, (short)e) == (short)e;
}
bool Cpu::MemoryWriteExclusive32(u64 a, std::uint32_t v, std::uint32_t e) {
    return _InterlockedCompareExchange(gptr<long>(a), (long)v, (long)e) == (long)e;
}
bool Cpu::MemoryWriteExclusive64(u64 a, std::uint64_t v, std::uint64_t e) {
    return _InterlockedCompareExchange64(gptr<long long>(a), (long long)v, (long long)e) == (long long)e;
}
bool Cpu::MemoryWriteExclusive128(u64 a, Dynarmic::A64::Vector v, Dynarmic::A64::Vector e) {
    long long cmp[2] = {(long long)e[0], (long long)e[1]};
    return _InterlockedCompareExchange128(gptr<long long>(a), (long long)v[1], (long long)v[0], cmp) != 0;
}

bool Cpu::IsReadOnlyMemory(u64 vaddr) { return vmem::is_readonly(vaddr); }

void Cpu::InterpreterFallback(u64 pc, size_t num) {
    dump_state("interpreter fallback");
    fatal("dynarmic interpreter fallback at %s (%zu instructions)", macho::symbolicate(pc).c_str(), num);
}

void Cpu::CallSVC(std::uint32_t swi) { hle::dispatch(*this, swi); }

void Cpu::ExceptionRaised(u64 pc, Dynarmic::A64::Exception ex) {
    using E = Dynarmic::A64::Exception;
    switch (ex) {
    case E::Yield:
    case E::WaitForEvent:
        SwitchToThread();
        return;
    case E::SendEvent:
    case E::SendEventLocal:
    case E::WaitForInterrupt:
        return;
    default: break;
    }
    if (const char* n = hle::name_of_stub(pc)) {
        dump_state("call into unresolved data symbol");
        fatal("guest called unresolved data symbol %s", n);
    }
    const char* what = ex == E::Breakpoint ? "breakpoint (brk / __builtin_trap)"
                     : ex == E::UnallocatedEncoding ? "unallocated encoding"
                     : ex == E::NoExecuteFault ? "no-execute fault"
                     : "cpu exception";
    set_pc(pc);
    dump_state(what);
    fatal("guest %s at %s", what, macho::symbolicate(pc).c_str());
}

std::uint64_t Cpu::GetCNTPCT() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    u64 t = (u64)li.QuadPart, f = qpc_freq();
    return (t / f) * 24000000ull + (t % f) * 24000000ull / f;
}

// ---------------------------------------------------------------- threads

namespace threads {
namespace {
std::mutex g_threads_lock;
std::vector<GuestThread*> g_threads;
std::atomic<u32> g_next_id{1};
thread_local GuestThread* t_current = nullptr;

std::mutex g_keys_lock;
bool g_key_used[GuestThread::kTsdSlots];
gaddr g_key_dtor[GuestThread::kTsdSlots];

GuestThread* alloc_thread(u64 stack_size) {
    // GuestThread lives in guest-visible memory (pthread_t points to it)
    void* mem = vmem::gmemalign(64, sizeof(GuestThread));
    auto* t = new (mem) GuestThread();
    t->id = g_next_id++;
    t->tsd[0] = gaddr_of(t);
    t->stack_size = align_up(stack_size, 0x10000);
    t->stack_base = vmem::map_anywhere(t->stack_size + 0x10000);  // + guard-ish slack
    if (!t->stack_base) fatal("cannot allocate guest stack");
    t->stack_base += 0x10000;
    std::lock_guard lk(g_threads_lock);
    g_threads.push_back(t);
    return t;
}
}  // namespace

GuestThread* current() { return t_current; }

GuestThread* create_main_thread(u64 stack_size) {
    GuestThread* t = alloc_thread(stack_size);
    t->name = "main";
    t->cpu = std::make_unique<Cpu>(t, true);
    t->cpu->tpidrro_el0 = gaddr_of(t->tsd);
    t_current = t;
    g_cpu = t->cpu.get();
    profiler::register_thread("main");
    return t;
}

GuestThread* spawn(gaddr fn, gaddr arg, u64 stack_size, bool detached) {
    GuestThread* t = alloc_thread(stack_size ? stack_size : 512 * 1024);
    t->start_routine = fn;
    t->start_arg = arg;
    t->detached = detached;
    t->host = std::thread([t] {
        t->cpu = std::make_unique<Cpu>(t, false);
        t->cpu->tpidrro_el0 = gaddr_of(t->tsd);
        t_current = t;
        g_cpu = t->cpu.get();
        profiler::register_thread(t->name.empty() ? "thread" : t->name.c_str());
        u64 r = t->cpu->call(t->start_routine, {t->start_arg});
        if (!t->cpu->exiting()) t->result = r;
        run_key_destructors(t);
        {
            std::lock_guard lk(t->mtx);
            t->finished = true;
        }
        t->cv.notify_all();
        profiler::unregister_thread();
        g_cpu = nullptr;
        t->cpu.reset();
    });
    if (detached) t->host.detach();
    return t;
}

void run_key_destructors(GuestThread* t) {
    for (int round = 0; round < 4; round++) {
        bool any = false;
        for (int k = GuestThread::kFirstUserKey; k < GuestThread::kTsdSlots; k++) {
            gaddr d = key_destructor(k);
            u64 v = t->tsd[k];
            if (d && v) {
                t->tsd[k] = 0;
                any = true;
                if (t->cpu && !t->cpu->exiting()) t->cpu->call(d, {v});
            }
        }
        if (!any) break;
    }
}

int alloc_key() {
    std::lock_guard lk(g_keys_lock);
    for (int k = GuestThread::kFirstUserKey; k < GuestThread::kTsdSlots; k++)
        if (!g_key_used[k]) { g_key_used[k] = true; g_key_dtor[k] = 0; return k; }
    return -1;
}
void free_key(int k) {
    std::lock_guard lk(g_keys_lock);
    if (k >= 0 && k < GuestThread::kTsdSlots) { g_key_used[k] = false; g_key_dtor[k] = 0; }
}
void set_key_destructor(u32 key, gaddr dtor) {
    std::lock_guard lk(g_keys_lock);
    if (key < GuestThread::kTsdSlots) g_key_dtor[key] = dtor;
}
gaddr key_destructor(u32 key) {
    std::lock_guard lk(g_keys_lock);
    return key < GuestThread::kTsdSlots ? g_key_dtor[key] : 0;
}

std::vector<GuestThread*> all() {
    std::lock_guard lk(g_threads_lock);
    return g_threads;
}

}  // namespace threads
