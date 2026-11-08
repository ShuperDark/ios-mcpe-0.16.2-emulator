// Example mod: shows how a mod is initialized, logs, uses libc++ and hooks a function
// of the game.
#include "emu_mod.h"

#include <string>

namespace {

// AppPlatform::getUIScreenType() of MCPE 0.16.2 (iOS), see the emulator's patches.cpp:
// returns 0 for desktop (PC style) screens. The emulator hooks it itself (classic screens
// on the PC, touch screens while a phone is connected); emu_hook chains onto that hook,
// so this one only logs the first call and forwards.
constexpr uint64_t kGetUIScreenType = 0x10052cbb8;

using GetUIScreenType = int (*)(void* platform);
GetUIScreenType g_original = nullptr;

int my_get_ui_screen_type(void* platform) {
    static bool logged = false;
    int r = g_original(platform);
    if (!logged) {
        logged = true;
        EMU_LOGF("example mod: AppPlatform::getUIScreenType() = %d", r);
    }
    return r;
}

}  // namespace

__attribute__((constructor)) static void mod_init() {
    std::string name = "Example Mod";
    EMU_LOGF("%s loaded (game at 0x%llx)", name.c_str(), (unsigned long long)emu_game_base());
    emu_set_title("Minecraft PE 0.16.2 - modded");

    uint64_t target = emu_game_base() - 0x100000000ull + kGetUIScreenType;
    uint64_t original = 0;
    if (emu_hook(target, (uint64_t)&my_get_ui_screen_type, &original) == 0)
        g_original = (GetUIScreenType)original;
}
