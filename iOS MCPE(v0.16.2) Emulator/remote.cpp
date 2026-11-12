// Remote screen for a phone's browser (see remote.h).
//
//  phone (Safari, remote.html)                 emulator
//    GET /             ---------------------->  remote.html from the exe's folder
//    WebSocket /ws     <--------------------->  one phone at a time (a new one replaces it)
//      "s W H"           landscape size in physical pixels -> game render size
//      "d|m|u|c ID X Y"  touch down / move / up / cancel (X, Y = 0..1 of the image)
//      "t TEXT" "b N" "e" typed text / N backspaces / return
//      "a"               a frame was shown (at most 2 frames are in flight)
//      binary            <- 0x01 + JPEG frame, or 0x02 + u32 sample rate + 16-bit stereo PCM
//      "kb 1" / "kb 0"   <- the game opened / closed a text field
//      "pick"            <- the game wants an image (custom skin); the phone answers with
//      binary / "pc"        the file's bytes, or "pc" when cancelled
//      "pick media"      <- the same for a mod that takes any picture (PNG / JPEG / GIF)
//      "pick model"      <- the same for a mod that takes an MMD model (.zip / .pmx / .pmd)
//      "pick cancel"     <- the game no longer wants the picture (hide the picker)
//
// Threads: the network thread accepts connections and reads messages; the encoder
// thread turns frames into JPEG (WIC) and sends them; window::present (main thread)
// reads the frame back with a pixel buffer object one frame later, so it never waits
// for the GPU.
#include "remote.h"
#include "controls.h"
#include "window.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <bcrypt.h>
#include <wincodec.h>
#include <GL/gl.h>

#include <commdlg.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace patches {
bool in_world();
}

namespace remote {
namespace {

controls::RemoteConfig g_cfg;
std::string g_url;
SOCKET g_listen = INVALID_SOCKET;

std::mutex g_send_lock;              // serializes everything written to the WebSocket
SOCKET g_client = INVALID_SOCKET;    // guarded by g_send_lock for writes
std::atomic<bool> g_connected{false};
std::atomic<bool> g_broken{false};   // a send failed: the network thread closes the client
std::atomic<int> g_inflight{0};
std::atomic<bool> g_text_input{false};
std::atomic<int> g_game_w{0}, g_game_h{0};
std::atomic<bool> g_usb_peer{false};     // the phone is connected through USB (Personal Hotspot)

// image picker (custom skin): 0 idle, 1 pending, 2 picked, 3 cancelled
std::mutex g_pick_lock;
int g_pick_state = 0;
int g_pick_kind = kPickSkin;
bool g_pick_on_phone = false;
std::vector<u8> g_pick_data;
void pick_finish(std::vector<u8>* data) {
    std::lock_guard lk(g_pick_lock);
    if (g_pick_state != 1) return;
    if (data && !data->empty()) {
        g_pick_data = std::move(*data);
        g_pick_state = 2;
    } else {
        g_pick_state = 3;
    }
    LOGI("remote: image picker %s", g_pick_state == 2 ? "returned an image" : "cancelled");
}
bool g_touch_down[10];               // network thread only
bool g_mode_tap_done = false;        // network thread only

// ---------------------------------------------------------------- helpers
bool private_address(u32 a) {  // host byte order
    return (a >> 24) == 10 || (a >> 24) == 127 || (a >> 20) == (172u << 4 | 1) || (a >> 16) == (192u << 8 | 168) ||
           (a >> 16) == (169u << 8 | 254) || (a >> 22) == (100u << 2 | 1);  // 100.64/10 (VPNs such as Tailscale)
}

std::string base64(const u8* p, size_t n) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < n; i += 3) {
        u32 v = u32(p[i]) << 16 | (i + 1 < n ? u32(p[i + 1]) << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        o += t[v >> 18 & 63];
        o += t[v >> 12 & 63];
        o += i + 1 < n ? t[v >> 6 & 63] : '=';
        o += i + 2 < n ? t[v & 63] : '=';
    }
    return o;
}

std::string ws_accept(const std::string& key) {
    std::string s = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    u8 h[20] = {};
    BCRYPT_ALG_HANDLE alg;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0) {
        BCryptHash(alg, nullptr, 0, (PUCHAR)s.data(), (ULONG)s.size(), h, sizeof h);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return base64(h, sizeof h);
}

bool send_all(SOCKET s, const void* p, size_t n) {
    const char* c = static_cast<const char*>(p);
    while (n) {
        int r = send(s, c, (int)std::min<size_t>(n, 1 << 20), 0);
        if (r <= 0) return false;
        c += r;
        n -= (size_t)r;
    }
    return true;
}

// one unfragmented server -> client frame (servers do not mask)
bool ws_send(u8 opcode, const void* p, size_t n) {
    u8 h[10];
    size_t hn = 2;
    h[0] = 0x80 | opcode;
    if (n < 126) h[1] = (u8)n;
    else if (n < 65536) { h[1] = 126; h[2] = (u8)(n >> 8); h[3] = (u8)n; hn = 4; }
    else { h[1] = 127; for (int i = 0; i < 8; i++) h[2 + i] = (u8)(u64(n) >> (56 - 8 * i)); hn = 10; }
    std::lock_guard lk(g_send_lock);
    if (g_client == INVALID_SOCKET) return false;
    if (!send_all(g_client, h, hn) || !send_all(g_client, p, n)) {
        g_broken = true;
        return false;
    }
    return true;
}
bool ws_text(const std::string& s) { return ws_send(1, s.data(), s.size()); }

// The LAN address (adapter with the default route) and, when the iPhone shares its
// connection over USB (Personal Hotspot), the address of that USB network adapter.
struct Addresses {
    std::string lan, usb;
    // the iPhone's USB network adapter ("Apple Mobile Device Ethernet"): 0 not present,
    // 1 present but disconnected (Personal Hotspot off / not allowed), 2 connected but no
    // address from the iPhone yet, 3 usable (usb is set)
    int usb_state = 0;
    bool operator!=(const Addresses& o) const { return lan != o.lan || usb != o.usb || usb_state != o.usb_state; }
};
bool hotspot_address(u32 h) { return (h >> 8) == (172u << 16 | 20u << 8 | 10u); }  // 172.20.10.x
Addresses local_addresses() {
    Addresses r;
    // the list can be large (virtual adapters of VMware / Hyper-V / WSL...): grow until it fits
    ULONG sz = 64 * 1024;
    std::vector<u8> buf;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST;
    for (int tries = 0; tries < 4 && rc == ERROR_BUFFER_OVERFLOW; tries++) {
        buf.resize(sz);
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &sz);
    }
    if (rc != NO_ERROR) {
        LOGW("remote: GetAdaptersAddresses failed (%lu)", rc);
        r.lan = "127.0.0.1";
        return r;
    }
    auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    std::string fallback;
    for (auto* a = aa; a; a = a->Next) {
        std::string desc = narrow(a->Description ? a->Description : L"");
        const bool apple = desc.find("Apple Mobile Device Ethernet") != std::string::npos;
        if (apple) r.usb_state = std::max(r.usb_state, a->OperStatus == IfOperStatusUp ? 2 : 1);
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            char ip[64] = {};
            auto* sin = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof ip);
            u32 h = ntohl(sin->sin_addr.s_addr);
            const bool link_local = (h >> 16) == (169u << 8 | 254u);  // no address from DHCP (yet)
            if (apple || hotspot_address(h)) {
                // the iPhone's DHCP answers a moment after the adapter appears; until then it
                // only has a 169.254 address that the iPhone cannot reach
                if (!link_local && (r.usb.empty() || hotspot_address(h))) r.usb = ip;
                continue;
            }
            if (link_local) continue;
            if (a->FirstGatewayAddress && r.lan.empty()) r.lan = ip;
            if (fallback.empty()) fallback = ip;
        }
    }
    if (r.lan.empty()) r.lan = fallback.empty() ? "127.0.0.1" : fallback;
    if (!r.usb.empty()) r.usb_state = 3;
    return r;
}
int g_port = 0;
Addresses g_addrs;
std::string usb_url() { return g_addrs.usb.empty() ? std::string() : "http://" + g_addrs.usb + ":" + std::to_string(g_port); }
// The address that matters most comes first: the end of a long title is cut off ("...")
// when the window is narrow.
const char* usb_hint() {
    switch (g_addrs.usb_state) {
    case 1: return "  (USB: iPhone のインターネット共有がオフ)";
    case 2: return "  (USB: iPhone からのアドレス待ち)";
    default: return "";
    }
}
std::string idle_title() {
    if (!g_addrs.usb.empty()) return " - iPhone USB: " + usb_url() + "  (Wi-Fi: " + g_url + ")";
    return " - iPhone: " + g_url + usb_hint();
}
std::string connected_title() {
    if (g_usb_peer) return " - iPhone 接続中 (USB)";
    if (!g_addrs.usb.empty()) return " - iPhone 接続中 (Wi-Fi)  USB: " + usb_url();
    return " - iPhone 接続中 (Wi-Fi)";
}

