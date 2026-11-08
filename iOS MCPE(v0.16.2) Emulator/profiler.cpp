#include "profiler.h"
#include "common.h"
#include "hle.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace profiler {
namespace {

struct Entry {
    DWORD tid;
    HANDLE h;
    std::string name;
    std::unordered_map<u64, u32> rips;
    u64 samples = 0;
};

std::mutex g_lock;
std::vector<Entry*> g_entries;
std::vector<Entry*> g_retired;  // finished threads, reported once more
bool g_enabled = false;
double g_interval = 10;

struct Sym {
    std::string name;
    int kind;  // 0 guest code (JIT), 1 emulator, 2 OS / other module
};

std::unordered_map<u64, Sym> g_sym_cache;

Sym classify(u64 rip) {
    auto it = g_sym_cache.find(rip);
    if (it != g_sym_cache.end()) return it->second;
    Sym s;
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(rip), &mod)) {
        s = {"[guest code (JIT)]", 0};
    } else {
        char buf[sizeof(SYMBOL_INFO) + 512];
        auto* si = reinterpret_cast<SYMBOL_INFO*>(buf);
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 511;
        DWORD64 disp = 0;
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(mod, path, MAX_PATH);
        std::wstring p = path;
        std::string modname = narrow(p.substr(p.find_last_of(L"\\/") + 1));
        bool exe = mod == GetModuleHandleW(nullptr);
        std::string fn = SymFromAddr(GetCurrentProcess(), rip, &disp, si) ? std::string(si->Name) : "?";
        s = {exe ? fn : modname + "!" + fn, exe ? 1 : 2};
    }
    g_sym_cache.emplace(rip, s);
    return s;
}

void report() {
    std::vector<Entry*> all;
    {
        std::lock_guard lk(g_lock);
        all = g_entries;
        all.insert(all.end(), g_retired.begin(), g_retired.end());
    }
    // group threads by name
    struct Group {
        u64 samples = 0, threads = 0;
        u64 kind[3] = {};
        std::map<std::string, u64> syms;
    };
    std::map<std::string, Group> groups;
    {
        std::lock_guard lk(g_lock);
        for (Entry* e : all) {
            Group& g = groups[e->name];
            g.threads++;
            g.samples += e->samples;
            for (auto& [rip, n] : e->rips) {
                Sym s = classify(rip);
                g.kind[s.kind] += n;
                g.syms[s.name] += n;
            }
            e->rips.clear();
            e->samples = 0;
        }
        for (Entry* e : g_retired) delete e;
        g_retired.clear();
    }
    LOGI("==== profile (last %.0f s, 1 sample/ms per thread) ====", g_interval);
    for (auto& [name, g] : groups) {
        if (!g.samples) continue;
        // waiting threads sit in ntdll / kernelbase wait functions
        u64 wait = 0;
        for (auto& [sn, n] : g.syms)
            if (sn.find("NtWait") != std::string::npos || sn.find("NtDelay") != std::string::npos ||
                sn.find("NtRemoveIoCompletion") != std::string::npos || sn.find("ZwWait") != std::string::npos ||
                sn.find("NtUserMsgWait") != std::string::npos || sn.find("SleepConditionVariable") != std::string::npos)
                wait += n;
        double busy = 100.0 * (g.samples - wait) / g.samples;
        LOGI("[%s] x%llu: busy %.0f%% | guest code %.1f%%, emulator %.1f%%, os %.1f%% (of all samples)", name.c_str(),
             (unsigned long long)g.threads, busy, 100.0 * g.kind[0] / g.samples, 100.0 * g.kind[1] / g.samples,
             100.0 * g.kind[2] / g.samples);
        if (busy < 5) continue;
        std::vector<std::pair<u64, std::string>> top;
        for (auto& [sn, n] : g.syms) top.emplace_back(n, sn);
        std::sort(top.rbegin(), top.rend());
        for (size_t i = 0; i < top.size() && i < 14; i++)
            LOGI("    %5.1f%%  %s", 100.0 * top[i].first / g.samples, top[i].second.c_str());
    }
    hle::prof_report(g_interval);
}

void sampler() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    LARGE_INTEGER f, t0, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    for (;;) {
        Sleep(1);
        {
            std::lock_guard lk(g_lock);
            for (Entry* e : g_entries) {
                if (SuspendThread(e->h) == (DWORD)-1) continue;
                CONTEXT ctx{};
                ctx.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(e->h, &ctx)) {
                    e->rips[ctx.Rip]++;
                    e->samples++;
                }
                ResumeThread(e->h);
            }
        }
        QueryPerformanceCounter(&t);
        if (double(t.QuadPart - t0.QuadPart) / f.QuadPart >= g_interval) {
            report();
            QueryPerformanceCounter(&t0);
        }
    }
}

}  // namespace

void init() {
    const char* v = getenv("EMU_PROFILE");
    if (!v) return;
    g_interval = (std::max)(1.0, atof(v));
    g_enabled = true;
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    std::thread(sampler).detach();
    LOGI("profiler: reporting every %.0f s", g_interval);
}

void register_thread(const char* name) {
    if (!g_enabled) return;
    auto* e = new Entry;
    e->tid = GetCurrentThreadId();
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &e->h,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    e->name = name && *name ? name : "thread";
    std::lock_guard lk(g_lock);
    g_entries.push_back(e);
}

void set_thread_name(const char* name) {
    if (!g_enabled || !name) return;
    DWORD tid = GetCurrentThreadId();
    std::lock_guard lk(g_lock);
    for (Entry* e : g_entries)
        if (e->tid == tid) e->name = name;
}

void unregister_thread() {
    if (!g_enabled) return;
    DWORD tid = GetCurrentThreadId();
    std::lock_guard lk(g_lock);
    for (size_t i = 0; i < g_entries.size(); i++)
        if (g_entries[i]->tid == tid) {
            CloseHandle(g_entries[i]->h);
            g_retired.push_back(g_entries[i]);
            g_entries.erase(g_entries.begin() + i);
            return;
        }
}

}  // namespace profiler
