// Guest virtual memory management (identity-mapped onto the host address space).
#pragma once
#include "common.h"

namespace vmem {

constexpr u64 kGuestPageSize = 0x4000;  // iOS arm64 uses 16 KiB pages

// Reserve+commit RW memory at exactly `addr`. Returns false if the range is taken.
bool map_fixed(gaddr addr, u64 size);
// Allocate RW memory anywhere (optionally near `hint`). Returns 0 on failure.
gaddr map_anywhere(u64 size, gaddr hint = 0);
// Release a mapping (whole or partial).
void unmap(gaddr addr, u64 size);

// Read-only ranges (e.g. __TEXT of loaded images) - used for JIT optimizations
void add_readonly_range(gaddr start, gaddr end);
bool is_readonly(gaddr addr);

// Is the address in mapped (committed) memory? (slow; diagnostics only)
bool is_mapped(gaddr addr, u64 len = 1);
bool code_mapped(gaddr addr);  // fast check for JIT instruction fetches (cached)

// Guest heap (malloc family). All guest heap blocks come from here so that
// free()/realloc() work for every allocation (including aligned ones).
void* gmalloc(size_t n);
void* gcalloc(size_t n, size_t sz);
void* grealloc(void* p, size_t n);
void* gmemalign(size_t align, size_t n);
void gfree(void* p);
size_t gmsize(void* p);

}  // namespace vmem
