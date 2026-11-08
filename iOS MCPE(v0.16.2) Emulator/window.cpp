#include "window.h"
#include "controls.h"
#include "remote.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>

#include <algorithm>
#include <array>
#include <deque>
#include <string>
#include <tuple>
#include <vector>
#include <mutex>

namespace window {
namespace {

HWND g_hwnd;
HDC g_dc;
HGLRC g_rc;
std::mutex g_lock;
std::deque<Event> g_events;
int g_w = 1280, g_h = 720;    // the game's render size (= client area unless a phone sets it)
int g_cw = 1280, g_ch = 720;  // client area of the window
int g_ovr_w = 0, g_ovr_h = 0; // render size requested by remote.cpp (0 = follow the window)
bool g_remote = false;        // a phone is connected: the PC mouse does not go to the game
std::wstring g_title = L"Minecraft PE 0.16.2 (iOS)", g_title_suffix;
bool g_title_dirty = false;
bool g_mouse_down[3];
LARGE_INTEGER g_freq, g_t0;

// GL entry points needed for presentation
typedef void(APIENTRY* PFNGENFB)(GLsizei, GLuint*);
typedef void(APIENTRY* PFNBINDFB)(GLenum, GLuint);
typedef void(APIENTRY* PFNFBRB)(GLenum, GLenum, GLenum, GLuint);
typedef void(APIENTRY* PFNBLIT)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef GLenum(APIENTRY* PFNCHECKFB)(GLenum);
typedef BOOL(WINAPI* PFNSWAPINTERVAL)(int);
typedef HGLRC(WINAPI* PFNCREATECTX)(HDC, HGLRC, const int*);
PFNGENFB pglGenFramebuffers;
PFNBINDFB pglBindFramebuffer;
PFNFBRB pglFramebufferRenderbuffer;
PFNBLIT pglBlitFramebuffer;
GLuint g_present_fbo;
GLuint g_present_rb_attached;

double ts() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart - g_t0.QuadPart) / double(g_freq.QuadPart);
}

void push(Event e) {
    e.time = ts();
    std::lock_guard lk(g_lock);
    g_events.push_back(e);
}

// requests from other threads (remote.cpp), applied by poll() on the window thread
int g_req_w = -1, g_req_h = -1;  // guarded by g_lock
std::wstring g_req_suffix;
bool g_req_suffix_set = false;
void release_buttons(float x, float y);
void update_capture();

void apply_requests() {
    int w, h;
    {
        std::lock_guard lk(g_lock);
        w = g_req_w;
        h = g_req_h;
        g_req_w = g_req_h = -1;
        if (g_req_suffix_set) {
            g_title_suffix = g_req_suffix;
            g_req_suffix_set = false;
            g_title_dirty = true;
        }
    }
    if (g_title_dirty && g_hwnd) {
        g_title_dirty = false;
        SetWindowTextW(g_hwnd, (g_title + g_title_suffix).c_str());
    }
    if (w < 0) return;
    bool remote = w > 0 && h > 0;
    if (remote != g_remote) {
        release_buttons(g_w / 2.0f, g_h / 2.0f);
        g_remote = remote;
        update_capture();
    }
    g_ovr_w = remote ? w : 0;
    g_ovr_h = remote ? h : 0;
    int nw = remote ? w : g_cw, nh = remote ? h : g_ch;
    if (nw != g_w || nh != g_h) {
        g_w = nw;
        g_h = nh;
        push({EvResize, 0, (float)nw, (float)nh});
    }
}

u32 mods() {
    u32 m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= 1;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= 2;
    if (GetKeyState(VK_MENU) & 0x8000) m |= 4;
    return m;
}

