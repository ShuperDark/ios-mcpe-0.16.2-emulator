#include "vfs.h"

#include <filesystem>
#include <mutex>

namespace vfs {
namespace {
std::string g_bundle, g_data;
std::mutex g_lock;
std::string g_cwd = "/";

struct Mount {
    std::string guest;  // no trailing slash
    std::wstring host;
};
std::vector<Mount> g_mounts;

std::string normalize(const std::string& p) {
    // resolve "." and ".." and duplicate slashes
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        std::string s = p.substr(i, j - i);
        if (s.empty() || s == ".") {
        } else if (s == "..") {
            if (!parts.empty()) parts.pop_back();
        } else
            parts.push_back(s);
        i = j + 1;
    }
    std::string r;
    for (auto& s : parts) r += "/" + s;
    return r.empty() ? "/" : r;
}
}  // namespace

void init(const std::string& host_bundle_dir, const std::string& host_data_dir) {
    g_bundle = host_bundle_dir;
    g_data = host_data_dir;
    std::error_code ec;
    for (const char* d : {"Documents", "Library", "Library/Caches", "Library/Preferences", "tmp", "root"})
        std::filesystem::create_directories(std::filesystem::path(widen(g_data)) / d, ec);
    g_mounts.clear();
    g_mounts.push_back({kBundlePath, widen(g_bundle)});
    g_mounts.push_back({kHomePath, widen(g_data)});
    g_mounts.push_back({"/tmp", widen(g_data + "/tmp")});
    g_mounts.push_back({"/private/var/mobile/Containers/Data/Application/5E6F7A8B-0000-4000-8000-000000016200", widen(g_data)});
    g_cwd = "/";
}

const std::string& host_bundle_dir() { return g_bundle; }
const std::string& host_data_dir() { return g_data; }

std::string cwd() {
    std::lock_guard lk(g_lock);
    return g_cwd;
}
bool set_cwd(const char* p) {
    std::lock_guard lk(g_lock);
    std::string full = (p && p[0] == '/') ? std::string(p) : g_cwd + "/" + (p ? p : "");
    g_cwd = normalize(full);
    return true;
}

std::wstring to_host(const char* gp) {
    if (!gp) return L"";
    std::string full;
    if (gp[0] == '/') full = gp;
    else {
        std::lock_guard lk(g_lock);
        full = g_cwd + "/" + gp;
    }
    full = normalize(full);
    for (auto& m : g_mounts) {
        if (full == m.guest) return m.host;
        if (full.size() > m.guest.size() && full.compare(0, m.guest.size(), m.guest) == 0 && full[m.guest.size()] == '/') {
            std::wstring rest = widen(full.substr(m.guest.size()));
            for (auto& c : rest) if (c == L'/') c = L'\\';
            return m.host + rest;
        }
    }
    // everything else lives in a private sandbox root
    std::wstring rest = widen(full);
    for (auto& c : rest) if (c == L'/') c = L'\\';
    return widen(g_data + "/root") + rest;
}

std::string to_guest(const std::wstring& hp) {
    std::wstring h = hp;
    for (auto& c : h) if (c == L'\\') c = L'/';
    for (auto& m : g_mounts) {
        std::wstring mh = m.host;
        for (auto& c : mh) if (c == L'\\') c = L'/';
        if (h.size() >= mh.size() && _wcsnicmp(h.c_str(), mh.c_str(), mh.size()) == 0 &&
            (h.size() == mh.size() || h[mh.size()] == L'/'))
            return m.guest + narrow(h.substr(mh.size()));
    }
    std::wstring root = widen(g_data + "/root");
    for (auto& c : root) if (c == L'\\') c = L'/';
    if (h.size() >= root.size() && _wcsnicmp(h.c_str(), root.c_str(), root.size()) == 0) {
        std::string r = narrow(h.substr(root.size()));
        return r.empty() ? "/" : r;
    }
    return narrow(h);
}

bool is_readonly(const char* gp) {
    if (!gp) return false;
    std::string full;
    if (gp[0] == '/') full = gp;
    else {
        std::lock_guard lk(g_lock);
        full = g_cwd + "/" + gp;
    }
    full = normalize(full);
    std::string b = kBundlePath;
    return full == b || (full.size() > b.size() && full.compare(0, b.size(), b) == 0 && full[b.size()] == '/');
}

bool is_dev_random(const char* p) { return p && (!strcmp(p, "/dev/urandom") || !strcmp(p, "/dev/random")); }
bool is_dev_null(const char* p) { return p && !strcmp(p, "/dev/null"); }

}  // namespace vfs