// ---------------------------------------------------------------- PWA (home screen app)
// Opened from the home screen, Safari shows the page without its address bar
// (manifest "display", apple-mobile-web-app-capable). The icon is drawn here (a
// grass block in 16x16 pixel art) so that no game artwork is needed.
const char kManifest[] = R"({
  "name": "MCPE 0.16.2 Emulator",
  "short_name": "MCPE",
  "start_url": "/",
  "scope": "/",
  "display": "fullscreen",
  "orientation": "landscape",
  "background_color": "#000000",
  "theme_color": "#000000",
  "icons": [
    { "src": "/icon-192.png", "sizes": "192x192", "type": "image/png", "purpose": "any" },
    { "src": "/icon-512.png", "sizes": "512x512", "type": "image/png", "purpose": "any" }
  ]
}
)";

std::string make_icon_png(int size) {
    static const u32 grass[] = {0x5d9e2f, 0x6fb43a, 0x4f8a28, 0x67a834};
    static const u32 dirt[] = {0x8b5a2b, 0x7a4e25, 0x9b6a3a, 0x6b4422};
    u32 art[16][16];
    u32 seed = 12345;
    auto rnd = [&] { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 3; };
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int edge = 4 + ((x * 7 + 3) % 5 == 0 ? 2 : (x * 3) % 4 == 0 ? 1 : 0);  // grass hanging over the dirt
            art[y][x] = y < edge ? grass[rnd()] : dirt[rnd()];
        }
    std::vector<u8> px(size_t(size) * size * 4);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            u32 c = art[y * 16 / size][x * 16 / size];
            u8* d = &px[(size_t(y) * size + x) * 4];
            d[0] = (u8)c, d[1] = (u8)(c >> 8), d[2] = (u8)(c >> 16), d[3] = 255;  // BGRA
        }
    std::string out;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* f = nullptr;
    IStream* st = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
        SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &st)) && SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) &&
        SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(size, size))) {
        WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(fr->SetPixelFormat(&pf)) && pf == GUID_WICPixelFormat32bppBGRA &&
            SUCCEEDED(fr->WritePixels(size, size * 4, (UINT)px.size(), px.data())) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit())) {
            ULARGE_INTEGER pos{};
            LARGE_INTEGER zero{};
            HGLOBAL hg = nullptr;
            if (SUCCEEDED(st->Seek(zero, STREAM_SEEK_CUR, &pos)) && SUCCEEDED(GetHGlobalFromStream(st, &hg)))
                if (void* p = GlobalLock(hg)) {
                    out.assign((const char*)p, (size_t)pos.QuadPart);
                    GlobalUnlock(hg);
                }
        }
    }
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
    if (f) f->Release();
    return out;
}

