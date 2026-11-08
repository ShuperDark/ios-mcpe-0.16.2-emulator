#include "controls.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string_view>

namespace controls {
namespace {

std::mutex g_lock;
std::wstring g_path;
FILETIME g_mtime{};
std::vector<Binding> g_keyboard, g_mouse;
float g_sensitivity = 1.0f;
bool g_invert_y = false;
float g_scroll = 1.0f;
bool g_fullscreen = false;
bool g_classic = true;
bool g_vsync = false;
int g_max_fps = -1;  // -1 = auto (monitor refresh rate)
RemoteConfig g_remote;

// friendly action name -> game button
struct Action { const char* name; const char* button; };
const Action kActions[] = {
    {"forward", "button.up"},          {"back", "button.down"},
    {"left", "button.left"},           {"right", "button.right"},
    {"jump", "button.jump"},           {"sneak", "button.sneak"},
    {"sprint", "button.sprint"},       {"attack", "button.destroy_or_attack"},
    {"use", "button.build_or_interact"},
    {"inventory", "button.inventory"}, {"chat", "button.chat"},
    {"drop", "button.drop"},           {"perspective", "button.third_person_view"},
    {"hide_gui", "button.hide_gui"},
    {"hotbar1", "button.slot1"}, {"hotbar2", "button.slot2"}, {"hotbar3", "button.slot3"},
    {"hotbar4", "button.slot4"}, {"hotbar5", "button.slot5"}, {"hotbar6", "button.slot6"},
    {"hotbar7", "button.slot7"}, {"hotbar8", "button.slot8"}, {"hotbar9", "button.slot9"},
};

const char kDefaultFile[] =
    "; iOS MCPE 0.16.2 Emulator - 操作設定 (controls)\n"
    ";\n"
    "; 書式:  動作 = キー [, キー ...]      (キーを空にするとその動作は割り当てなし)\n"
    "; 保存すると、次に画面を開閉したとき (ワールドに入る / インベントリを閉じる など) に反映されます。\n"
    ";\n"
    "; キー名: A-Z, 0-9, F1-F24, Space, Shift, Ctrl, Tab, Enter, Backspace, CapsLock,\n"
    ";         Up, Down, Left, Right, Insert, Delete, Home, End, PageUp, PageDown,\n"
    ";         Numpad0-Numpad9, Minus, Equals, LBracket, RBracket, Semicolon, Quote,\n"
    ";         Comma, Period, Slash, Backslash, Grave, #<仮想キーコード(10進)>\n"
    "; マウス: MouseLeft, MouseRight, MouseMiddle\n"
    "; 動作名の代わりにゲーム内部のボタン名 (例: button.sneak_toggle) も書けます。\n"
    "; Esc (ポーズ / 画面を閉じる) は固定です。\n"
    "\n"
    "[controls]\n"
    "forward     = W\n"
    "back        = S\n"
    "left        = A\n"
    "right       = D\n"
    "jump        = Space\n"
    "sneak       = Shift\n"
    "sprint      = Ctrl\n"
    "attack      = MouseLeft\n"
    "use         = MouseRight\n"
    "inventory   = E\n"
    "chat        = T\n"
    "drop        = Q\n"
    "perspective = F5\n"
    "hide_gui    = F1\n"
    "hotbar1     = 1\n"
    "hotbar2     = 2\n"
    "hotbar3     = 3\n"
    "hotbar4     = 4\n"
    "hotbar5     = 5\n"
    "hotbar6     = 6\n"
    "hotbar7     = 7\n"
    "hotbar8     = 8\n"
    "hotbar9     = 9\n"
    "\n"
    "[mouse]\n"
    "; 視点移動の感度 (1.0 = 標準)\n"
    "sensitivity = 1.0\n"
    "; 上下反転 (true / false)\n"
    "invert_y    = false\n"
    "; ホイールのスクロール量 (1.0 = 標準)\n"
    "scroll_speed = 1.0\n"
    "\n"
    "[window]\n"
    "; 起動時にフルスクリーンにする (F11 / Alt+Enter でいつでも切り替え可能)\n"
    "fullscreen  = false\n"
    "; フレームレートの上限 (auto = モニタのリフレッシュレート, 0 = 上限なし, 数値 = その fps)\n"
    "max_fps     = auto\n"
    "; 垂直同期 (true にするとティアリングは消えますが、60Hz 以外のモニタではカクつきます)\n"
    "vsync       = false\n"
    "\n"
    "[ui]\n"
    "; 画面の種類: classic = PC 版 (カーソルでアイテムを掴む) / pocket = スマホ版 (iPhone 接続中は常にスマホ版)\n"
    "screens     = classic\n"
    "\n"
    "[remote]\n"
    "; iPhone などのブラウザ (Safari) に画面を出して操作する機能 (false で無効)\n"
    "enabled     = true\n"
    "; 待ち受けるポート (iPhone では http://<PC の IP アドレス>:<port> を開く)\n"
    "port        = 8080\n"
    "; 接続中の描画解像度 (長辺のピクセル数。大きいほど鮮明 / 重い)\n"
    "resolution  = 1280\n"
    "; 画質 (JPEG の品質 10-100)\n"
    "quality     = 70\n"
    "; 送るフレームレートの上限\n"
    "fps         = 60\n"
    "; 接続中も PC から音を出す (false = 音は iPhone だけ)\n"
    "pc_audio    = false\n";

std::string trim(std::string s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// returns >0 keyboard VK, <0 mouse button (-1 left, -2 right, -3 middle), 0 unknown
int parse_key(const std::string& name) {
    std::string k = lower(name);
    if (k.empty()) return 0;
    if (k.size() == 1) {
        char c = k[0];
        if (c >= 'a' && c <= 'z') return toupper(c);
        if (c >= '0' && c <= '9') return c;
        switch (c) {
        case '-': return VK_OEM_MINUS; case '=': return VK_OEM_PLUS; case '[': return VK_OEM_4; case ']': return VK_OEM_6;
        case ';': return VK_OEM_1; case '\'': return VK_OEM_7; case ',': return VK_OEM_COMMA; case '.': return VK_OEM_PERIOD;
        case '/': return VK_OEM_2; case '\\': return VK_OEM_5; case '`': return VK_OEM_3;
        }
        return 0;
    }
    if (k[0] == '#') return std::clamp(atoi(k.c_str() + 1), 0, 255);
    if (k[0] == 'f' && isdigit((unsigned char)k[1])) {
        int n = atoi(k.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    if (k.rfind("numpad", 0) == 0 && k.size() == 7 && isdigit((unsigned char)k[6])) return VK_NUMPAD0 + (k[6] - '0');
    static const struct { const char* n; int vk; } names[] = {
        {"space", VK_SPACE}, {"shift", VK_SHIFT}, {"ctrl", VK_CONTROL}, {"control", VK_CONTROL}, {"tab", VK_TAB},
        {"enter", VK_RETURN}, {"return", VK_RETURN}, {"backspace", VK_BACK}, {"capslock", VK_CAPITAL},
        {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT}, {"insert", VK_INSERT},
        {"delete", VK_DELETE}, {"home", VK_HOME}, {"end", VK_END}, {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT},
        {"minus", VK_OEM_MINUS}, {"equals", VK_OEM_PLUS}, {"lbracket", VK_OEM_4}, {"rbracket", VK_OEM_6},
        {"semicolon", VK_OEM_1}, {"quote", VK_OEM_7}, {"comma", VK_OEM_COMMA}, {"period", VK_OEM_PERIOD},
        {"slash", VK_OEM_2}, {"backslash", VK_OEM_5}, {"grave", VK_OEM_3},
        {"mouseleft", -1}, {"mouseright", -2}, {"mousemiddle", -3},
    };
    for (auto& n : names)
        if (k == n.n) return n.vk;
    return 0;
}

const char* button_for(const std::string& action) {
    std::string a = lower(action);
    if (a.rfind("button.", 0) == 0) return nullptr;  // raw name, handled by the caller
    for (auto& x : kActions)
        if (a == x.name) return x.button;
    return "";
}

void parse(const std::string& text) {
    std::vector<Binding> kb, ms;
    float sens = 1.0f, scroll = 1.0f;
    bool inv = false, fullscreen = false, classic = true, vs = false;
    int fps = -1;
    RemoteConfig remote;
    std::string section;
    size_t pos = 0;
    int line_no = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        line_no++;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            section = lower(trim(line.substr(1, line.find(']') - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (size_t c = value.find(';'); c != std::string::npos) value = trim(value.substr(0, c));
        if (section == "mouse") {
            if (lower(key) == "sensitivity") sens = std::clamp((float)atof(value.c_str()), 0.05f, 20.0f);
            else if (lower(key) == "invert_y") inv = lower(value) == "true" || value == "1" || lower(value) == "yes";
            else if (lower(key) == "scroll_speed") scroll = std::clamp((float)atof(value.c_str()), 0.1f, 10.0f);
            continue;
        }
        if (section == "remote") {
            std::string k = lower(key);
            if (k == "enabled") remote.enabled = !(lower(value) == "false" || value == "0" || lower(value) == "no");
            else if (k == "port") remote.port = std::clamp(atoi(value.c_str()), 1, 65535);
            else if (k == "resolution") remote.resolution = std::clamp(atoi(value.c_str()), 320, 4096);
            else if (k == "quality") remote.quality = std::clamp(atoi(value.c_str()), 10, 100);
            else if (k == "fps") remote.fps = std::clamp(atoi(value.c_str()), 1, 240);
            else if (k == "pc_audio") remote.pc_audio = lower(value) == "true" || value == "1" || lower(value) == "yes";
            continue;
        }
        if (section == "ui") {
            if (lower(key) == "screens") classic = lower(value) != "pocket";
            continue;
        }
        if (section == "window") {
            if (lower(key) == "fullscreen") fullscreen = lower(value) == "true" || value == "1" || lower(value) == "yes";
            else if (lower(key) == "vsync") vs = lower(value) == "true" || value == "1" || lower(value) == "yes";
            else if (lower(key) == "max_fps") fps = lower(value) == "auto" ? -1 : std::clamp(atoi(value.c_str()), 0, 1000);
            continue;
        }
        if (section != "controls") continue;
        const char* b = button_for(key);
        std::string button = b ? std::string(b) : lower(key);
        if (button.empty()) {
            LOGW("controls.ini:%d: unknown action '%s'", line_no, key.c_str());
            continue;
        }
        size_t p = 0;
        while (p <= value.size()) {
            size_t c = value.find(',', p);
            if (c == std::string::npos) c = value.size();
            std::string kname = trim(value.substr(p, c - p));
            p = c + 1;
            if (kname.empty()) continue;
            int code = parse_key(kname);
            if (code == 0 || code == VK_ESCAPE) LOGW("controls.ini:%d: unknown key '%s'", line_no, kname.c_str());
            else if (code < 0) ms.push_back({button, -code});
            else kb.push_back({button, code});
        }
    }
    g_keyboard = std::move(kb);
    g_mouse = std::move(ms);
    g_sensitivity = sens;
    g_invert_y = inv;
    g_scroll = scroll;
    g_fullscreen = fullscreen;
    g_classic = classic;
    g_vsync = vs;
    g_max_fps = fps;
    g_remote = remote;
    LOGI("controls: %zu key and %zu mouse bindings, sensitivity %.2f%s", g_keyboard.size(), g_mouse.size(), g_sensitivity,
         g_invert_y ? ", inverted Y" : "");
}

bool file_time(FILETIME* ft) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &a)) return false;
    *ft = a.ftLastWriteTime;
    return true;
}

void load() {
    FILE* f = _wfopen(g_path.c_str(), L"rb");
    if (!f) {
        parse(kDefaultFile);
        return;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
    parse(text);
}

}  // namespace

void init(const std::wstring& path) {
    std::lock_guard lk(g_lock);
    g_path = path;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (FILE* f = _wfopen(path.c_str(), L"wb")) {
            fwrite(kDefaultFile, 1, sizeof kDefaultFile - 1, f);
            fclose(f);
            LOGI("controls: wrote default %s", narrow(path).c_str());
        }
    }
    file_time(&g_mtime);
    load();
}

bool reload_if_changed() {
    std::lock_guard lk(g_lock);
    FILETIME ft;
    if (g_path.empty() || !file_time(&ft) || CompareFileTime(&ft, &g_mtime) == 0) return false;
    g_mtime = ft;
    load();
    return true;
}

const std::vector<Binding>& keyboard() {
    if (g_keyboard.empty() && g_mouse.empty() && g_path.empty()) parse(kDefaultFile);
    return g_keyboard;
}
const std::vector<Binding>& mouse() { return g_mouse; }
float mouse_sensitivity() { return g_sensitivity; }
bool invert_mouse_y() { return g_invert_y; }
float scroll_speed() { return g_scroll; }
bool start_fullscreen() { return g_fullscreen; }
bool classic_screens() { return g_classic; }
bool vsync() { return g_vsync; }
int max_fps() { return g_max_fps; }
RemoteConfig remote() { return g_remote; }

bool is_key_for(const char* button, int vk) {
    for (auto& b : g_keyboard)
        if (b.code == vk && b.button == button) return true;
    return false;
}

}  // namespace controls
