#include "vmem.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <malloc.h>

#include <atomic>
#include <map>
#include <mutex>
#include <shared_mutex>

namespace vmem {

namespace {
std::shared_mutex g_ro_lock;
std::vector<std::pair<gaddr, gaddr>> g_ro;
std::atomic<u32> g_map_generation{0};  // bumped by unmap(): invalidates code_mapped() caches
}  // namespace

bool map_fixed(gaddr addr, u64 size) {
    void* p = VirtualAlloc(gptr(addr), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) return false;
    if (gaddr_of(p) != addr) {
        VirtualFree(p, 0, MEM_RELEASE);
        return false;
    }
    return true;
}

gaddr map_anywhere(u64 size, gaddr hint) {
    size = align_up(size, kGuestPageSize);
    void* p = nullptr;
    if (hint) p = VirtualAlloc(gptr(hint), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) p = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return gaddr_of(p);
}

void unmap(gaddr addr, u64 size) {
    g_map_generation++;
    // Release whole allocations covered by the range; decommit partial ones.
    gaddr end = addr + size;
    gaddr cur = addr;
    while (cur < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(gptr(cur), &mbi, sizeof(mbi))) break;
        gaddr rbase = gaddr_of(mbi.BaseAddress);
        gaddr rend = rbase + mbi.RegionSize;
        gaddr abase = gaddr_of(mbi.AllocationBase);
        if (mbi.State == MEM_FREE) { cur = rend; continue; }
        if (abase == cur) {
            // figure out the extent of the whole allocation
            gaddr q = abase, aend = abase;
            for (;;) {
                MEMORY_BASIC_INFORMATION m2{};
                if (!VirtualQuery(gptr(q), &m2, sizeof(m2)) || gaddr_of(m2.AllocationBase) != abase) break;
                aend = gaddr_of(m2.BaseAddress) + m2.RegionSize;
                q = aend;
            }
            if (aend <= end) {
                VirtualFree(gptr(abase), 0, MEM_RELEASE);
                cur = aend;
                continue;
            }
        }
        gaddr dend = rend < end ? rend : end;
        if (mbi.State == MEM_COMMIT) VirtualFree(gptr(cur), dend - cur, MEM_DECOMMIT);
        cur = dend;
    }
}

void add_readonly_range(gaddr start, gaddr end) {
    std::unique_lock lk(g_ro_lock);
    g_ro.emplace_back(start, end);
}

bool is_readonly(gaddr addr) {
    std::shared_lock lk(g_ro_lock);
    for (auto& [s, e] : g_ro)
        if (addr >= s && addr < e) return true;
    return false;
}


bool code_mapped(gaddr addr) {
    // JIT instruction fetches: one VirtualQuery per committed region and thread
    thread_local gaddr lo = 1, hi = 0;
    thread_local u32 gen = ~0u;
    u32 g = g_map_generation.load(std::memory_order_relaxed);
    if (gen == g && addr >= lo && addr + 4 <= hi) return true;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(gptr(addr), &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    lo = gaddr_of(mbi.BaseAddress);
    hi = lo + mbi.RegionSize;
    gen = g;
    return addr + 4 <= hi || is_mapped(addr, 4);
}

bool is_mapped(gaddr addr, u64 len) {
    gaddr cur = addr, end = addr + len;
    while (cur < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(gptr(cur), &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        cur = gaddr_of(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

// ---- guest heap ----
// _aligned_malloc gives one consistent free function for every alignment.
void* gmalloc(size_t n) { return _aligned_malloc(n ? n : 1, 16); }
void* gcalloc(size_t n, size_t sz) {
    size_t t = n * sz;
    if (sz && t / sz != n) return nullptr;
    void* p = _aligned_malloc(t ? t : 1, 16);
    if (p) memset(p, 0, t);
    return p;
}
void* grealloc(void* p, size_t n) {
    if (!p) return gmalloc(n);
    if (!n) { _aligned_free(p); return nullptr; }
    return _aligned_realloc(p, n, 16);
}
void* gmemalign(size_t align, size_t n) {
    if (align < 16) align = 16;
    return _aligned_malloc(n ? n : 1, align);
}
void gfree(void* p) {
    if (p) _aligned_free(p);
}
size_t gmsize(void* p) { return p ? _aligned_msize(p, 16, 0) : 0; }

}  // namespace vmem