std::wstring exe_dir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    return s.substr(0, s.find_last_of(L"\\/"));
}

// ---------------------------------------------------------------- input from the phone
void post(window::EventType t, int id, float x, float y, u32 chr = 0) { window::post_event({t, id, x, y, chr, 0, 0}); }

void release_touches() {
    for (int i = 0; i < 10; i++)
        if (g_touch_down[i]) {
            g_touch_down[i] = false;
            post(window::EvPointerUp, i, 0, 0);
        }
}

void post_text(const std::string& s) {  // UTF-8 -> one Char event per code point
    for (size_t i = 0; i < s.size();) {
        u8 c = (u8)s[i];
        u32 cp = c;
        int n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        if (n > 1) cp = c & (0x3f >> (n - 1));
        for (int k = 1; k < n && i + k < s.size(); k++) cp = cp << 6 | ((u8)s[i + k] & 0x3f);
        i += n;
        if (cp == '\n' || cp == '\r') cp = '\r';
        post(window::EvChar, 0, 0, 0, cp);
    }
}

void handle_message(const std::string& m) {
    char cmd[4] = {};
    int id = 0;
    float x = 0, y = 0;
    if (m == "a") {
        if (g_inflight > 0) g_inflight--;
        return;
    }
    if (m.rfind("t ", 0) == 0) { post_text(m.substr(2)); return; }
    if (m == "e") { post(window::EvChar, 0, 0, 0, '\r'); return; }
    if (m == "pc") { pick_finish(nullptr); return; }
    if (sscanf(m.c_str(), "b %d", &id) == 1) {
        for (int i = 0; i < std::min(id, 1000); i++) post(window::EvChar, 0, 0, 0, 8);
        return;
    }
    if (sscanf(m.c_str(), "s %f %f", &x, &y) == 2) {
        // landscape, long side limited to [remote] resolution, even sizes
        float lw = std::max(x, y), sh = std::min(x, y);
        if (lw < 64 || sh < 64) return;
        float k = std::min(1.0f, g_cfg.resolution / lw);
        int w = (int)(lw * k) & ~1, h = (int)(sh * k) & ~1;
        if (w != g_game_w || h != g_game_h) {
            g_game_w = w;
            g_game_h = h;
            LOGI("remote: phone screen %.0fx%.0f, rendering at %dx%d", x, y, w, h);
            window::set_game_size(w, h);
        }
        // The game is still in its mouse mode (PC); the first touch only switches it to touch
        // mode and is otherwise lost, so the phone's first real tap would do nothing. On a
        // menu a tap in the top-left corner hits nothing; in the world it is left out (a
        // touch there could act on the world).
        if (!g_mode_tap_done) {
            g_mode_tap_done = true;
            if (!patches::in_world()) {
                post(window::EvPointerDown, 9, 0, 0);
                post(window::EvPointerUp, 9, 0, 0);
            }
        }
        return;
    }
    if (sscanf(m.c_str(), "%3s %d %f %f", cmd, &id, &x, &y) >= 2 && id >= 0 && id < 10) {
        float px = std::clamp(x, 0.0f, 1.0f) * g_game_w, py = std::clamp(y, 0.0f, 1.0f) * g_game_h;
        switch (cmd[0]) {
        case 'd':
            g_touch_down[id] = true;
            post(window::EvPointerDown, id, px, py);
            break;
        case 'm':
            if (g_touch_down[id]) post(window::EvPointerMove, id, px, py);
            break;
        case 'u':
        case 'c':
            if (g_touch_down[id]) post(window::EvPointerUp, id, px, py);
            g_touch_down[id] = false;
            break;
        }
    }
}

// ---------------------------------------------------------------- encoder thread
std::mutex g_frame_lock;
std::condition_variable g_frame_cv;
std::vector<u8> g_frame;  // BGRA, bottom-up (as read from GL)
int g_frame_w = 0, g_frame_h = 0;
std::atomic<bool> g_frame_busy{false};  // g_frame is owned by the encoder until it clears this

