#include "hle.h"
#include "macho.h"
#include "vmem.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace hle {

std::vector<FuncDef>& func_registry() {
    static std::vector<FuncDef> v;
    return v;
}
std::vector<DataDef>& data_registry() {
    static std::vector<DataDef> v;
    return v;
}

namespace {

constexpr u32 kMaxFuncs = 65536;
struct Entry {
    const char* name;
    Handler fn;
    bool reentrant;
};
Entry g_table[kMaxFuncs];
std::atomic<u32> g_count{0};
gaddr g_stub_base = 0;
std::mutex g_lock;
std::unordered_map<std::string, gaddr> g_by_name;
std::unordered_map<gaddr, std::string> g_data_names;  // unresolved-data blocks
thread_local u32 t_cur_idx = 0;
thread_local u32 t_hist[48];
thread_local u32 t_hist_pos = 0;
std::unordered_set<std::string> g_unimpl_reported;
std::vector<std::string*> g_owned_names;

u32 add_entry(const char* name, Handler fn, bool re) {
    u32 i = g_count++;
    if (i >= kMaxFuncs) fatal("HLE table full");
    g_table[i] = {name, fn, re};
    u32* code = gptr<u32>(g_stub_base + u64(i) * 8);
    code[0] = 0xD4000001u | (i << 5);  // svc #i
    code[1] = 0xD65F03C0u;             // ret
    return i;
}

void return_trap_handler(Cpu& c) { c.halt_return(); }

void unimpl_handler(Cpu& c) {
    const char* n = g_table[t_cur_idx].name;
    {
        std::lock_guard lk(g_lock);
        if (g_unimpl_reported.insert(n).second)
            LOGW("UNIMPLEMENTED %s called from %s (returning 0)", n, macho::symbolicate(c.lr()).c_str());
    }
    c.set_x(0, 0);
    c.set_d(0, 0.0);
}

}  // namespace

void init() {
    g_stub_base = vmem::map_anywhere(u64(kMaxFuncs) * 8);
    if (!g_stub_base) fatal("cannot allocate HLE stub area");
    vmem::add_readonly_range(g_stub_base, g_stub_base + u64(kMaxFuncs) * 8);
    add_entry("<return trap>", &return_trap_handler, false);
    for (auto& f : func_registry()) {
        if (g_by_name.count(f.name)) {
            LOGW("duplicate HLE function %s", f.name);
            continue;
        }
        u32 i = add_entry(f.name, f.fn, f.reentrant);
        g_by_name[f.name] = g_stub_base + u64(i) * 8;
    }
    for (auto& d : data_registry()) {
        if (g_by_name.count(d.name)) {
            LOGW("duplicate HLE data %s", d.name);
            continue;
        }
        void* p = vmem::gcalloc(1, d.size < 16 ? 16 : d.size);
        if (d.init) d.init(p);
        g_by_name[d.name] = gaddr_of(p);
    }
    LOGI("HLE: %zu functions, %zu data symbols", func_registry().size(), data_registry().size());
}

gaddr lookup(const std::string& name) {
    std::lock_guard lk(g_lock);
    auto it = g_by_name.find(name);
    return it == g_by_name.end() ? 0 : it->second;
}

gaddr add_function(const char* name, Handler h, bool reentrant) {
    std::lock_guard lk(g_lock);
    u32 i = add_entry(name, h, reentrant);
    gaddr a = g_stub_base + u64(i) * 8;
    g_by_name[name] = a;
    return a;
}

gaddr make_unimplemented_stub(const std::string& name) {
    std::lock_guard lk(g_lock);
    auto it = g_by_name.find(name);
    if (it != g_by_name.end()) return it->second;
    auto* s = new std::string(name);
    g_owned_names.push_back(s);
    u32 i = add_entry(s->c_str(), &unimpl_handler, false);
    gaddr a = g_stub_base + u64(i) * 8;
    g_by_name[name] = a;
    return a;
}

gaddr make_unimplemented_data(const std::string& name) {
    std::lock_guard lk(g_lock);
    auto it = g_by_name.find(name);
    if (it != g_by_name.end()) return it->second;
    void* p = vmem::gcalloc(1, 256);
    g_by_name[name] = gaddr_of(p);
    g_data_names[gaddr_of(p)] = name;
    return gaddr_of(p);
}

const char* name_of_stub(gaddr addr) {
    std::lock_guard lk(g_lock);
    for (auto& [a, n] : g_data_names)
        if (addr >= a && addr < a + 256) return n.c_str();
    if (addr >= g_stub_base && addr < g_stub_base + u64(g_count) * 8) return g_table[(addr - g_stub_base) / 8].name;
    return nullptr;
}

gaddr return_trap() { return g_stub_base; }

void dump_history() {
    std::string line;
    for (u32 k = t_hist_pos > 48 ? t_hist_pos - 48 : 0; k < t_hist_pos; k++) {
        line += g_table[t_hist[k % 48]].name;
        line += ' ';
    }
    LOGE("recent HLE calls: %s", line.c_str());
}

// EMU_PROFILE: time spent inside each HLE function (all threads)
std::atomic<u64> g_prof_ticks[65536];
std::atomic<u32> g_prof_calls[65536];
bool g_prof_on = getenv("EMU_PROFILE") != nullptr;

static inline u64 prof_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (u64)t.QuadPart;
}

void prof_report(double seconds) {
    if (!g_prof_on) return;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    std::vector<std::pair<u64, u32>> v;
    for (u32 i = 0; i < g_count; i++) {
        u64 t = g_prof_ticks[i].exchange(0);
        u32 n = g_prof_calls[i].exchange(0);
        if (t) v.emplace_back(t, i);
        (void)n;
    }
    std::sort(v.rbegin(), v.rend());
    for (size_t k = 0; k < v.size() && k < 12; k++) {
        double ms = 1000.0 * v[k].first / f.QuadPart;
        LOGI("    HLE %8.1f ms (%5.1f%% of one thread)  %s", ms, ms / (10.0 * seconds), g_table[v[k].second].name);
    }
}

void dispatch(Cpu& c, u32 idx) {
    if (idx >= g_count) {
        c.dump_state("bad svc");
        fatal("guest executed svc #%u (no such HLE function)", idx);
    }
    const Entry& e = g_table[idx];
    t_hist[t_hist_pos++ % 48] = idx;
    if (e.reentrant) {
        c.defer_hle(idx);
        return;
    }
    t_cur_idx = idx;
    if (g_prof_on) {
        u64 t0 = prof_now();
        e.fn(c);
        g_prof_ticks[idx] += prof_now() - t0;
        g_prof_calls[idx]++;
        return;
    }
    e.fn(c);
}

void run_deferred(Cpu& c, u32 idx) {
    t_cur_idx = idx;
    if (g_prof_on) {
        u64 t0 = prof_now();
        g_table[idx].fn(c);
        g_prof_ticks[idx] += prof_now() - t0;
        g_prof_calls[idx]++;
        return;
    }
    g_table[idx].fn(c);
}

}  // namespace hle
