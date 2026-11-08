// High-level emulation (HLE) of imported functions.
//
// Every host function gets a guest stub "svc #idx; ret". The SVC handler
// unmarshals arguments from guest registers per the Apple arm64 ABI, calls the
// host implementation and writes the result back into x0 / v0.
//
// Usage (in any .cpp):
//     HLE_FUNC(strlen, size_t, (const char* s)) { return ::strlen(s); }
//     HLE_RAW(printf) { ... use g_cpu / VaReader ... }            // variadic
//     HLE_DATA(___stack_chk_guard, 8) { *(u64*)p = 0x1234; }       // data symbol
#pragma once
#include "common.h"
#include "cpu.h"

#include <tuple>
#include <type_traits>

namespace hle {

using Handler = void (*)(Cpu&);

struct FuncDef {
    const char* name;  // Mach-O symbol name (leading underscore)
    Handler fn;
    bool reentrant;    // may call back into the guest (runs outside of JIT)
};
struct DataDef {
    const char* name;
    size_t size;
    void (*init)(void* p);
};

std::vector<FuncDef>& func_registry();
std::vector<DataDef>& data_registry();

struct FuncRegistrar { FuncRegistrar(const char* n, Handler h, bool r) { func_registry().push_back({n, h, r}); } };
struct DataRegistrar { DataRegistrar(const char* n, size_t s, void (*i)(void*)) { data_registry().push_back({n, s, i}); } };

void init();                         // builds stubs + data
gaddr lookup(const std::string& name);  // stub/data address or 0
gaddr make_unimplemented_stub(const std::string& name);  // logs when called
gaddr make_unimplemented_data(const std::string& name);
const char* name_of_stub(gaddr addr);  // for diagnostics (nullptr if none)
gaddr return_trap();                 // address of the "return to host" trap
void dispatch(Cpu& c, u32 idx);
void dump_history();
void prof_report(double seconds);  // EMU_PROFILE: slowest HLE functions  // recent HLE calls of the calling thread
void run_deferred(Cpu& c, u32 idx);
// Register a host function at runtime (e.g. for synthesized callbacks)
gaddr add_function(const char* name, Handler h, bool reentrant = false);

// ---- argument marshaling (Apple arm64 ABI) ----
struct ArgState {
    Cpu& c;
    int ngrn = 0, nsrn = 0;
    u64 nsaa;
    explicit ArgState(Cpu& cpu) : c(cpu), nsaa(cpu.sp()) {}
};

template <class T>
T arg_get(ArgState& s) {
    if constexpr (std::is_same_v<T, float>) {
        if (s.nsrn < 8) return s.c.s(s.nsrn++);
        float v; memcpy(&v, gptr(s.nsaa), 4); s.nsaa += 4; return v;
    } else if constexpr (std::is_same_v<T, double>) {
        if (s.nsrn < 8) return s.c.d(s.nsrn++);
        s.nsaa = align_up(s.nsaa, 8);
        double v; memcpy(&v, gptr(s.nsaa), 8); s.nsaa += 8; return v;
    } else if constexpr (std::is_pointer_v<T>) {
        if (s.ngrn < 8) return reinterpret_cast<T>(static_cast<uintptr_t>(s.c.x(s.ngrn++)));
        s.nsaa = align_up(s.nsaa, 8);
        u64 v; memcpy(&v, gptr(s.nsaa), 8); s.nsaa += 8;
        return reinterpret_cast<T>(static_cast<uintptr_t>(v));
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
        if (s.ngrn < 8) return static_cast<T>(s.c.x(s.ngrn++));
        // Apple: stack arguments use their natural size/alignment
        s.nsaa = align_up(s.nsaa, sizeof(T));
        T v; memcpy(&v, gptr(s.nsaa), sizeof(T)); s.nsaa += sizeof(T); return v;
    } else {
        static_assert(sizeof(T) == 0, "unsupported HLE argument type");
    }
}

template <class T>
void ret_set(Cpu& c, T v) {
    if constexpr (std::is_same_v<T, float>) c.set_s(0, v);
    else if constexpr (std::is_same_v<T, double>) c.set_d(0, v);
    else if constexpr (std::is_pointer_v<T>) c.set_x(0, static_cast<u64>(reinterpret_cast<uintptr_t>(v)));
    else if constexpr (std::is_same_v<T, bool>) c.set_x(0, v ? 1 : 0);
    else if constexpr (std::is_signed_v<T>) c.set_x(0, static_cast<u64>(static_cast<s64>(v)));
    else c.set_x(0, static_cast<u64>(v));
}

template <class F> struct fn_traits;
template <class R, class... A> struct fn_traits<R (*)(A...)> {
    using ret = R;
    using args = std::tuple<A...>;
};

template <auto F>
void wrap(Cpu& c) {
    using Tr = fn_traits<decltype(F)>;
    ArgState st(c);
    auto args = [&]<class... A>(std::tuple<A...>*) { return std::tuple<A...>{arg_get<A>(st)...}; }(
        static_cast<typename Tr::args*>(nullptr));
    if constexpr (std::is_void_v<typename Tr::ret>) std::apply(F, args);
    else ret_set(c, std::apply(F, args));
}

// Reads variadic arguments (Apple arm64: every variadic argument occupies an
// 8-byte stack slot) or a va_list (a plain pointer to such slots).
struct VaReader {
    u64 p;
    explicit VaReader(u64 ptr) : p(ptr) {}
    u64 next_u64() { u64 v; memcpy(&v, gptr(p), 8); p += 8; return v; }
    s64 next_s64() { return static_cast<s64>(next_u64()); }
    double next_double() { double v; memcpy(&v, gptr(p), 8); p += 8; return v; }
    template <class T> T* next_ptr() { return gptr<T>(next_u64()); }
};

}  // namespace hle