bool encode_jpeg(IWICImagingFactory* f, const u8* bgr, int w, int h, int quality, std::vector<u8>& out) {
    IStream* st = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* fr = nullptr;
    IPropertyBag2* bag = nullptr;
    bool ok = false;
    if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &st)) && SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, &bag))) {
        PROPBAG2 opt[2] = {};
        opt[0].pstrName = (LPOLESTR)L"ImageQuality";
        opt[1].pstrName = (LPOLESTR)L"JpegYCrCbSubsampling";
        VARIANT v[2];
        VariantInit(&v[0]);
        VariantInit(&v[1]);
        v[0].vt = VT_R4;
        v[0].fltVal = quality / 100.0f;
        v[1].vt = VT_UI1;
        v[1].bVal = WICJpegYCrCbSubsampling420;
        bag->Write(2, opt, v);
        WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
        if (SUCCEEDED(fr->Initialize(bag)) && SUCCEEDED(fr->SetSize(w, h)) && SUCCEEDED(fr->SetPixelFormat(&pf)) &&
            pf == GUID_WICPixelFormat24bppBGR && SUCCEEDED(fr->WritePixels(h, w * 3, w * 3 * h, (BYTE*)bgr)) &&
            SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit())) {
            ULARGE_INTEGER pos{};
            LARGE_INTEGER zero{};
            HGLOBAL hg = nullptr;
            if (SUCCEEDED(st->Seek(zero, STREAM_SEEK_CUR, &pos)) && SUCCEEDED(GetHGlobalFromStream(st, &hg))) {
                if (void* p = GlobalLock(hg)) {
                    out.assign((u8*)p, (u8*)p + pos.QuadPart);
                    GlobalUnlock(hg);
                    ok = true;
                }
            }
        }
    }
    if (bag) bag->Release();
    if (fr) fr->Release();
    if (enc) enc->Release();
    if (st) st->Release();
    return ok;
}

void encoder_thread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) {
        LOGE("remote: WIC is not available, the screen cannot be sent");
        return;
    }
    std::vector<u8> bgr, jpeg;
    for (;;) {
        int w, h;
        {
            std::unique_lock lk(g_frame_lock);
            g_frame_cv.wait(lk, [] { return g_frame_busy.load(); });
            w = g_frame_w;
            h = g_frame_h;
            bgr.resize(size_t(w) * h * 3);
            for (int y = 0; y < h; y++) {  // flip to top-down, drop alpha
                const u8* s = g_frame.data() + size_t(h - 1 - y) * w * 4;
                u8* d = bgr.data() + size_t(y) * w * 3;
                for (int x = 0; x < w; x++, s += 4, d += 3) d[0] = s[0], d[1] = s[1], d[2] = s[2];
            }
        }
        g_frame_busy = false;
        if (encode_jpeg(f, bgr.data(), w, h, g_cfg.quality, jpeg) && g_connected) {
            jpeg.insert(jpeg.begin(), u8(1));
            g_inflight++;
            if (!ws_send(2, jpeg.data(), jpeg.size())) g_inflight--;
        }
    }
}

// ---------------------------------------------------------------- network thread
struct Pending {
    SOCKET s;
    std::string buf;
    double since;
    bool usb;
};

void close_client(const char* why) {
    {
        std::lock_guard lk(g_send_lock);
        if (g_client == INVALID_SOCKET) return;
        closesocket(g_client);
        g_client = INVALID_SOCKET;
    }
    g_connected = false;
    g_broken = false;
    g_inflight = 0;
    release_touches();
    g_game_w = g_game_h = 0;
    window::set_game_size(0, 0);
    window::set_title_suffix(idle_title().c_str());
    {
        std::lock_guard lk(g_pick_lock);
        if (g_pick_state == 1 && g_pick_on_phone) g_pick_state = 3;
    }
    LOGI("remote: phone disconnected (%s)", why);
}

std::string header(const std::string& req, const char* name) {
    std::string lreq = req, lname = std::string("\r\n") + name + ":";
    for (auto& c : lreq) c = (char)tolower((unsigned char)c);
    for (auto& c : lname) c = (char)tolower((unsigned char)c);
    size_t p = lreq.find(lname);
    if (p == std::string::npos) return {};
    p += lname.size();
    size_t e = req.find("\r\n", p);
    std::string v = req.substr(p, e - p);
    size_t a = v.find_first_not_of(" \t"), b = v.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
}

// returns true when the socket became the WebSocket client
bool handle_http(SOCKET s, const std::string& req, bool usb) {
    std::string path;
    {
        size_t a = req.find(' '), b = req.find(' ', a + 1);
        if (a != std::string::npos && b != std::string::npos) path = req.substr(a + 1, b - a - 1);
    }
    std::string key = header(req, "Sec-WebSocket-Key");
    if (path == "/ws" && !key.empty()) {
        if (g_connected) ws_text("replaced");
        close_client("another phone connected");
        std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                           ws_accept(key) + "\r\n\r\n";
        if (!send_all(s, resp.data(), resp.size())) return false;
        DWORD tmo = 3000;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tmo, sizeof tmo);
        BOOL nodelay = TRUE;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof nodelay);
        {
            std::lock_guard lk(g_send_lock);
            g_client = s;
        }
        g_inflight = 0;
        g_broken = false;
        g_mode_tap_done = false;
        g_connected = true;
        g_usb_peer = usb;
        ws_text(g_text_input ? "kb 1" : "kb 0");
        window::set_title_suffix(connected_title().c_str());
        LOGI("remote: phone connected (%s)", usb ? "USB" : "network");
        return true;
    }
    std::string body, type = "text/html; charset=utf-8", status = "200 OK";
    if (path == "/" || path.rfind("/?", 0) == 0 || path == "/index.html") {
        FILE* f = _wfopen((exe_dir() + L"\\remote.html").c_str(), L"rb");
        if (f) {
            char b[8192];
            size_t n;
            while ((n = fread(b, 1, sizeof b, f)) > 0) body.append(b, n);
            fclose(f);
        } else {
            status = "500 Internal Server Error";
            body = "remote.html was not found next to the emulator";
            type = "text/plain; charset=utf-8";
        }
    } else if (path == "/manifest.webmanifest") {
        body = kManifest;
        type = "application/manifest+json";
    } else if (path == "/icon-180.png" || path == "/icon-192.png" || path == "/icon-512.png" || path == "/apple-touch-icon.png" ||
               path == "/favicon.ico") {
        int size = path == "/icon-512.png" ? 512 : path == "/icon-192.png" ? 192 : path == "/favicon.ico" ? 32 : 180;
        body = make_icon_png(size);
        type = "image/png";
    } else {
        status = "404 Not Found";
        body = "not found";
        type = "text/plain";
    }
    std::string resp = "HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) +
                       "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" + body;
    send_all(s, resp.data(), resp.size());
    return false;
}