// ---- PC-style controls ----
// Keys and the mouse are delivered to the game's own keyboard / mouse input (see
// patches.cpp). While the player is in the world (no screen open) the mouse is
// captured: the cursor is hidden and relative motion turns the camera. On screens the
// cursor is visible and its position, buttons and wheel go to the game as a mouse
// (pointer, item dragging, scrolling). EMU_TOUCH_MENUS=1 sends touches there instead.
bool g_text_input = false;
bool g_gameplay = false;   // the game's in-world input mapping is active
bool g_focused = false;
bool g_captured = false;
bool g_held_keys[256];
bool g_held_buttons[3];
const bool g_no_mouselook = getenv("EMU_NO_MOUSELOOK") != nullptr;
const bool g_touch_menus = getenv("EMU_TOUCH_MENUS") != nullptr;

u64 g_frames = 0;
bool g_fullscreen = false;
WINDOWPLACEMENT g_saved_placement{sizeof(WINDOWPLACEMENT)};

// Borderless fullscreen on the monitor the window is on (F11 / Alt+Enter).
void set_fullscreen(bool on) {
    if (!g_hwnd || on == g_fullscreen) return;
    g_fullscreen = on;
    if (on) {
        GetWindowPlacement(g_hwnd, &g_saved_placement);
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongW(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        SetWindowLongW(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(g_hwnd, &g_saved_placement);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    LOGI("fullscreen %s", on ? "on" : "off");
}

void release_buttons(float x, float y) {
    for (int b = 0; b < 3; b++) {
        if (g_held_buttons[b]) {
            g_held_buttons[b] = false;
            push({EvGameMouseButton, b + 1, x, y, 0});
        }
        if (g_mouse_down[b]) {
            g_mouse_down[b] = false;
            push({EvPointerUp, b, x, y});
        }
    }
}

void update_capture() {
    bool want = g_gameplay && g_focused && !g_text_input && !g_no_mouselook && !g_remote && g_hwnd;
    if (want == g_captured) return;
    g_captured = want;
    release_buttons(g_w / 2.0f, g_h / 2.0f);
    if (want) {
        ReleaseCapture();
        RAWINPUTDEVICE rid{0x01, 0x02, 0, g_hwnd};  // generic desktop / mouse
        RegisterRawInputDevices(&rid, 1, sizeof rid);
        RECT r;
        GetClientRect(g_hwnd, &r);
        POINT c{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
        ClientToScreen(g_hwnd, &c);
        SetCursorPos(c.x, c.y);
        MapWindowPoints(g_hwnd, nullptr, (POINT*)&r, 2);
        ClipCursor(&r);
        while (ShowCursor(FALSE) >= 0) {}
        push({EvGameMouseMove, 0, 0, 0});  // switches the game to keyboard/mouse mode
    } else {
        RAWINPUTDEVICE rid{0x01, 0x02, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&rid, 1, sizeof rid);
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
        if (g_hwnd && g_focused && !g_remote) {  // put the cursor in the middle, like the desktop edition
            POINT c{g_w / 2, g_h / 2};
            ClientToScreen(g_hwnd, &c);
            SetCursorPos(c.x, c.y);
            push({EvGameMousePos, 0, g_w / 2.0f, g_h / 2.0f});
        }
    }
}

void release_held_keys() {  // focus loss: Windows will not send the key-ups
    for (int k = 0; k < 256; k++)
        if (g_held_keys[k]) {
            g_held_keys[k] = false;
            push({EvKeyUp, k, 0, 0, 0, 0});
        }
}

LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE: push({EvClose}); return 0;
    case WM_SIZE: {
        int w = LOWORD(lp), hh = HIWORD(lp);
        if (w > 0 && hh > 0) {
            g_cw = w;
            g_ch = hh;
            if (!g_ovr_w && (w != g_w || hh != g_h)) {
                g_w = w;
                g_h = hh;
                push({EvResize, 0, (float)w, (float)hh});
            }
        }
        if (g_captured) {  // keep the clip rectangle in sync
            RECT r;
            GetClientRect(h, &r);
            MapWindowPoints(h, nullptr, (POINT*)&r, 2);
            ClipCursor(&r);
        }
        return 0;
    }
    case WM_SETFOCUS:
        g_focused = true;
        update_capture();
        push({EvFocus});
        return 0;
    case WM_KILLFOCUS:
        g_focused = false;
        release_held_keys();
        release_buttons(g_w / 2.0f, g_h / 2.0f);
        update_capture();
        push({EvBlur});
        return 0;
    case WM_INPUT: {
        if (!g_captured) break;
        RAWINPUT ri;
        UINT sz = sizeof ri;
        if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
            ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) &&
            (ri.data.mouse.lLastX || ri.data.mouse.lLastY))
            push({EvGameMouseMove, 0, (float)ri.data.mouse.lLastX, (float)ri.data.mouse.lLastY});
        break;  // DefWindowProc must see WM_INPUT
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        if (g_remote) return 0;  // the phone's touches drive the game
        int b = msg == WM_LBUTTONDOWN ? 0 : msg == WM_RBUTTONDOWN ? 1 : 2;
        if (g_captured) {
            g_held_buttons[b] = true;
            push({EvGameMouseButton, b + 1, g_w / 2.0f, g_h / 2.0f, 1});
            return 0;
        }
        SetCapture(h);
        float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
        if (!g_touch_menus) {
            g_held_buttons[b] = true;
            push({EvGameMousePos, 0, x, y});
            push({EvGameMouseButton, b + 1, x, y, 1});
            return 0;
        }
        g_mouse_down[b] = true;
        push({EvPointerDown, b, x, y, 0, mods()});
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        if (g_remote) return 0;  // the phone's touches drive the game
        int b = msg == WM_LBUTTONUP ? 0 : msg == WM_RBUTTONUP ? 1 : 2;
        float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
        if (g_captured) x = g_w / 2.0f, y = g_h / 2.0f;
        if (g_held_buttons[b]) {
            g_held_buttons[b] = false;
            push({EvGameMouseButton, b + 1, x, y, 0});
        }
        if (g_mouse_down[b]) {
            g_mouse_down[b] = false;
            push({EvPointerUp, b, x, y, 0, mods()});
        }
        bool any = false;
        for (int k = 0; k < 3; k++) any |= g_mouse_down[k] || g_held_buttons[k];
        if (!any) ReleaseCapture();
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (g_remote) return 0;  // the phone's touches drive the game
        if (g_captured) return 0;
        float x = (float)GET_X_LPARAM(lp), y = (float)GET_Y_LPARAM(lp);
        if (!g_touch_menus) {
            push({EvGameMousePos, 0, x, y});
            return 0;
        }
        for (int b = 0; b < 3; b++)
            if (g_mouse_down[b]) push({EvPointerMove, b, x, y, 0, mods()});
        if (!g_mouse_down[0] && !g_mouse_down[1] && !g_mouse_down[2]) push({EvPointerMove, -1, x, y, 0, mods()});
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (g_remote) return 0;  // the phone's touches drive the game
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &p);
        if (g_captured) p = {g_w / 2, g_h / 2};
        // one event per notch (high-resolution wheels send fractions of WHEEL_DELTA)
        static int accum = 0;
        accum += GET_WHEEL_DELTA_WPARAM(wp);
        while (accum >= WHEEL_DELTA || accum <= -WHEEL_DELTA) {
            int dir = accum > 0 ? 1 : -1;
            accum -= dir * WHEEL_DELTA;
            if (g_captured || !g_touch_menus) push({EvGameMouseButton, 4, (float)p.x, (float)p.y, (u32)dir});
            else push({EvWheel, dir * WHEEL_DELTA, (float)p.x, (float)p.y, 0, mods()});
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wp == VK_F4 && (GetKeyState(VK_MENU) & 0x8000)) break;
        if ((wp == VK_F11 || (wp == VK_RETURN && (lp & (1 << 29)))) && !(lp & (1 << 30))) {
            set_fullscreen(!g_fullscreen);
            return 0;
        }
        if (wp == VK_F11) return 0;
        if (wp == VK_MENU || wp == VK_F10) break;  // keep the system menu keys working
        if (wp < 256) g_held_keys[wp] = true;
        push({EvKeyDown, (s32)wp, 0, 0, 0, mods() | ((lp & (1 << 30)) ? 0x100u : 0u)});
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wp == VK_F11 || (wp == VK_RETURN && (lp & (1 << 29)))) return 0;
        if (wp < 256) g_held_keys[wp] = false;
        push({EvKeyUp, (s32)wp, 0, 0, 0, mods()});
        return 0;
    case WM_CHAR: {
        static wchar_t hi = 0;
        u32 c = (u32)wp;
        if (c >= 0xd800 && c < 0xdc00) { hi = (wchar_t)c; return 0; }
        if (c >= 0xdc00 && c < 0xe000 && hi) { c = 0x10000 + ((hi - 0xd800) << 10) + (c - 0xdc00); hi = 0; }
        push({EvChar, 0, 0, 0, c, mods()});
        return 0;
    }
    case WM_SYSCHAR:
        if (wp == VK_RETURN) return 0;
        break;
    case WM_ERASEBKGND: return 1;
    default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

void* gl_proc(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (!p || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) {
        static HMODULE gl = LoadLibraryW(L"opengl32.dll");
        p = (void*)GetProcAddress(gl, name);
    }
    return p;
}

bool exists() { return g_hwnd != nullptr; }

bool create(int width, int height, const char* title) {
    if (g_hwnd) return true;
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_t0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"iOSMCPEWindow";
    RegisterClassExW(&wc);
    RECT r{0, 0, width, height};
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&r, style, FALSE);
    g_w = g_cw = width;
    g_h = g_ch = height;
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, widen(title).c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                             r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return false;
    g_dc = GetDC(g_hwnd);
    PIXELFORMATDESCRIPTOR pfd{sizeof pfd, 1};
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(g_dc, &pfd);
    SetPixelFormat(g_dc, pf, &pfd);
    HGLRC tmp = wglCreateContext(g_dc);
    wglMakeCurrent(g_dc, tmp);
    auto create_attribs = (PFNCREATECTX)wglGetProcAddress("wglCreateContextAttribsARB");
    if (create_attribs) {
        const int attribs[] = {0x2091, 4, 0x2092, 3,                    // version 4.3
                               0x9126, 0x00000002,                      // compatibility profile
                               0};
        g_rc = create_attribs(g_dc, nullptr, attribs);
        if (!g_rc) {
            const int a2[] = {0x2091, 3, 0x2092, 3, 0x9126, 0x2, 0};
            g_rc = create_attribs(g_dc, nullptr, a2);
        }
    }
    if (g_rc) {
        wglMakeCurrent(g_dc, g_rc);
        wglDeleteContext(tmp);
    } else {
        g_rc = tmp;
    }
    LOGI("OpenGL: %s / %s / %s", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    pglGenFramebuffers = (PFNGENFB)gl_proc("glGenFramebuffers");
    pglBindFramebuffer = (PFNBINDFB)gl_proc("glBindFramebuffer");
    pglFramebufferRenderbuffer = (PFNFBRB)gl_proc("glFramebufferRenderbuffer");
    pglBlitFramebuffer = (PFNBLIT)gl_proc("glBlitFramebuffer");
    // vsync off by default: the game is paced at max_fps by the guest's display link with a
    // 1 ms timer, which stays even on 120/144/165 Hz monitors (vsync would alternate frames)
    if (auto si = (PFNSWAPINTERVAL)wglGetProcAddress("wglSwapIntervalEXT")) si(controls::vsync() ? 1 : 0);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    if (controls::start_fullscreen()) set_fullscreen(true);
    return true;
}

void size(int* w, int* h) {
    *w = g_w;
    *h = g_h;
}

// debug: EMU_AUTOTAP="12:640,370;20:300,200" taps (window pixels) at the given seconds.
// Other entries: "20:key=E" (press+release; ESC or #<vk> also work), "20:keydown=W",
// "22:keyup=W", "20:mouse=100,0" (relative motion), "20:mbutton=1,1" (game mouse button
// 1 left / 2 right, 1 down / 0 up).
static int autotap_vk(const char* k) {
    if (_stricmp(k, "ESC") == 0) return VK_ESCAPE;
    if (k[0] == '#') return atoi(k + 1);
    return toupper((u8)k[0]);
}
static void autotap() {
    enum { Tap, Key, KeyDown, KeyUp, Move, MButton };
    struct Item { double t; int kind; float x, y; };
    static std::vector<Item> items;
    static bool parsed = false;
    static size_t next = 0;
    if (!parsed) {
        parsed = true;
        if (const char* s = getenv("EMU_AUTOTAP")) {
            std::string str = s;
            size_t pos = 0;
            while (pos < str.size()) {
                size_t e = str.find(';', pos);
                if (e == std::string::npos) e = str.size();
                double t;
                float x, y;
                char key[16] = {};
                std::string item = str.substr(pos, e - pos);
                if (sscanf(item.c_str(), "%lf:keydown=%15[^;]", &t, key) == 2) items.push_back({t, KeyDown, 0, (float)autotap_vk(key)});
                else if (sscanf(item.c_str(), "%lf:keyup=%15[^;]", &t, key) == 2) items.push_back({t, KeyUp, 0, (float)autotap_vk(key)});
                else if (sscanf(item.c_str(), "%lf:key=%15[^;]", &t, key) == 2) items.push_back({t, Key, 0, (float)autotap_vk(key)});
                else if (sscanf(item.c_str(), "%lf:mouse=%f,%f", &t, &x, &y) == 3) items.push_back({t, Move, x, y});
                else if (sscanf(item.c_str(), "%lf:mbutton=%f,%f", &t, &x, &y) == 3) items.push_back({t, MButton, x, y});
                else if (sscanf(item.c_str(), "%lf:%f,%f", &t, &x, &y) == 3) items.push_back({t, Tap, x, y});
                pos = e + 1;
            }
            std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.t < b.t; });
        }
    }
    while (next < items.size() && ts() >= items[next].t) {
        Item it = items[next++];
        LOGI("autotap kind %d %.0f,%.0f", it.kind, it.x, it.y);
        switch (it.kind) {
        case Key:
            PostMessageW(g_hwnd, WM_KEYDOWN, (WPARAM)it.y, 1);
            PostMessageW(g_hwnd, WM_KEYUP, (WPARAM)it.y, 1 | (1u << 30) | (1u << 31));
            break;
        case KeyDown: PostMessageW(g_hwnd, WM_KEYDOWN, (WPARAM)it.y, 1); break;
        case KeyUp: PostMessageW(g_hwnd, WM_KEYUP, (WPARAM)it.y, 1 | (1u << 30) | (1u << 31)); break;
        case Move: push({EvGameMouseMove, 0, it.x, it.y}); break;
        case MButton: push({EvGameMouseButton, (s32)it.x, g_w / 2.0f, g_h / 2.0f, (u32)(s32)it.y}); break;
        default:
            if (g_touch_menus) {
                push({EvPointerDown, 0, it.x, it.y});
                push({EvPointerUp, 0, it.x, it.y});
            } else {  // same path as a real mouse click
                push({EvGameMousePos, 0, it.x, it.y});
                push({EvGameMouseButton, 1, it.x, it.y, 1});
                push({EvGameMouseButton, 1, it.x, it.y, 0});
            }
            break;
        }
    }
}

