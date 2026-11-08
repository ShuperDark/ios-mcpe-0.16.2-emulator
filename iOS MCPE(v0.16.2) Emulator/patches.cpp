// Game-specific patches applied to the loaded binary before it runs.
//
// Objective-C methods can be replaced by name (the guest runtime copies the
// method lists when it starts, so patching the static metadata is enough).
#include "controls.h"
#include "cpu.h"
#include "hle.h"
#include "macho.h"
#include "window.h"

#include <cstring>
#include <string>

namespace patches {
namespace {

struct Patch {
    const char* cls;
    bool meta;
    const char* sel;
    const char* replacement;  // exported symbol providing the new IMP
};

// HockeyApp (crash reporting / update checks) installs signal and Mach
// exception handlers and talks to the network: disable it entirely.
const Patch kObjcPatches[] = {
    {"BITHockeyManager", true, "sharedHockeyManager", "__objc_nil_imp"},
};

u64 rd64(gaddr a) { return *gptr<u64>(a); }
u32 rd32(gaddr a) { return *gptr<u32>(a); }

bool patch_method_list(gaddr ml, const char* sel, gaddr imp) {
    if (!ml) return false;
    u32 es = rd32(ml) & 0xfffc;
    u32 cnt = rd32(ml + 4);
    if (rd32(ml) & 0x80000000u) return false;  // relative lists are not used by the app
    for (u32 i = 0; i < cnt; i++) {
        gaddr m = ml + 8 + u64(i) * es;
        const char* name = gptr<const char>(rd64(m));
        if (name && !strcmp(name, sel)) {
            *gptr<u64>(m + 16) = imp;
            return true;
        }
    }
    return false;
}

// ---- window resizing ----
// The game sets its render size once, in -[minecraftpeViewController initView]
// (app->setSize(width, height), vtable +0xb0); iOS screens never change size. The
// view controller is remembered there so that a resized / fullscreen window can
// call setSize again.
constexpr gaddr kInitViewImp = 0x1009057b8;
constexpr gaddr kAppIvarOffset = 0x1011c6af8;  // ivar offset variable of the App* member
gaddr g_view_controller = 0;
void hook_init_view(Cpu& c) {
    u64 self = c.x(0), cmd = c.x(1);
    g_view_controller = self;
    c.call(kInitViewImp, {self, cmd});
}

gaddr find_method_list(const MachSection* cl, const char* cls_name) {
    for (u64 i = 0; i < cl->size / 8; i++) {
        gaddr c = rd64(cl->addr + i * 8);
        gaddr ro = rd64(c + 32) & ~7ull;
        const char* name = gptr<const char>(rd64(ro + 24));
        if (name && !strcmp(name, cls_name)) return rd64(ro + 32);
    }
    return 0;
}

// AppPlatform::getUIScreenType(): 0 = desktop screens ($desktop_screen, the PC style
// inventory where items are picked up with the cursor), otherwise pocket screens.
// The iOS platform answers "pocket"; [ui] screens = classic in controls.ini asks for
// desktop screens, except while a phone is connected (remote.cpp), which gets the
// touch screens. The game asks whenever it creates a screen, so the answer may change
// at run time.
//   ldrb w8, [x0, #0xf8]; cbz w8, 1f; ldr w0, [x0, #0xf4]; ret     (setUIScreenType override)
//   1: ldr x8, [x0]; ldr x1, [x8, #0x330]; br x1                    (platform default)
constexpr gaddr kGetScreenType = 0x10052cbb8;
void hook_screen_type(Cpu& c) {
    u64 self = c.x(0);
    if (controls::classic_screens() && !window::phone_connected()) {
        c.set_x(0, 0);
        return;
    }
    if (*gptr<u8>(self + 0xf8)) {
        c.set_x(0, *gptr<u32>(self + 0xf4));
        return;
    }
    c.set_x(0, c.call(*gptr<u64>(*gptr<u64>(self) + 0x330), {self}));
}

}  // namespace

void game_resized(int w, int h) {
    if (!g_view_controller || w <= 0 || h <= 0) return;
    gaddr app = *gptr<u64>(g_view_controller + *gptr<s32>(kAppIvarOffset));
    if (!app) return;
    // same sequence as initView: vtable +0xb0 (w, h), then +0xa8 (w, h, gui scale 0 = automatic),
    // which stores the render size, recomputes the GUI scale and resizes the renderers
    gaddr vt = *gptr<u64>(app);
    LOGI("game resized to %dx%d", w, h);
    g_cpu->call(*gptr<u64>(vt + 0xb0), {app, (u64)w, (u64)h});
    g_cpu->call_fp(*gptr<u64>(vt + 0xa8), {app, (u64)w, (u64)h}, {0.0});
}

void apply(MachImage& img) {
    {
        u32* code = gptr<u32>(kGetScreenType);
        if (code[0] == 0x3943e008 && code[1] == 0x34000068) {  // ldrb w8, [x0, #0xf8]; cbz w8, +12
            gaddr stub = hle::add_function("emu_hook_get_ui_screen_type", hook_screen_type, true);
            code[0] = 0x58000050;  // ldr x16, #8
            code[1] = 0xd61f0200;  // br x16
            *reinterpret_cast<u64*>(code + 2) = stub;
            LOGI("hooked AppPlatform::getUIScreenType (%s screens)", controls::classic_screens() ? "desktop" : "pocket");
        } else {
            LOGW("patch: getUIScreenType has unexpected code %08x %08x", code[0], code[1]);
        }
    }
    const MachSection* cl = img.find_section("__DATA", "__objc_classlist");
    if (!cl) return;
    if (gaddr ml = find_method_list(cl, "minecraftpeViewController"))
        if (!patch_method_list(ml, "initView", hle::add_function("emu_hook_init_view", hook_init_view, true)))
            LOGW("patch: -[minecraftpeViewController initView] not found");
    for (auto& p : kObjcPatches) {
        gaddr imp = macho::find_export(p.replacement);
        if (!imp) {
            LOGW("patch: replacement %s not found", p.replacement);
            continue;
        }
        bool done = false;
        for (u64 i = 0; i < cl->size / 8 && !done; i++) {
            gaddr c = rd64(cl->addr + i * 8);
            gaddr ro = rd64(c + 32) & ~7ull;
            const char* name = gptr<const char>(rd64(ro + 24));
            if (!name || strcmp(name, p.cls)) continue;
            gaddr target = p.meta ? rd64(c) : c;
            gaddr tro = rd64(target + 32) & ~7ull;
            done = patch_method_list(rd64(tro + 32), p.sel, imp);
        }
        if (done) LOGI("patched %c[%s %s]", p.meta ? '+' : '-', p.cls, p.sel);
        else LOGW("patch target %c[%s %s] not found", p.meta ? '+' : '-', p.cls, p.sel);
    }
}

// ============================================================================
// PC-style keyboard / mouse input
//
// The iOS build still contains the game's cross-platform keyboard and mouse input
// (the Windows 10 edition shares the code), it is just never fed on iOS:
//
//   Keyboard::_inputs  std::vector<KeyboardAction{int state; u8 key}> @ 0x101265a08
//   Keyboard::_states  int[256]                                       @ 0x101265608
//     (Keyboard::feed is inlined into -[ShowKeyboardView textViewShouldReturn:])
//   Mouse::_instance   MouseDevice; its std::vector<MouseAction> at +0x18 @ 0x101267250
//
// KeyboardInputDevice / MouseInputDevice turn those events into button.* events via an
// unordered_multimap<int key, Binding> (device+0x18) that setInputMapping() rebuilds
// from the current InputMapping. A mouse move with a delta switches the game into its
// keyboard/mouse mode (crosshair, no touch HUD) and rotates the camera.
//
// The iOS mappings only bind a few debug keys while playing, so after every
// setInputMapping() of the in-world mapping (recognized by Esc -> button.pause) the
// PC bindings below are added. Menu mappings already bind E/Esc/Enter/arrows/digits.
// Key codes are Windows virtual-key codes, as on the Windows 10 edition.
// ============================================================================
namespace {
constexpr gaddr kKbInputs = 0x101265a08, kKbStates = 0x101265608;
constexpr gaddr kKbPushSlow = 0x100900e70;     // vector<KeyboardAction>::__push_back_slow_path
constexpr gaddr kMouse = 0x101267250;
constexpr gaddr kMousePushSlow = 0x100020f0c;  // vector<MouseAction>::__push_back_slow_path
constexpr gaddr kKbdVtSetMapping = 0x1010ec4a8;    // KeyboardInputDevice vtable slot 2
constexpr gaddr kMouseVtSetMapping = 0x1010ec508;  // MouseInputDevice vtable slot 2
constexpr gaddr kKbdMapInsert = 0x10002071c;       // multimap __node_insert_multi
constexpr gaddr kMouseMapInsert = 0x1000117c8;

struct KeyboardAction { s32 state; u8 key; u8 pad[3]; };
struct MouseAction { s16 x, y, dx, dy; u8 action; s8 data; u8 pad[2]; s32 pointer; };
static_assert(sizeof(MouseAction) == 16);

bool g_input_ready = false;
bool g_in_world = false;
// Screens opened with the inventory key or the use button (inventory, chest, crafting table, furnace...)
// are closed with E as well; the creative/pocket screens only react to menu_cancel, so
// the inventory key is sent as Esc there.
bool g_inventory_screen = false;
bool g_e_as_escape = false;
double g_last_open_input = -100;
gaddr g_orig_kbd_setmap, g_orig_mouse_setmap, g_znwm;

std::string guest_string(gaddr p) {  // libc++ (alternate layout) std::string
    const u8* sp = gptr<u8>(p);
    if (sp[23] & 0x80) return std::string(gptr<char>(*(const u64*)sp), *(const u64*)(sp + 8));
    return std::string((const char*)sp, sp[23]);
}

// ButtonRegistry: unordered_map<std::string, short>; nodes {next, hash, string, id @+0x28}
int button_id(gaddr registry, const char* name) {
    for (u64 n = *gptr<u64>(registry + 0x10); n; n = *gptr<u64>(n))
        if (guest_string(n + 0x10) == name) return *gptr<s16>(n + 0x28);
    return -1;
}

// device+0x18: unordered_multimap<int, Binding>; nodes {next, hash, int key @+0x10, value @+0x14}
bool has_binding(gaddr dev, int key, int id) {
    for (u64 n = *gptr<u64>(dev + 0x28); n; n = *gptr<u64>(n))
        if (*gptr<s32>(n + 0x10) == key && (id < 0 || *gptr<s16>(n + 0x14) == id)) return true;
    return false;
}

void add_binding(Cpu& c, gaddr dev, gaddr insert_fn, int key, int id) {
    if (has_binding(dev, key, id)) return;
    gaddr node = c.call(g_znwm, {0x18});
    *gptr<u64>(node) = 0;
    *gptr<s64>(node + 8) = key;  // std::hash<int>
    *gptr<s32>(node + 0x10) = key;
    *gptr<u32>(node + 0x14) = (u16)id;
    c.call(insert_fn, {dev + 0x18, node});
}

// virtual void setInputMapping(InputDevice* this, ?, ?, ButtonRegistry&, const InputMapping&)
void hook_kbd_setmap(Cpu& c) {
    u64 dev = c.x(0), a1 = c.x(1), a2 = c.x(2), reg = c.x(3), map = c.x(4);
    c.call(g_orig_kbd_setmap, {dev, a1, a2, reg, map});
    g_input_ready = true;
    int pause = button_id(reg, "button.pause");
    bool was_in_world = g_in_world;
    g_in_world = pause >= 0 && has_binding(dev, 0x1b, pause);
    if (was_in_world && !g_in_world)  // a screen opened: by E / right click (inventory, chest, ...)?
        g_inventory_screen = window::now() - g_last_open_input < 1.0;
    if (g_in_world) g_inventory_screen = false;
    if (g_in_world) {
        controls::reload_if_changed();
        for (auto& b : controls::keyboard()) {
            int id = button_id(reg, b.button.c_str());
            if (id >= 0) add_binding(c, dev, kKbdMapInsert, b.code, id);
            else LOGW("input: %s is not a button of this game", b.button.c_str());
        }
    }
    window::set_gameplay(g_in_world);
    if (g_log_level <= LogLevel::Debug) {
        std::string keys;
        for (u64 n = *gptr<u64>(dev + 0x28); n; n = *gptr<u64>(n)) {
            char b[32];
            snprintf(b, sizeof b, " %d->%x", *gptr<s32>(n + 0x10), *gptr<u32>(n + 0x14));
            keys += b;
        }
        LOGD("keyboard mapping (mode %d, world %d):%s", *gptr<s32>(dev + 0x10), g_in_world, keys.c_str());
    }
}

void hook_mouse_setmap(Cpu& c) {
    u64 dev = c.x(0), a1 = c.x(1), a2 = c.x(2), reg = c.x(3), map = c.x(4);
    c.call(g_orig_mouse_setmap, {dev, a1, a2, reg, map});
    if (g_in_world)
        for (auto& b : controls::mouse()) {
            int id = button_id(reg, b.button.c_str());
            if (id >= 0) add_binding(c, dev, kMouseMapInsert, b.code, id);
        }
}
}  // namespace

void install_input_hooks() {
    g_znwm = macho::find_export("__Znwm");
    if (!g_znwm) {
        LOGW("input: operator new not found, PC controls disabled");
        return;
    }
    g_orig_kbd_setmap = *gptr<u64>(kKbdVtSetMapping);
    g_orig_mouse_setmap = *gptr<u64>(kMouseVtSetMapping);
    if (g_orig_kbd_setmap != 0x100020168 || g_orig_mouse_setmap != 0x1000210c8) {
        LOGW("input: unexpected binary layout, PC controls disabled");
        return;
    }
    *gptr<u64>(kKbdVtSetMapping) = hle::add_function("emu_hook_keyboard_set_mapping", hook_kbd_setmap, true);
    *gptr<u64>(kMouseVtSetMapping) = hle::add_function("emu_hook_mouse_set_mapping", hook_mouse_setmap, true);
}

bool game_input_ready() { return g_input_ready; }

bool in_world() { return g_in_world; }

void game_key(int key, bool down) {
    if (key <= 0 || key > 255) return;
    if (controls::is_key_for("button.inventory", key)) {
        if (down) {
            if (g_in_world) g_last_open_input = window::now();
            g_e_as_escape = !g_in_world && g_inventory_screen;
        }
        if (g_e_as_escape) key = 0x1b;
    }
    KeyboardAction ka{down ? 1 : 0, (u8)key, {}};
    u64* vec = gptr<u64>(kKbInputs);  // begin, end, capacity
    if (vec[1] + sizeof ka <= vec[2]) {
        memcpy(gptr<void>(vec[1]), &ka, sizeof ka);
        vec[1] += sizeof ka;
    } else {
        g_cpu->call(kKbPushSlow, {kKbInputs, (u64)(uintptr_t)&ka});  // guest VA == host VA
    }
    gptr<s32>(kKbStates)[key] = ka.state;
}

// action: 0 move, 1 left, 2 right, 3 middle, 4 wheel; data: 1 down / 0 up (wheel: +-1)
void game_mouse(int action, int data, int x, int y, int dx, int dy) {
    if (action >= 1 && action <= 3 && data == 1 && g_in_world)
        for (auto& b : controls::mouse())
            if (b.code == action && b.button == "button.build_or_interact") g_last_open_input = window::now();
    MouseAction ma{(s16)x, (s16)y, (s16)dx, (s16)dy, (u8)action, (s8)data, {}, 0};
    u64* vec = gptr<u64>(kMouse + 0x18);
    if (vec[1] + sizeof ma <= vec[2]) {
        memcpy(gptr<void>(vec[1]), &ma, sizeof ma);
        vec[1] += sizeof ma;
    } else {
        g_cpu->call(kMousePushSlow, {kMouse + 0x18, (u64)(uintptr_t)&ma});
    }
    if (action >= 1 && action <= 3) *gptr<u8>(kMouse + 0x10 + action) = (u8)data;
    *gptr<s16>(kMouse + 4) = (s16)x;
    *gptr<s16>(kMouse + 6) = (s16)y;
}

}  // namespace patches