// parses complete client frames from buf; false = close the connection
bool read_frames(std::string& buf) {
    for (;;) {
        if (buf.size() < 2) return true;
        const u8* p = (const u8*)buf.data();
        u8 op = p[0] & 15;
        bool masked = p[1] & 0x80;
        u64 len = p[1] & 127;
        size_t hn = 2;
        if (len == 126) {
            if (buf.size() < 4) return true;
            len = u64(p[2]) << 8 | p[3];
            hn = 4;
        } else if (len == 127) {
            if (buf.size() < 10) return true;
            len = 0;
            for (int i = 0; i < 8; i++) len = len << 8 | p[2 + i];
            hn = 10;
        }
        if (len > (256u << 20)) return false;  // the largest message: a picked MMD model
        size_t total = hn + (masked ? 4 : 0) + (size_t)len;
        if (buf.size() < total) return true;
        std::string payload = buf.substr(hn + (masked ? 4 : 0), (size_t)len);
        if (masked)
            for (size_t i = 0; i < payload.size(); i++) payload[i] ^= p[hn + (i & 3)];
        buf.erase(0, total);
        if (op == 8) return false;                  // close
        if (op == 9) ws_send(10, payload.data(), payload.size());  // ping -> pong
        else if (op == 1) handle_message(payload);
        else if (op == 2) {  // the picked image
            std::vector<u8> d(payload.begin(), payload.end());
            pick_finish(&d);
        }
    }
}

// Windows tells us when an adapter goes up / down or gets an address (the iPhone's USB
// network appears and disappears with Personal Hotspot), so the title follows at once.
std::atomic<bool> g_net_changed{true};
void CALLBACK on_interface_change(PVOID, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) { g_net_changed = true; }
void CALLBACK on_address_change(PVOID, PMIB_UNICASTIPADDRESS_ROW, MIB_NOTIFICATION_TYPE) { g_net_changed = true; }

void network_thread() {
    HANDLE h1 = nullptr, h2 = nullptr;
    NotifyIpInterfaceChange(AF_INET, on_interface_change, nullptr, FALSE, &h1);
    NotifyUnicastIpAddressChange(AF_INET, on_address_change, nullptr, FALSE, &h2);
    std::vector<Pending> pending;
    std::string ws_buf;
    double next_addr_check = 0;
    for (;;) {
        if (g_broken) close_client("send failed");
        if (g_net_changed.exchange(false) || window::now() >= next_addr_check) {  // also polled, in case a notification is missed
            next_addr_check = window::now() + 5;
            Addresses a = local_addresses();
            if (a != g_addrs) {
                if (a.usb_state != g_addrs.usb_state) {
                    static const char* const kState[] = {"no iPhone USB network adapter", "iPhone USB adapter disconnected (Personal Hotspot off?)",
                                                         "iPhone USB adapter connected, waiting for an address", "iPhone USB connection ready"};
                    LOGI("remote: %s%s%s", kState[a.usb_state], a.usb.empty() ? "" : ": http://", a.usb.empty() ? "" : (a.usb + ":" + std::to_string(g_port)).c_str());
                }
                g_addrs = a;
                g_url = "http://" + a.lan + ":" + std::to_string(g_port);
                window::set_title_suffix(g_connected ? connected_title().c_str() : idle_title().c_str());
            }
        }
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(g_listen, &rd);
        for (auto& p : pending) FD_SET(p.s, &rd);
        SOCKET client = g_client;
        if (client != INVALID_SOCKET) FD_SET(client, &rd);
        timeval tv{0, 200000};
        if (select(0, &rd, nullptr, nullptr, &tv) == SOCKET_ERROR) {
            Sleep(50);
            continue;
        }
        double now = window::now();
        if (FD_ISSET(g_listen, &rd)) {
            sockaddr_in from{};
            int fl = sizeof from;
            SOCKET s = accept(g_listen, (sockaddr*)&from, &fl);
            if (s != INVALID_SOCKET) {
                if (from.sin_family == AF_INET && private_address(ntohl(from.sin_addr.s_addr))) {
                    pending.push_back({s, {}, now, hotspot_address(ntohl(from.sin_addr.s_addr))});
                } else {
                    LOGW("remote: refused a connection from outside the local network");
                    closesocket(s);
                }
            }
        }
        for (size_t i = 0; i < pending.size();) {
            Pending& p = pending[i];
            bool done = now - p.since > 10;
            if (FD_ISSET(p.s, &rd)) {
                char b[4096];
                int n = recv(p.s, b, sizeof b, 0);
                if (n <= 0) done = true;
                else p.buf.append(b, n);
                if (p.buf.size() > 16384) done = true;
                if (p.buf.find("\r\n\r\n") != std::string::npos) {
                    if (handle_http(p.s, p.buf, p.usb)) {
                        ws_buf = p.buf.substr(p.buf.find("\r\n\r\n") + 4);
                        pending.erase(pending.begin() + i);
                        continue;
                    }
                    done = true;
                }
            }
            if (done) {
                shutdown(p.s, SD_SEND);
                closesocket(p.s);
                pending.erase(pending.begin() + i);
                continue;
            }
            i++;
        }
        if (client != INVALID_SOCKET && client == g_client && FD_ISSET(client, &rd)) {
            char b[8192];
            int n = recv(client, b, sizeof b, 0);
            if (n <= 0) close_client("connection closed");
            else {
                ws_buf.append(b, n);
                if (!read_frames(ws_buf)) close_client("closed by the phone");
            }
        }
    }
}

