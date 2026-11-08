// ARM64 CPU (dynarmic) wrapper and guest threads.
#pragma once
#include "common.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <dynarmic/interface/A64/a64.h>
#include <dynarmic/interface/A64/config.h>

struct GuestThread;

class Cpu final : public Dynarmic::A64::UserCallbacks {
public:
    explicit Cpu(GuestThread* t, bool main_thread);
    ~Cpu() override;

    // ---- register access ----
    u64 x(int i) const { return jit_->GetRegister(i); }
    void set_x(int i, u64 v) { jit_->SetRegister(i, v); }
    u64 sp() const { return jit_->GetSP(); }
    void set_sp(u64 v) { jit_->SetSP(v); }
    u64 pc() const { return jit_->GetPC(); }
    void set_pc(u64 v) { jit_->SetPC(v); }
    u64 lr() const { return x(30); }
    double d(int i) const;
    float s(int i) const;
    void set_d(int i, double v);
    void set_s(int i, float v);
    Dynarmic::A64::Vector v(int i) const { return jit_->GetVector(i); }
    void set_v(int i, Dynarmic::A64::Vector val) { jit_->SetVector(i, val); }

    // ---- execution ----
    // Calls a guest function with integer arguments; returns x0. Can be nested
    // (from a reentrant HLE handler); the caller's register state is preserved.
    u64 call(gaddr fn, std::initializer_list<u64> args = {});
    // Same, but with double arguments in d0.. as well
    u64 call_fp(gaddr fn, std::initializer_list<u64> args, std::initializer_list<double> fargs);

    void halt_return();       // used by the return trap
    void request_exit();      // pthread_exit / exit: unwind every call level
    bool exiting() const { return exiting_; }
    void defer_hle(u32 idx);  // run an HLE handler outside of JIT context

    GuestThread* thread() { return thread_; }
    Dynarmic::A64::Jit* jit() { return jit_.get(); }

    // dump registers + backtrace
    void dump_state(const char* why);
    void invalidate_code(u64 addr, u64 size);  // after guest code was modified

    // ---- dynarmic callbacks ----
    std::optional<std::uint32_t> MemoryReadCode(u64 vaddr) override;
    std::uint8_t MemoryRead8(u64 vaddr) override;
    std::uint16_t MemoryRead16(u64 vaddr) override;
    std::uint32_t MemoryRead32(u64 vaddr) override;
    std::uint64_t MemoryRead64(u64 vaddr) override;
    Dynarmic::A64::Vector MemoryRead128(u64 vaddr) override;
    void MemoryWrite8(u64 vaddr, std::uint8_t value) override;
    void MemoryWrite16(u64 vaddr, std::uint16_t value) override;
    void MemoryWrite32(u64 vaddr, std::uint32_t value) override;
    void MemoryWrite64(u64 vaddr, std::uint64_t value) override;
    void MemoryWrite128(u64 vaddr, Dynarmic::A64::Vector value) override;
    bool MemoryWriteExclusive8(u64 vaddr, std::uint8_t value, std::uint8_t expected) override;
    bool MemoryWriteExclusive16(u64 vaddr, std::uint16_t value, std::uint16_t expected) override;
    bool MemoryWriteExclusive32(u64 vaddr, std::uint32_t value, std::uint32_t expected) override;
    bool MemoryWriteExclusive64(u64 vaddr, std::uint64_t value, std::uint64_t expected) override;
    bool MemoryWriteExclusive128(u64 vaddr, Dynarmic::A64::Vector value, Dynarmic::A64::Vector expected) override;
    bool IsReadOnlyMemory(u64 vaddr) override;
    void InterpreterFallback(u64 pc, size_t num_instructions) override;
    void CallSVC(std::uint32_t swi) override;
    void ExceptionRaised(u64 pc, Dynarmic::A64::Exception exception) override;
    void AddTicks(std::uint64_t) override {}
    std::uint64_t GetTicksRemaining() override { return ~0ull; }
    std::uint64_t GetCNTPCT() override;

    u64 tpidrro_el0 = 0;
    u64 tpidr_el0 = 0;

private:
    void run_until_return();

    GuestThread* thread_;
    std::unique_ptr<Dynarmic::A64::Jit> jit_;
    size_t processor_id_;
    int call_depth_ = 0;
    int returned_depth_ = -1;
    bool exiting_ = false;
    s64 pending_hle_ = -1;
};

// The Cpu of the guest thread running on the calling host thread.
extern thread_local Cpu* g_cpu;

struct GuestThread {
    // Guest-visible thread specific data (TPIDRRO_EL0 points here). Slot 0 holds
    // the pthread_t (== this). Slots >= kFirstUserKey are pthread keys.
    static constexpr int kTsdSlots = 768;
    static constexpr int kFirstUserKey = 256;
    u64 tsd[kTsdSlots] = {};
    int errno_val = 0;
    int _pad = 0;
    u32 id = 0;
    std::unique_ptr<Cpu> cpu;
    gaddr stack_base = 0;
    u64 stack_size = 0;
    gaddr start_routine = 0, start_arg = 0;
    gaddr result = 0;
    std::atomic<bool> detached{false};
    std::atomic<bool> finished{false};
    std::mutex mtx;
    std::condition_variable cv;
    std::thread host;
    std::string name;
    std::unordered_map<u32, gaddr> tlv_blocks;  // image tlv key -> block

    gaddr stack_top() const { return stack_base + stack_size; }
};

namespace threads {
GuestThread* current();
GuestThread* create_main_thread(u64 stack_size);
// Spawns a host thread running guest `fn(arg)`; returns the new thread.
GuestThread* spawn(gaddr fn, gaddr arg, u64 stack_size, bool detached);
// Calls guest functions registered with pthread_key_create destructors.
void run_key_destructors(GuestThread* t);
void set_key_destructor(u32 key, gaddr dtor);
gaddr key_destructor(u32 key);
int alloc_key();
void free_key(int key);
std::vector<GuestThread*> all();
}  // namespace threads