#define HLE_CAT2(a, b) a##b
#define HLE_CAT(a, b) HLE_CAT2(a, b)

// HLE_FUNC(name, ret, (args)) - name WITHOUT leading underscore
#define HLE_FUNC(NAME, RET, ARGS)                                                                   \
    static RET hle_impl_##NAME ARGS;                                                                \
    static ::hle::FuncRegistrar HLE_CAT(hle_reg_, NAME)("_" #NAME, &::hle::wrap<&hle_impl_##NAME>, false); \
    static RET hle_impl_##NAME ARGS
// Same, but the function may call into the guest (g_cpu->call(...))
#define HLE_FUNC_R(NAME, RET, ARGS)                                                                 \
    static RET hle_impl_##NAME ARGS;                                                                \
    static ::hle::FuncRegistrar HLE_CAT(hle_reg_, NAME)("_" #NAME, &::hle::wrap<&hle_impl_##NAME>, true); \
    static RET hle_impl_##NAME ARGS
// Register an existing function under an arbitrary symbol name
#define HLE_ALIAS(SYM, FN) static ::hle::FuncRegistrar HLE_CAT(hle_alias_, __LINE__)(SYM, &::hle::wrap<&FN>, false)
// Raw handler with direct register access: void(Cpu& c)
#define HLE_RAW(NAME)                                                                                \
    static void hle_raw_##NAME(Cpu& c);                                                             \
    static ::hle::FuncRegistrar HLE_CAT(hle_reg_, NAME)("_" #NAME, &hle_raw_##NAME, false);         \
    static void hle_raw_##NAME([[maybe_unused]] Cpu& c)
#define HLE_RAW_SYM(SYM, ID)                                                                         \
    static void hle_raw_##ID(Cpu& c);                                                               \
    static ::hle::FuncRegistrar HLE_CAT(hle_reg_, ID)(SYM, &hle_raw_##ID, false);                    \
    static void hle_raw_##ID([[maybe_unused]] Cpu& c)
// Data symbol: HLE_DATA(name_without_underscore, size) { init using `p` }
#define HLE_DATA(NAME, SIZE)                                                                         \
    static void hle_dinit_##NAME(void* p);                                                          \
    static ::hle::DataRegistrar HLE_CAT(hle_dreg_, NAME)("_" #NAME, SIZE, &hle_dinit_##NAME);       \
    static void hle_dinit_##NAME([[maybe_unused]] void* p)