// ---------------------------------------------------------------- GL readback (main thread)
typedef void(APIENTRY* PFNGENBUF)(GLsizei, GLuint*);
typedef void(APIENTRY* PFNBINDBUF)(GLenum, GLuint);
typedef void(APIENTRY* PFNBUFDATA)(GLenum, ptrdiff_t, const void*, GLenum);
typedef void*(APIENTRY* PFNMAPRANGE)(GLenum, ptrdiff_t, ptrdiff_t, GLbitfield);
typedef GLboolean(APIENTRY* PFNUNMAP)(GLenum);
PFNGENBUF pglGenBuffers;
PFNBINDBUF pglBindBuffer;
PFNBUFDATA pglBufferData;
PFNMAPRANGE pglMapBufferRange;
PFNUNMAP pglUnmapBuffer;
constexpr GLenum kPixelPack = 0x88EB, kPixelPackBinding = 0x88ED, kStreamRead = 0x88E1, kMapRead = 0x0001, kBGRA = 0x80E1;

GLuint g_pbo[2];
size_t g_pbo_size[2];
int g_pbo_w[2], g_pbo_h[2];
int g_pbo_next = 0, g_pbo_pending = -1;
double g_last_capture = 0;

}  // namespace

void capture(int w, int h) {
    if (!g_connected || w <= 0 || h <= 0) {
        g_pbo_pending = -1;
        return;
    }
    if (!pglGenBuffers) {
        pglGenBuffers = (PFNGENBUF)window::gl_proc("glGenBuffers");
        pglBindBuffer = (PFNBINDBUF)window::gl_proc("glBindBuffer");
        pglBufferData = (PFNBUFDATA)window::gl_proc("glBufferData");
        pglMapBufferRange = (PFNMAPRANGE)window::gl_proc("glMapBufferRange");
        pglUnmapBuffer = (PFNUNMAP)window::gl_proc("glUnmapBuffer");
        if (!pglGenBuffers || !pglBindBuffer || !pglBufferData || !pglMapBufferRange || !pglUnmapBuffer) {
            LOGE("remote: pixel buffer objects are not available");
            g_connected = false;
            return;
        }
        pglGenBuffers(2, g_pbo);
    }
    GLint prev_pack = 0;
    glGetIntegerv(kPixelPackBinding, &prev_pack);
    // the frame read in the previous call: hand it to the encoder
    if (g_pbo_pending >= 0 && !g_frame_busy) {
        int i = g_pbo_pending;
        g_pbo_pending = -1;
        pglBindBuffer(kPixelPack, g_pbo[i]);
        size_t n = size_t(g_pbo_w[i]) * g_pbo_h[i] * 4;
        if (const void* p = pglMapBufferRange(kPixelPack, 0, (ptrdiff_t)n, kMapRead)) {
            {
                std::lock_guard lk(g_frame_lock);
                g_frame.assign((const u8*)p, (const u8*)p + n);
                g_frame_w = g_pbo_w[i];
                g_frame_h = g_pbo_h[i];
                g_frame_busy = true;
            }
            g_frame_cv.notify_one();
        }
        pglUnmapBuffer(kPixelPack);
    }
    double now = window::now();
    if (g_pbo_pending < 0 && !g_frame_busy && g_inflight < 2 && now - g_last_capture >= 1.0 / g_cfg.fps - 0.002) {
        g_last_capture = now;
        int i = g_pbo_next;
        g_pbo_next ^= 1;
        size_t n = size_t(w) * h * 4;
        pglBindBuffer(kPixelPack, g_pbo[i]);
        if (g_pbo_size[i] != n) {
            pglBufferData(kPixelPack, (ptrdiff_t)n, nullptr, kStreamRead);
            g_pbo_size[i] = n;
        }
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, w, h, kBGRA, GL_UNSIGNED_BYTE, nullptr);
        g_pbo_w[i] = w;
        g_pbo_h[i] = h;
        g_pbo_pending = i;
    }
    pglBindBuffer(kPixelPack, (GLuint)prev_pack);
}

void start() {
    g_cfg = controls::remote();
    if (!g_cfg.enabled) {
        LOGI("remote: disabled in controls.ini");
        return;
    }
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int port = 0;
    for (int p = g_cfg.port; p < g_cfg.port + 10 && p <= 65535; p++) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons((u16)p);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(g_listen, (sockaddr*)&a, sizeof a) == 0) {
            port = p;
            break;
        }
    }
    if (!port || listen(g_listen, 8) != 0) {
        LOGW("remote: could not listen on port %d", g_cfg.port);
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return;
    }
    g_port = port;
    g_addrs = local_addresses();
    g_url = "http://" + g_addrs.lan + ":" + std::to_string(port);
    LOGI("remote: open %s in the phone's browser (Safari)", g_url.c_str());
    if (!g_addrs.usb.empty()) LOGI("remote: iPhone USB connection: http://%s:%d", g_addrs.usb.c_str(), port);
    else if (g_addrs.usb_state == 1) LOGI("remote: the iPhone's USB network adapter is disconnected (turn on Personal Hotspot to use USB)");
    window::set_title_suffix(idle_title().c_str());
    std::thread(network_thread).detach();
    std::thread(encoder_thread).detach();
}