int poll(Event* out, int max) {
    apply_requests();
    autotap();
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    std::lock_guard lk(g_lock);
    int n = 0;
    while (n < max && !g_events.empty()) {
        out[n++] = g_events.front();
        g_events.pop_front();
    }
    return n;
}

void wait(double seconds) {
    if (seconds <= 0) return;
    {
        std::lock_guard lk(g_lock);
        if (!g_events.empty()) return;
    }
    MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)(seconds * 1000.0), QS_ALLINPUT);
}

void present(u32 rb, int rb_w, int rb_h) {
    if (!g_hwnd || !pglBlitFramebuffer) return;
    GLint prev_draw = 0, prev_read = 0;
    glGetIntegerv(0x8CA6, &prev_draw);  // GL_DRAW_FRAMEBUFFER_BINDING
    glGetIntegerv(0x8CAA, &prev_read);  // GL_READ_FRAMEBUFFER_BINDING
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (!g_present_fbo) pglGenFramebuffers(1, &g_present_fbo);
    pglBindFramebuffer(0x8CA8, g_present_fbo);  // GL_READ_FRAMEBUFFER
    // attach every frame: when the game recreates its framebuffer (window resized) the new
    // renderbuffer usually gets the deleted one's name, and a cached attachment would keep
    // showing the orphaned old storage
    pglFramebufferRenderbuffer(0x8CA8, 0x8CE0, 0x8D41, rb);  // COLOR_ATTACHMENT0, GL_RENDERBUFFER
    g_present_rb_attached = rb;
    pglBindFramebuffer(0x8CA9, 0);  // GL_DRAW_FRAMEBUFFER
    if (scissor) glDisable(GL_SCISSOR_TEST);
    remote::capture(rb_w, rb_h);  // the phone's copy of the frame (needs the read framebuffer)
    // keep the aspect ratio (the render size differs from the window while a phone is connected)
    int dx = 0, dy = 0, dw = g_cw, dh = g_ch;
    if (rb_w > 0 && rb_h > 0 && (long long)rb_w * g_ch != (long long)rb_h * g_cw) {
        if ((long long)rb_w * g_ch > (long long)rb_h * g_cw) dh = (int)((long long)g_cw * rb_h / rb_w);
        else dw = (int)((long long)g_ch * rb_w / rb_h);
        dx = (g_cw - dw) / 2;
        dy = (g_ch - dh) / 2;
    }
    glViewport(0, 0, g_cw, g_ch);
    glClearColor(0, 0, 0, 1);
    if (rb_w != g_cw || rb_h != g_ch) glClear(GL_COLOR_BUFFER_BIT);
    // debug: EMU_SCREENSHOT=<dir> writes the frame every EMU_SCREENSHOT_EVERY seconds (default 5)
    static const char* shot_dir = getenv("EMU_SCREENSHOT");
    static double next_shot = 0;
    static int shot_no = 0;
    if (shot_dir && ts() >= next_shot) {
        const char* ev = getenv("EMU_SCREENSHOT_EVERY");
        next_shot = ts() + (ev ? atof(ev) : 5.0);
        std::vector<u8> px(size_t(rb_w) * rb_h * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, rb_w, rb_h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        // flip to top-down and save as BMP (no codec needed)
        char path[512];
        snprintf(path, sizeof path, "%s/shot_%03d.bmp", shot_dir, shot_no++);
        FILE* f = fopen(path, "wb");
        if (f) {
            BITMAPFILEHEADER fh{};
            BITMAPINFOHEADER ih{};
            ih.biSize = sizeof ih;
            ih.biWidth = rb_w;
            ih.biHeight = rb_h;  // bottom-up, matches GL
            ih.biPlanes = 1;
            ih.biBitCount = 32;
            fh.bfType = 0x4d42;
            fh.bfOffBits = sizeof fh + sizeof ih;
            fh.bfSize = fh.bfOffBits + (DWORD)px.size();
            for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]);
            fwrite(&fh, sizeof fh, 1, f);
            fwrite(&ih, sizeof ih, 1, f);
            fwrite(px.data(), 1, px.size(), f);
            fclose(f);
            LOGI("screenshot %s", path);
        }
    }
    // both use a bottom-left origin: a straight (possibly scaling) blit
    pglBlitFramebuffer(0, 0, rb_w, rb_h, dx, dy, dx + dw, dy + dh, GL_COLOR_BUFFER_BIT, rb_w == dw && rb_h == dh ? GL_NEAREST : GL_LINEAR);
    SwapBuffers(g_dc);
    g_frames++;
    // frame statistics: EMU_SHOW_FPS=1 shows them in the title bar, -v logs them
    {
        static double t0 = ts(), last = ts(), worst = 0;
        static int frames = 0;
        static const bool show = getenv("EMU_SHOW_FPS") != nullptr;
        double now = ts();
        static const bool log_long = getenv("EMU_PROFILE") != nullptr;
        if (log_long && now - last > 0.05) LOGI("long frame: %.0f ms", (now - last) * 1000);
        worst = (std::max)(worst, now - last);
        last = now;
        frames++;
        if (now - t0 >= 2.0) {
            double fps = frames / (now - t0);
            LOGD("fps %.1f (slowest frame %.1f ms)", fps, worst * 1000);
            if (show) {
                wchar_t t[128];
                swprintf(t, 128, L"Minecraft PE 0.16.2 - %.1f fps (max %.1f ms)", fps, worst * 1000);
                SetWindowTextW(g_hwnd, (t + g_title_suffix).c_str());
            }
            t0 = now;
            frames = 0;
            worst = 0;
        }
    }
    if (scissor) glEnable(GL_SCISSOR_TEST);
    pglBindFramebuffer(0x8CA9, prev_draw);
    pglBindFramebuffer(0x8CA8, prev_read);
}

