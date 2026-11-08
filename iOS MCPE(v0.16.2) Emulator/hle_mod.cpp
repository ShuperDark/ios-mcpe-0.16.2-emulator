// Services for mods (guest dylibs loaded from <data>/mods, see guest/include/emu/mod.h).
#include "hle.h"
#include "macho.h"
#include "vmem.h"

#include <cstring>

namespace {
// Instructions whose meaning depends on their address cannot be moved into a trampoline.
bool pc_relative(u32 i) {
    return (i & 0x1f000000) == 0x10000000 ||  // ADR / ADRP
           (i & 0x7c000000) == 0x14000000 ||  // B / BL
           (i & 0xff000010) == 0x54000000 ||  // B.cond
           (i & 0x7e000000) == 0x34000000 ||  // CBZ / CBNZ
           (i & 0x7e000000) == 0x36000000 ||  // TBZ / TBNZ
           (i & 0x3b000000) == 0x18000000;    // LDR (literal)
}
constexpr u32 kLdrX16Lit8 = 0x58000050;  // ldr x16, #8
constexpr u32 kBrX16 = 0xd61f0200;       // br x16
}  // namespace

HLE_FUNC(emu_log, void, (const char* msg)) { LOGI("[mod] %s", msg ? msg : "(null)"); }

HLE_FUNC(emu_game_base, u64, ()) {
    for (auto& i : macho::images())
        if (i->install_name == "minecraftpe") return i->base;
    return 0x100000000ull;
}

// Redirects the function at `target` to `replacement`. If `original` is not null it
// receives a trampoline that runs the replaced function. The first 16 bytes of the
// target are overwritten, so they must not contain PC-relative instructions (a function
// that is already hooked - by the emulator or another mod - can be hooked again: the
// trampoline then runs the previous hook).
// Returns 0 on success. Call it from the mod's constructor (before the game runs the
// target for the first time) - code that was already translated by the JIT keeps
// running the old version on threads other than the calling one.
HLE_FUNC(emu_hook, int, (u64 target, u64 replacement, u64* original)) {
    if (!target || !replacement || !vmem::is_mapped(target, 16)) {
        LOGW("[mod] emu_hook: bad address %llx", (unsigned long long)target);
        return -1;
    }
    u32* t = gptr<u32>(target);
    // already redirected (by the emulator or another mod): chain to the current replacement
    const bool chained = t[0] == kLdrX16Lit8 && t[1] == kBrX16;
    for (int k = 0; k < 4 && !chained; k++)
        if (pc_relative(t[k])) {
            LOGW("[mod] emu_hook: %llx starts with a PC-relative instruction (%08x), not hooked", (unsigned long long)target, t[k]);
            return -2;
        }
    if (original) {
        u32* tr = static_cast<u32*>(vmem::gmemalign(16, 32));
        if (chained) {
            memcpy(tr, t, 16);  // jumps to the previous replacement
        } else {
            memcpy(tr, t, 16);
            tr[4] = kLdrX16Lit8;
            tr[5] = kBrX16;
            *reinterpret_cast<u64*>(tr + 6) = target + 16;
        }
        *original = gaddr_of(tr);
    }
    t[0] = kLdrX16Lit8;
    t[1] = kBrX16;
    *reinterpret_cast<u64*>(t + 2) = replacement;
    if (g_cpu) g_cpu->invalidate_code(target, 16);
    LOGI("[mod] hooked %llx -> %llx", (unsigned long long)target, (unsigned long long)replacement);
    return 0;
}