bool connected() { return g_connected; }

void set_text_input(bool on) {
    if (g_text_input.exchange(on) == on) return;
    if (g_connected) ws_text(on ? "kb 1" : "kb 0");
}

std::string url() { return g_url; }

// ---------------------------------------------------------------- audio
namespace {
std::mutex g_audio_lock;
std::condition_variable g_audio_cv;
std::vector<s16> g_audio;  // interleaved stereo waiting to be sent
int g_audio_rate = 48000;
bool g_audio_thread = false;

void audio_thread() {
    std::vector<u8> msg;
    for (;;) {
        {
            std::unique_lock lk(g_audio_lock);
            g_audio_cv.wait(lk, [] { return g_audio.size() >= 2048; });  // ~21 ms at 48 kHz
            int rate = g_audio_rate;
            msg.resize(5 + g_audio.size() * 2);
            msg[0] = 2;
            memcpy(msg.data() + 1, &rate, 4);
            memcpy(msg.data() + 5, g_audio.data(), g_audio.size() * 2);
            g_audio.clear();
        }
        if (g_connected) ws_send(2, msg.data(), msg.size());
    }
}
}  // namespace

bool audio(const s16* samples, int frames, int rate) {
    if (!g_connected) return false;
    {
        std::lock_guard lk(g_audio_lock);
        if (!g_audio_thread) {
            g_audio_thread = true;
            std::thread(audio_thread).detach();
        }
        g_audio_rate = rate;
        g_audio.insert(g_audio.end(), samples, samples + size_t(frames) * 2);
        size_t max = size_t(rate);  // the network stalled: keep at most 0.5 s (stereo)
        if (g_audio.size() > max) g_audio.erase(g_audio.begin(), g_audio.end() - max);
    }
    g_audio_cv.notify_one();
    return !g_cfg.pc_audio;
}

// ---------------------------------------------------------------- image picker
namespace {
std::atomic<DWORD> g_dialog_thread{0};  // thread showing the PC file dialog (0 = none)

constexpr size_t kModelLimit = 256u << 20;  // a model with its textures (also the phone's message limit)

bool read_whole(const std::wstring& path, std::vector<u8>& out, size_t limit) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    u8 b[65536];
    size_t n;
    out.clear();
    while ((n = fread(b, 1, sizeof b, f)) > 0 && out.size() <= limit) out.insert(out.end(), b, b + n);
    fclose(f);
    return out.size() <= limit;
}