void set_text_input(bool on) {
    g_text_input = on;
    update_capture();
    remote::set_text_input(on);
}

void set_gameplay(bool on) {
    if (g_gameplay == on) return;
    g_gameplay = on;
    LOGI("gameplay input %s", on ? "on" : "off");
    update_capture();
}

bool text_input_active() { return g_text_input; }

void set_title(const char* t) {
    g_title = widen(t);
    if (g_hwnd) SetWindowTextW(g_hwnd, (g_title + g_title_suffix).c_str());
}

void set_title_suffix(const char* t) {
    std::lock_guard lk(g_lock);
    g_req_suffix = widen(t);
    g_req_suffix_set = true;
}

void post_event(Event e) { push(e); }

void set_game_size(int w, int h) {
    std::lock_guard lk(g_lock);
    g_req_w = w > 0 && h > 0 ? w : 0;
    g_req_h = w > 0 && h > 0 ? h : 0;
}

bool phone_connected() { return g_remote; }
void* native_handle() { return g_hwnd; }

double now() { return ts(); }
u64 frames_presented() { return g_frames; }
int refresh_rate() {
    // re-queried at most twice a second (the window may move to another monitor)
    static int hz = 60;
    static double next = 0;
    if (g_hwnd && ts() >= next) {
        next = ts() + 0.5;
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof mi;
        DEVMODEW dm{};
        dm.dmSize = sizeof dm;
        if (GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi) &&
            EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
            hz = (int)dm.dmDisplayFrequency;
    }
    return hz;
}
void toggle_fullscreen() { set_fullscreen(!g_fullscreen); }

int message_box(const char* title, const char* text, int buttons) {
    UINT type = buttons >= 2 ? MB_OKCANCEL : MB_OK;
    int r = MessageBoxW(g_hwnd, widen(text ? text : "").c_str(), widen(title ? title : "").c_str(), type | MB_ICONINFORMATION);
    return r == IDOK ? 0 : 1;
}

}  // namespace window