u32 crc32(const u8* p, size_t n) {
    static u32 table[256];
    if (!table[1])
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    u32 c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// The picked model and the other files of its folder (and sub folders: textures are often in
// "tex\"), as a ZIP archive without compression. Names are UTF-8, relative to the folder.
std::vector<u8> zip_model_folder(const std::wstring& model) {
    size_t slash = model.find_last_of(L"\\/");
    std::wstring dir = slash == std::wstring::npos ? L"." : model.substr(0, slash);
    std::vector<std::wstring> files{model.substr(slash + 1)};
    std::vector<std::wstring> pending{L""};
    while (!pending.empty() && files.size() < 4000) {
        std::wstring rel = pending.back();
        pending.pop_back();
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\" + rel + L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (std::count(rel.begin(), rel.end(), L'\\') < 4) pending.push_back(rel + name + L"\\");
            } else if (rel + name != files[0]) {
                files.push_back(rel + name);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::vector<u8> zip, central;
    auto put16 = [](std::vector<u8>& v, u32 x) { v.push_back(u8(x)); v.push_back(u8(x >> 8)); };
    auto put32 = [&](std::vector<u8>& v, u32 x) { put16(v, x & 0xFFFF); put16(v, x >> 16); };
    int count = 0;
    for (const std::wstring& rel : files) {
        std::vector<u8> data;
        if (!read_whole(dir + L"\\" + rel, data, 64u << 20)) continue;  // skips huge files
        if (zip.size() + data.size() > kModelLimit) break;
        std::string name = narrow(rel);
        std::replace(name.begin(), name.end(), '\\', '/');
        u32 crc = crc32(data.data(), data.size()), off = (u32)zip.size();
        // local header: version 20, flags 0x800 (UTF-8 name), stored
        put32(zip, 0x04034b50); put16(zip, 20); put16(zip, 0x800); put16(zip, 0); put32(zip, 0);
        put32(zip, crc); put32(zip, (u32)data.size()); put32(zip, (u32)data.size());
        put16(zip, (u32)name.size()); put16(zip, 0);
        zip.insert(zip.end(), name.begin(), name.end());
        zip.insert(zip.end(), data.begin(), data.end());
        put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0x800); put16(central, 0);
        put32(central, 0); put32(central, crc); put32(central, (u32)data.size()); put32(central, (u32)data.size());
        put16(central, (u32)name.size()); put16(central, 0); put16(central, 0); put16(central, 0); put16(central, 0);
        put32(central, 0); put32(central, off);
        central.insert(central.end(), name.begin(), name.end());
        count++;
    }
    u32 cdOff = (u32)zip.size();
    zip.insert(zip.end(), central.begin(), central.end());
    put32(zip, 0x06054b50); put16(zip, 0); put16(zip, 0); put16(zip, (u32)count); put16(zip, (u32)count);
    put32(zip, (u32)central.size()); put32(zip, cdOff); put16(zip, 0);
    LOGI("remote: packed %d file(s) of %s (%zu bytes)", count, narrow(dir).c_str(), zip.size());
    return count ? zip : std::vector<u8>();
}

void pc_file_dialog(int kind) {
    g_dialog_thread = GetCurrentThreadId();
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{sizeof ofn};
    ofn.hwndOwner = (HWND)window::native_handle();
    if (kind == kPickModel) {
        ofn.lpstrFilter = L"MMD モデル (*.pmx;*.pmd;*.zip)\0*.pmx;*.pmd;*.zip\0すべてのファイル (*.*)\0*.*\0";
        ofn.lpstrTitle = L"MMD モデルを選択 (.pmx / .pmd、またはモデルのフォルダを圧縮した .zip)";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        std::vector<u8> data;
        if (GetOpenFileNameW(&ofn)) {
            std::wstring path = file, ext = path.size() > 4 ? path.substr(path.size() - 4) : L"";
            for (auto& c : ext) c = (wchar_t)towlower(c);
            if (ext == L".pmx" || ext == L".pmd") {
                data = zip_model_folder(path);
            } else if (!read_whole(path, data, kModelLimit)) {
                LOGW("remote: %s is larger than %zu MB, not used", narrow(path).c_str(), kModelLimit >> 20);
                data.clear();
            }
            LOGI("remote: picked %s (%zu bytes)", narrow(path).c_str(), data.size());
        }
        g_dialog_thread = 0;
        pick_finish(&data);
        return;
    }
    if (kind == kPickSound) {
        ofn.lpstrFilter = L"音声 (*.mp3;*.ogg)\0*.mp3;*.ogg\0すべてのファイル (*.*)\0*.*\0";
        ofn.lpstrTitle = L"音声ファイルを選択 (.mp3 / .ogg)";
    } else if (kind == kPickMedia) {
        ofn.lpstrFilter = L"画像・GIF (*.png;*.jpg;*.jpeg;*.gif;*.bmp)\0*.png;*.jpg;*.jpeg;*.gif;*.bmp\0すべてのファイル (*.*)\0*.*\0";
        ofn.lpstrTitle = L"画像・GIF を選択";
    } else {
        ofn.lpstrFilter = L"スキン画像 (*.png)\0*.png\0画像 (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0";
        ofn.lpstrTitle = L"スキンの画像を選択 (64x64 または 64x32 の PNG)";
    }
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    const size_t limit = kind == kPickMedia ? (32u << 20) : (16u << 20);
    std::vector<u8> data;
    if (GetOpenFileNameW(&ofn)) {
        if (FILE* f = _wfopen(file, L"rb")) {
            u8 b[65536];
            size_t n;
            while ((n = fread(b, 1, sizeof b, f)) > 0 && data.size() <= limit) data.insert(data.end(), b, b + n);
            fclose(f);
        }
        if (data.size() > limit) {
            LOGW("remote: %s is larger than %zu MB, not used", narrow(file).c_str(), limit >> 20);
            data.clear();
        } else {
            LOGI("remote: picked %s (%zu bytes)", narrow(file).c_str(), data.size());
        }
    }
    g_dialog_thread = 0;
    pick_finish(&data);
}

BOOL CALLBACK close_dialog(HWND w, LPARAM) {
    wchar_t cls[16] = {};
    GetClassNameW(w, cls, 16);
    if (!wcscmp(cls, L"#32770")) PostMessageW(w, WM_COMMAND, IDCANCEL, 0);
    return TRUE;
}
}  // namespace

void pick_image_cancel() {
    bool phone;
    {
        std::lock_guard lk(g_pick_lock);
        if (g_pick_state != 1) return;
        g_pick_state = 0;  // a late answer is ignored (pick_finish only takes pending requests)
        phone = g_pick_on_phone;
    }
    LOGI("remote: image picker cancelled by the game");
    if (phone) {
        ws_text("pick cancel");
    } else if (DWORD t = g_dialog_thread) {
        EnumThreadWindows(t, close_dialog, 0);
    }
}

void pick_image_begin(int kind) {
    {
        std::lock_guard lk(g_pick_lock);
        if (g_pick_state == 1) return;
        g_pick_state = 1;
        g_pick_kind = kind;
        g_pick_data.clear();
        g_pick_on_phone = g_connected;
    }
    if (g_pick_on_phone) {
        LOGI("remote: asking the phone for an image");
        ws_text(kind == kPickModel ? "pick model" : kind == kPickSound ? "pick sound" : kind == kPickMedia ? "pick media" : "pick");
    } else {
        std::thread(pc_file_dialog, kind).detach();
    }
}

int pick_image_poll(size_t* len) {
    std::lock_guard lk(g_pick_lock);
    if (g_pick_state == 1) return 0;
    if (g_pick_state == 2) {
        *len = g_pick_data.size();
        return 1;
    }
    g_pick_state = 0;
    return -1;  // cancelled (or nothing was asked for)
}

void pick_image_take(void* out) {
    std::lock_guard lk(g_pick_lock);
    if (g_pick_state != 2) return;
    memcpy(out, g_pick_data.data(), g_pick_data.size());
    g_pick_data.clear();
    g_pick_state = 0;
}

}  // namespace remote
