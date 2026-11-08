// libSystem: BSD sockets on top of Winsock (Darwin structure layouts and
// constants are converted at the boundary).
#include "fdtab.h"
#include "hle_common.h"
#include "vmem.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>

#include <algorithm>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

using namespace hlec;

namespace {
constexpr int D_AF_INET = 2, D_AF_INET6 = 30, D_AF_UNSPEC = 0;

void wsa_init() {
    static std::once_flag f;
    std::call_once(f, [] {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    });
}

int map_wsa(int e) {
    switch (e) {
    case WSAEWOULDBLOCK: return D_EAGAIN;
    case WSAEINPROGRESS: return D_EINPROGRESS;
    case WSAEALREADY: return D_EALREADY;
    case WSAENOTSOCK: return D_ENOTSOCK;
    case WSAEDESTADDRREQ: return D_EDESTADDRREQ;
    case WSAEMSGSIZE: return D_EMSGSIZE;
    case WSAEPROTOTYPE: return D_EPROTOTYPE;
    case WSAENOPROTOOPT: return D_ENOPROTOOPT;
    case WSAEPROTONOSUPPORT: return D_EPROTONOSUPPORT;
    case WSAEOPNOTSUPP: return D_EOPNOTSUPP;
    case WSAEAFNOSUPPORT: return D_EAFNOSUPPORT;
    case WSAEADDRINUSE: return D_EADDRINUSE;
    case WSAEADDRNOTAVAIL: return D_EADDRNOTAVAIL;
    case WSAENETDOWN: return D_ENETDOWN;
    case WSAENETUNREACH: return D_ENETUNREACH;
    case WSAECONNABORTED: return D_ECONNABORTED;
    case WSAECONNRESET: return D_ECONNRESET;
    case WSAENOBUFS: return D_ENOBUFS;
    case WSAEISCONN: return D_EISCONN;
    case WSAENOTCONN: return D_ENOTCONN;
    case WSAETIMEDOUT: return D_ETIMEDOUT;
    case WSAECONNREFUSED: return D_ECONNREFUSED;
    case WSAEHOSTUNREACH: return D_EHOSTUNREACH;
    case WSAEINVAL: return D_EINVAL;
    case WSAEACCES: return D_EACCES;
    case WSAEBADF: return D_EBADF;
    case WSAEINTR: return D_EINTR;
    case WSAEFAULT: return D_EFAULT;
    case WSAEMFILE: return D_EMFILE;
    default: return D_EIO;
    }
}
void set_wsa_errno() { set_errno(map_wsa(WSAGetLastError())); }

// Darwin sockaddr (with sa_len) -> Windows sockaddr
int to_host_sa(const u8* d, u32 dlen, sockaddr_storage* out) {
    memset(out, 0, sizeof *out);
    if (!d || dlen < 2) return 0;
    int fam = d[1];
    if (fam == D_AF_INET) {
        auto* s = reinterpret_cast<sockaddr_in*>(out);
        s->sin_family = AF_INET;
        memcpy(&s->sin_port, d + 2, 2);
        memcpy(&s->sin_addr, d + 4, 4);
        return sizeof(sockaddr_in);
    }
    if (fam == D_AF_INET6) {
        auto* s = reinterpret_cast<sockaddr_in6*>(out);
        s->sin6_family = AF_INET6;
        memcpy(&s->sin6_port, d + 2, 2);
        memcpy(&s->sin6_flowinfo, d + 4, 4);
        memcpy(&s->sin6_addr, d + 8, 16);
        if (dlen >= 28) memcpy(&s->sin6_scope_id, d + 24, 4);
        return sizeof(sockaddr_in6);
    }
    return 0;
}
// Windows sockaddr -> Darwin; returns Darwin length
u32 to_guest_sa(const sockaddr* s, u8* d, u32 cap) {
    u8 tmp[28] = {0};
    u32 n = 0;
    if (s->sa_family == AF_INET) {
        auto* in = reinterpret_cast<const sockaddr_in*>(s);
        n = 16;
        tmp[0] = 16;
        tmp[1] = D_AF_INET;
        memcpy(tmp + 2, &in->sin_port, 2);
        memcpy(tmp + 4, &in->sin_addr, 4);
    } else if (s->sa_family == AF_INET6) {
        auto* in = reinterpret_cast<const sockaddr_in6*>(s);
        n = 28;
        tmp[0] = 28;
        tmp[1] = D_AF_INET6;
        memcpy(tmp + 2, &in->sin6_port, 2);
        memcpy(tmp + 4, &in->sin6_flowinfo, 4);
        memcpy(tmp + 8, &in->sin6_addr, 16);
        memcpy(tmp + 24, &in->sin6_scope_id, 4);
    }
    if (d && cap) memcpy(d, tmp, std::min(n, cap));
    return n;
}
int to_host_af(int f) { return f == D_AF_INET6 ? AF_INET6 : f == D_AF_INET ? AF_INET : f == D_AF_UNSPEC ? AF_UNSPEC : f; }
int to_guest_af(int f) { return f == AF_INET6 ? D_AF_INET6 : f; }

SOCKET sock_of(int fd) {
    auto f = fdtab::get(fd);
    if (!f || f->kind != fdtab::Kind::Socket) { set_errno(f ? D_ENOTSOCK : D_EBADF); return INVALID_SOCKET; }
    return (SOCKET)f->sock;
}
int map_msg_flags(int f) {
    int r = 0;
    if (f & 1) r |= MSG_OOB;
    if (f & 2) r |= MSG_PEEK;
    if (f & 0x40) r |= MSG_WAITALL;
    return r;
}
// Socket level options that differ (timeouts are timeval on Darwin, DWORD ms on Windows)
}  // namespace

HLE_FUNC(socket, int, (int dom, int type, int proto)) {
    LOGD("net: socket(%d, %d, %d)", dom, type, proto);
    wsa_init();
    if (dom != D_AF_INET && dom != D_AF_INET6) { set_errno(D_EAFNOSUPPORT); return -1; }
    SOCKET s = ::socket(to_host_af(dom), type, proto);
    if (s == INVALID_SOCKET) { set_wsa_errno(); return -1; }
    auto f = std::make_shared<fdtab::Fd>();
    f->kind = fdtab::Kind::Socket;
    f->sock = (u64)s;
    return fdtab::add(f);
}
HLE_FUNC(socketpair, int, (int, int, int, int*)) { set_errno(D_EOPNOTSUPP); return -1; }
HLE_FUNC(bind, int, (int fd, const u8* addr, u32 len)) {
    LOGD("net: %s", "bind");
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    sockaddr_storage ss;
    int l = to_host_sa(addr, len, &ss);
    if (::bind(s, (sockaddr*)&ss, l) != 0) { set_wsa_errno(); return -1; }
    return 0;
}
HLE_FUNC(connect, int, (int fd, const u8* addr, u32 len)) {
    LOGD("net: connect(fd %d, family %d, len %u)", fd, addr ? addr[1] : -1, len);
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    sockaddr_storage ss;
    int l = to_host_sa(addr, len, &ss);
    if (::connect(s, (sockaddr*)&ss, l) != 0) {
        int e = WSAGetLastError();
        set_errno(e == WSAEWOULDBLOCK ? D_EINPROGRESS : map_wsa(e));
        return -1;
    }
    return 0;
}
HLE_FUNC(listen, int, (int fd, int backlog)) {
    LOGD("net: %s", "listen");
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    if (::listen(s, backlog) != 0) { set_wsa_errno(); return -1; }
    return 0;
}
HLE_FUNC(accept, int, (int fd, u8* addr, u32* len)) {
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    sockaddr_storage ss;
    int l = sizeof ss;
    SOCKET n = ::accept(s, (sockaddr*)&ss, &l);
    if (n == INVALID_SOCKET) { set_wsa_errno(); return -1; }
    if (addr && len) *len = to_guest_sa((sockaddr*)&ss, addr, *len);
    auto f = std::make_shared<fdtab::Fd>();
    f->kind = fdtab::Kind::Socket;
    f->sock = (u64)n;
    return fdtab::add(f);
}
HLE_FUNC(shutdown, int, (int fd, int how)) {
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    return ::shutdown(s, how) == 0 ? 0 : (set_wsa_errno(), -1);
}
HLE_FUNC(send, s64, (int fd, const void* b, size_t n, int flags)) {
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    int r = ::send(s, (const char*)b, (int)n, map_msg_flags(flags));
    if (r < 0) { set_wsa_errno(); return -1; }
    return r;
}
HLE_FUNC(sendto, s64, (int fd, const void* b, size_t n, int flags, const u8* addr, u32 alen)) {
    LOGD("net: %s", "sendto");
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    sockaddr_storage ss;
    int l = addr ? to_host_sa(addr, alen, &ss) : 0;
    int r = ::sendto(s, (const char*)b, (int)n, map_msg_flags(flags), addr ? (sockaddr*)&ss : nullptr, l);
    if (r < 0) {
        int e = WSAGetLastError();
        static int reported = 0;
        if (reported++ < 5) {
            char host[64] = "?";
            DWORD hl = sizeof host;
            WSAAddressToStringA((sockaddr*)&ss, l, nullptr, host, &hl);
            LOGW("sendto(%s, family %d, len %u) failed: %d", host, addr ? addr[1] : -1, alen, e);
        }
        set_errno(map_wsa(e));
        return -1;
    }
    return r;
}
HLE_FUNC(recv, s64, (int fd, void* b, size_t n, int flags)) {
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    int r = ::recv(s, (char*)b, (int)n, map_msg_flags(flags));
    if (r < 0) { set_wsa_errno(); return -1; }
    return r;
}
HLE_FUNC(recvfrom, s64, (int fd, void* b, size_t n, int flags, u8* addr, u32* alen)) {
    LOGD("net: %s", "recvfrom");
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    sockaddr_storage ss;
    int l = sizeof ss;
    int r = ::recvfrom(s, (char*)b, (int)n, map_msg_flags(flags), (sockaddr*)&ss, &l);
    if (r < 0) {
        int e = WSAGetLastError();
        if (e == WSAEMSGSIZE) r = (int)n;  // truncated datagram: BSD returns the truncated length
        else if (e == WSAECONNRESET) { set_errno(D_EAGAIN); return -1; }  // ICMP port unreachable on UDP
        else { set_errno(map_wsa(e)); return -1; }
    }
    if (addr && alen) *alen = to_guest_sa((sockaddr*)&ss, addr, *alen);
    return r;
}
// struct msghdr { void* name; u32 namelen; iovec* iov; int iovlen; void* control; u32 controllen; int flags; }
struct DMsgHdr { u64 name; u32 namelen; u32 _p0; u64 iov; s32 iovlen; u32 _p1; u64 control; u32 controllen; s32 flags; };
struct DIovec { u64 base; u64 len; };
HLE_FUNC(sendmsg, s64, (int fd, const DMsgHdr* m, int flags)) {
    std::vector<u8> buf;
    const DIovec* iov = gptr<const DIovec>(m->iov);
    for (int i = 0; i < m->iovlen; i++) buf.insert(buf.end(), gptr<u8>(iov[i].base), gptr<u8>(iov[i].base) + iov[i].len);
    return hle_impl_sendto(fd, buf.data(), buf.size(), flags, gptr<const u8>(m->name), m->namelen);
}
HLE_FUNC(recvmsg, s64, (int fd, DMsgHdr* m, int flags)) {
    size_t total = 0;
    DIovec* iov = gptr<DIovec>(m->iov);
    for (int i = 0; i < m->iovlen; i++) total += iov[i].len;
    std::vector<u8> buf(total ? total : 1);
    u32 alen = m->namelen;
    s64 r = hle_impl_recvfrom(fd, buf.data(), total, flags, gptr<u8>(m->name), m->name ? &alen : nullptr);
    if (r < 0) return r;
    m->namelen = alen;
    m->controllen = 0;
    m->flags = 0;
    size_t off = 0;
    for (int i = 0; i < m->iovlen && off < (size_t)r; i++) {
        size_t k = std::min<size_t>(iov[i].len, (size_t)r - off);
        memcpy(gptr(iov[i].base), buf.data() + off, k);
        off += k;
    }
    return r;
}
HLE_FUNC(getsockname, int, (int fd, u8* addr, u32* len)) {
    LOGD("net: %s", "getsockname");
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    sockaddr_storage ss;
    int l = sizeof ss;
    if (::getsockname(s, (sockaddr*)&ss, &l) != 0) { set_wsa_errno(); return -1; }
    *len = to_guest_sa((sockaddr*)&ss, addr, *len);
    return 0;
}
HLE_FUNC(getpeername, int, (int fd, u8* addr, u32* len)) {
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    sockaddr_storage ss;
    int l = sizeof ss;
    if (::getpeername(s, (sockaddr*)&ss, &l) != 0) { set_wsa_errno(); return -1; }
    *len = to_guest_sa((sockaddr*)&ss, addr, *len);
    return 0;
}
HLE_FUNC(setsockopt, int, (int fd, int level, int name, const void* val, u32 len)) {
    LOGD("net: %s", "setsockopt");
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    if (level == 0xffff) {
        if (name == 0x1022 || name == 0x200) return 0;  // SO_NOSIGPIPE, SO_REUSEPORT
        if (name == 0x1005 || name == 0x1006) {           // SO_SNDTIMEO / SO_RCVTIMEO: timeval -> ms
            const d_timeval* tv = static_cast<const d_timeval*>(val);
            DWORD ms = (DWORD)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
            return ::setsockopt(s, SOL_SOCKET, name, (const char*)&ms, sizeof ms) == 0 ? 0 : (set_wsa_errno(), -1);
        }
    }
    if (level == IPPROTO_IPV6 && name == 27) name = IPV6_V6ONLY;
    if (::setsockopt(s, level, name, (const char*)val, (int)len) != 0) {
        LOGD("setsockopt(%d, %d) failed: %d", level, name, WSAGetLastError());
        return 0;  // be lenient: most options are hints
    }
    return 0;
}
HLE_FUNC(getsockopt, int, (int fd, int level, int name, void* val, u32* len)) {
    LOGD("net: %s", "getsockopt");
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    int l = (int)*len;
    if (::getsockopt(s, level, name, (char*)val, &l) != 0) { set_wsa_errno(); return -1; }
    if (level == 0xffff && name == 0x1007 && l >= 4) *(int*)val = map_wsa(*(int*)val) * (*(int*)val != 0);  // SO_ERROR
    *len = (u32)l;
    return 0;
}

// ---- select / poll ----
HLE_FUNC(select, int, (int nfds, u32* rd, u32* wr, u32* ex, d_timeval* tv)) {
    LOGD("net: select(%d, tv=%lld.%06d)", nfds, tv ? (long long)tv->tv_sec : -1, tv ? tv->tv_usec : 0);
    fd_set r, w, e;
    FD_ZERO(&r); FD_ZERO(&w); FD_ZERO(&e);
    std::vector<std::pair<int, SOCKET>> map;
    std::vector<std::pair<int, int>> ready;  // non-socket fds are always ready (fd, mask bits r=1 w=2)
    bool any = false;
    for (int fd = 0; fd < nfds; fd++) {
        bool inr = rd && (rd[fd / 32] >> (fd % 32) & 1), inw = wr && (wr[fd / 32] >> (fd % 32) & 1), ine = ex && (ex[fd / 32] >> (fd % 32) & 1);
        if (!inr && !inw && !ine) continue;
        auto f = fdtab::get(fd);
        if (!f) continue;
        if (f->kind != fdtab::Kind::Socket) { ready.emplace_back(fd, (inr ? 1 : 0) | (inw ? 2 : 0)); continue; }
        SOCKET s = (SOCKET)f->sock;
        map.emplace_back(fd, s);
        if (inr) FD_SET(s, &r);
        if (inw) FD_SET(s, &w);
        if (ine) FD_SET(s, &e);
        any = true;
    }
    int words = (nfds + 31) / 32;
    if (rd) memset(rd, 0, words * 4);
    if (wr) memset(wr, 0, words * 4);
    if (ex) memset(ex, 0, words * 4);
    int count = 0;
    for (auto& [fd, m] : ready) {
        if ((m & 1) && rd) rd[fd / 32] |= 1u << (fd % 32);
        if ((m & 2) && wr) wr[fd / 32] |= 1u << (fd % 32);
        count++;
    }
    if (count) {
        // also report sockets that are ready right now, without blocking
        timeval zero{};
        if (any && ::select(0, &r, &w, &e, &zero) > 0) goto collect;
        return count;
    }
    if (!any) {
        if (tv) Sleep((DWORD)(tv->tv_sec * 1000 + tv->tv_usec / 1000));
        else Sleep(INFINITE);
        return 0;
    }
    {
        timeval t{};
        if (tv) { t.tv_sec = (long)tv->tv_sec; t.tv_usec = tv->tv_usec; }
        int n = ::select(0, &r, &w, &e, tv ? &t : nullptr);
        if (n < 0) { set_wsa_errno(); return -1; }
    }
collect:
    for (auto& [fd, s] : map) {
        bool hit = false;
        if (rd && FD_ISSET(s, &r)) { rd[fd / 32] |= 1u << (fd % 32); hit = true; }
        if (wr && FD_ISSET(s, &w)) { wr[fd / 32] |= 1u << (fd % 32); hit = true; }
        if (ex && FD_ISSET(s, &e)) { ex[fd / 32] |= 1u << (fd % 32); hit = true; }
        count += hit;
    }
    return count;
}
HLE_ALIAS("_select$1050", hle_impl_select);
HLE_ALIAS("_select$DARWIN_EXTSN", hle_impl_select);
struct DPollFd { s32 fd; s16 events, revents; };
HLE_FUNC(poll, int, (DPollFd* fds, u32 n, int timeout)) {
    LOGD("net: %s", "poll");
    std::vector<WSAPOLLFD> w(n);
    bool sockets = false;
    for (u32 i = 0; i < n; i++) {
        fds[i].revents = 0;
        auto f = fdtab::get(fds[i].fd);
        w[i].fd = f && f->kind == fdtab::Kind::Socket ? (SOCKET)f->sock : INVALID_SOCKET;
        short ev = 0;
        if (fds[i].events & (1 | 0x40)) ev |= POLLRDNORM;
        if (fds[i].events & 4) ev |= POLLWRNORM;
        w[i].events = ev;
        if (w[i].fd != INVALID_SOCKET) sockets = true;
    }
    if (!sockets) {
        // files/pipes are always ready
        int c = 0;
        for (u32 i = 0; i < n; i++) if (fdtab::get(fds[i].fd)) { fds[i].revents = fds[i].events & 5; c += fds[i].revents != 0; }
        if (!c && timeout > 0) Sleep(timeout);
        return c;
    }
    int r = WSAPoll(w.data(), n, timeout);
    if (r < 0) { set_wsa_errno(); return -1; }
    int c = 0;
    for (u32 i = 0; i < n; i++) {
        short re = w[i].revents, d = 0;
        if (re & (POLLRDNORM | POLLRDBAND)) d |= 1;
        if (re & POLLWRNORM) d |= 4;
        if (re & POLLERR) d |= 8;
        if (re & POLLHUP) d |= 0x10;
        if (re & POLLNVAL) d |= 0x20;
        fds[i].revents = d;
        c += d != 0;
    }
    return c;
}

// ---- name resolution ----
// struct addrinfo { int flags, family, socktype, protocol; u32 addrlen; char* canonname; sockaddr* addr; addrinfo* next; }
struct DAddrInfo { s32 flags, family, socktype, protocol; u32 addrlen; u32 _pad; u64 canonname; u64 addr; u64 next; };
static_assert(sizeof(DAddrInfo) == 48);
HLE_FUNC(getaddrinfo, int, (const char* node, const char* service, const DAddrInfo* hints, DAddrInfo** res)) {
    LOGD("net: %s", "getaddrinfo");
    wsa_init();
    addrinfo h{}, *out = nullptr;
    if (hints) {
        int f = hints->flags;
        h.ai_flags = (f & 1 ? AI_PASSIVE : 0) | (f & 2 ? AI_CANONNAME : 0) | (f & 4 ? AI_NUMERICHOST : 0) | (f & 0x1000 ? AI_NUMERICSERV : 0) |
                     (f & 0x400 ? AI_ADDRCONFIG : 0);
        h.ai_family = to_host_af(hints->family);
        h.ai_socktype = hints->socktype;
        h.ai_protocol = hints->protocol;
    }
    int r = ::getaddrinfo(node, service, hints ? &h : nullptr, &out);
    if (r != 0) {
        *res = nullptr;
        // Darwin EAI_*: AGAIN 2, BADFLAGS 3, FAIL 4, FAMILY 5, MEMORY 6, NONAME 8, SERVICE 9, SOCKTYPE 10
        switch (r) {
        case WSATRY_AGAIN: return 2;
        case WSAEINVAL: return 3;
        case WSANO_RECOVERY: return 4;
        case WSAEAFNOSUPPORT: return 5;
        case WSA_NOT_ENOUGH_MEMORY: return 6;
        case WSATYPE_NOT_FOUND: return 9;
        case WSAESOCKTNOSUPPORT: return 10;
        default: return 8;
        }
    }
    DAddrInfo* head = nullptr;
    DAddrInfo** tail = &head;
    for (addrinfo* a = out; a; a = a->ai_next) {
        if (a->ai_family != AF_INET && a->ai_family != AF_INET6) continue;
        auto* d = static_cast<DAddrInfo*>(vmem::gcalloc(1, sizeof(DAddrInfo) + 32));
        u8* sa = reinterpret_cast<u8*>(d + 1);
        d->family = to_guest_af(a->ai_family);
        d->socktype = a->ai_socktype;
        d->protocol = a->ai_protocol;
        d->addrlen = to_guest_sa(a->ai_addr, sa, 32);
        d->addr = gaddr_of(sa);
        if (a->ai_canonname) {
            size_t l = strlen(a->ai_canonname) + 1;
            char* c = static_cast<char*>(vmem::gmalloc(l));
            memcpy(c, a->ai_canonname, l);
            d->canonname = gaddr_of(c);
        }
        *tail = d;
        tail = reinterpret_cast<DAddrInfo**>(&d->next);
    }
    ::freeaddrinfo(out);
    *res = head;
    return head ? 0 : 8;
}
HLE_FUNC(freeaddrinfo, void, (DAddrInfo* a)) {
    while (a) {
        DAddrInfo* n = gptr<DAddrInfo>(a->next);
        if (a->canonname) vmem::gfree(gptr(a->canonname));
        vmem::gfree(a);
        a = n;
    }
}
HLE_FUNC(gai_strerror, const char*, (int e)) {
    switch (e) {
    case 8: return "nodename nor servname provided, or not known";
    case 2: return "temporary failure in name resolution";
    default: return "name resolution failure";
    }
}
HLE_FUNC(getnameinfo, int, (const u8* sa, u32 salen, char* host, u32 hl, char* serv, u32 sl, int flags)) {
    LOGD("net: %s", "getnameinfo");
    wsa_init();
    sockaddr_storage ss;
    int l = to_host_sa(sa, salen, &ss);
    int f = (flags & 2 ? NI_NUMERICHOST : 0) | (flags & 8 ? NI_NUMERICSERV : 0) | (flags & 1 ? NI_NOFQDN : 0) | (flags & 4 ? NI_NAMEREQD : 0) |
            (flags & 0x10 ? NI_DGRAM : 0);
    int r = ::getnameinfo((sockaddr*)&ss, l, host, hl, serv, sl, f);
    return r == 0 ? 0 : 8;
}
// struct hostent { char* name; char** aliases; int addrtype; int length; char** addr_list; }
struct DHostent { u64 name; u64 aliases; s32 addrtype; s32 length; u64 addr_list; };
static DHostent* make_hostent(const hostent* h) {
    if (!h) return nullptr;
    auto* d = static_cast<DHostent*>(vmem::gcalloc(1, sizeof(DHostent)));
    size_t nl = strlen(h->h_name) + 1;
    char* name = static_cast<char*>(vmem::gmalloc(nl));
    memcpy(name, h->h_name, nl);
    d->name = gaddr_of(name);
    u64* aliases = static_cast<u64*>(vmem::gcalloc(1, 8));
    d->aliases = gaddr_of(aliases);
    d->addrtype = to_guest_af(h->h_addrtype);
    d->length = h->h_length;
    int n = 0;
    while (h->h_addr_list[n]) n++;
    u64* list = static_cast<u64*>(vmem::gcalloc(n + 1, 8));
    for (int i = 0; i < n; i++) {
        char* a = static_cast<char*>(vmem::gmalloc(h->h_length));
        memcpy(a, h->h_addr_list[i], h->h_length);
        list[i] = gaddr_of(a);
    }
    d->addr_list = gaddr_of(list);
    return d;
}
static void free_hostent(DHostent* d) {
    if (!d) return;
    vmem::gfree(gptr(d->name));
    vmem::gfree(gptr(d->aliases));
    u64* l = gptr<u64>(d->addr_list);
    for (int i = 0; l[i]; i++) vmem::gfree(gptr(l[i]));
    vmem::gfree(l);
    vmem::gfree(d);
}
HLE_FUNC(gethostbyname, DHostent*, (const char* name)) {
    LOGD("net: %s", "gethostbyname");
    wsa_init();
    static thread_local DHostent* last;
    free_hostent(last);
    last = make_hostent(::gethostbyname(name));
    return last;
}
HLE_FUNC(gethostbyaddr, DHostent*, (const void* addr, u32 len, int type)) {
    wsa_init();
    static thread_local DHostent* last;
    free_hostent(last);
    last = make_hostent(::gethostbyaddr((const char*)addr, (int)len, to_host_af(type)));
    return last;
}
HLE_FUNC(getipnodebyname, DHostent*, (const char* name, int af, int flags, int* err)) {
    (void)flags;
    wsa_init();
    addrinfo h{}, *out = nullptr;
    h.ai_family = to_host_af(af);
    if (::getaddrinfo(name, nullptr, &h, &out) != 0 || !out) { if (err) *err = 1; return nullptr; }
    hostent he{};
    he.h_name = (char*)name;
    he.h_addrtype = (short)out->ai_family;
    he.h_length = out->ai_family == AF_INET6 ? 16 : 4;
    char* addrs[2] = {nullptr, nullptr};
    char buf[16];
    if (out->ai_family == AF_INET6) memcpy(buf, &((sockaddr_in6*)out->ai_addr)->sin6_addr, 16);
    else memcpy(buf, &((sockaddr_in*)out->ai_addr)->sin_addr, 4);
    addrs[0] = buf;
    he.h_addr_list = addrs;
    char* aliases[1] = {nullptr};
    he.h_aliases = aliases;
    DHostent* d = make_hostent(&he);
    ::freeaddrinfo(out);
    if (err) *err = 0;
    return d;
}
HLE_FUNC(freehostent, void, (DHostent* h)) { free_hostent(h); }
HLE_FUNC(getservbyname, void*, (const char*, const char*)) { return nullptr; }
HLE_FUNC(gethostbyname2, DHostent*, (const char* name, int af)) { int e; return hle_impl_getipnodebyname(name, af, 0, &e); }

// ---- address conversion ----
HLE_FUNC(inet_addr, u32, (const char* s)) {
    LOGD("net: %s", "inet_addr");
    in_addr a;
    return inet_pton(AF_INET, s, &a) == 1 ? a.s_addr : 0xffffffffu;
}
HLE_FUNC(inet_aton, int, (const char* s, u32* out)) {
    in_addr a;
    if (inet_pton(AF_INET, s, &a) != 1) return 0;
    *out = a.s_addr;
    return 1;
}
HLE_FUNC(inet_ntoa, const char*, (u32 a)) {
    LOGD("net: %s", "inet_ntoa");
    static thread_local char buf[32];
    in_addr ia;
    ia.s_addr = a;
    inet_ntop(AF_INET, &ia, buf, sizeof buf);
    return buf;
}
HLE_FUNC(inet_ntop, const char*, (int af, const void* src, char* dst, u32 len)) {
    LOGD("net: %s", "inet_ntop");
    return ::inet_ntop(to_host_af(af), src, dst, len) ? dst : nullptr;
}
HLE_FUNC(inet_pton, int, (int af, const char* src, void* dst)) { return ::inet_pton(to_host_af(af), src, dst); }
HLE_FUNC(htonl, u32, (u32 v)) { return _byteswap_ulong(v); }
HLE_FUNC(htons, u16, (u16 v)) { return _byteswap_ushort(v); }
HLE_FUNC(ntohl, u32, (u32 v)) { return _byteswap_ulong(v); }
HLE_FUNC(ntohs, u16, (u16 v)) { return _byteswap_ushort(v); }
HLE_DATA(in6addr_any, 16) {}
HLE_DATA(in6addr_loopback, 16) { static_cast<u8*>(p)[15] = 1; }

// ---- interfaces ----
// struct ifaddrs { ifaddrs* next; char* name; u32 flags; sockaddr* addr; sockaddr* netmask; sockaddr* dstaddr; void* data; }
struct DIfAddrs { u64 next, name; u32 flags, _pad; u64 addr, netmask, dstaddr, data; };
HLE_FUNC(getifaddrs, int, (DIfAddrs** out)) {
    LOGD("net: %s", "getifaddrs");
    if (getenv("EMU_NO_IFADDRS")) { *out = nullptr; return 0; }
    ULONG size = 32 * 1024;
    std::vector<u8> buf(size);
    auto* aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS, nullptr, aa, &size) != NO_ERROR) {
        buf.resize(size);
        aa = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS, nullptr, aa, &size) != NO_ERROR) {
            *out = nullptr;
            return 0;
        }
    }
    DIfAddrs* head = nullptr;
    DIfAddrs** tail = &head;
    // Interfaces that are up, the one with a default gateway (the real LAN) first: games
    // treat en0 as the Wi-Fi interface and broadcast LAN advertisements on it.
    std::vector<IP_ADAPTER_ADDRESSES*> ifs;
    for (auto* a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_TUNNEL) continue;
        if (a->FirstGatewayAddress) ifs.insert(ifs.begin(), a);
        else ifs.push_back(a);
    }
    int idx = 0;
    for (auto* a : ifs) {
        bool loop = a->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
        std::string name = loop ? "lo0" : "en" + std::to_string(idx++);
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family == AF_INET &&
                (ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr) >> 16) == 0xa9fe)
                continue;
            auto* d = static_cast<DIfAddrs*>(vmem::gcalloc(1, sizeof(DIfAddrs) + 3 * 32 + 32));
            u8* sa = reinterpret_cast<u8*>(d + 1);
            char* nm = reinterpret_cast<char*>(sa + 96);
            strncpy(nm, name.c_str(), 31);
            d->name = gaddr_of(nm);
            d->flags = 0x1 | 0x40 | (loop ? 0x8 : 0x2 | 0x8000) | (a->OperStatus == IfOperStatusUp ? 0 : 0);
            to_guest_sa(u->Address.lpSockaddr, sa, 32);
            d->addr = gaddr_of(sa);
            // netmask from prefix length
            u8 mask[32] = {0};
            sockaddr_storage ms{};
            if (u->Address.lpSockaddr->sa_family == AF_INET) {
                auto* m4 = reinterpret_cast<sockaddr_in*>(&ms);
                m4->sin_family = AF_INET;
                u32 bits = u->OnLinkPrefixLength >= 32 ? 0xffffffffu : (u->OnLinkPrefixLength ? ~((1u << (32 - u->OnLinkPrefixLength)) - 1) : 0);
                m4->sin_addr.s_addr = htonl(bits);
                to_guest_sa((sockaddr*)&ms, sa + 32, 32);
                d->netmask = gaddr_of(sa + 32);
                // broadcast address
                sockaddr_storage bs{};
                auto* b4 = reinterpret_cast<sockaddr_in*>(&bs);
                b4->sin_family = AF_INET;
                b4->sin_addr.s_addr = (reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr) | htonl(~bits);
                to_guest_sa((sockaddr*)&bs, sa + 64, 32);
                d->dstaddr = gaddr_of(sa + 64);
            } else {
                auto* m6 = reinterpret_cast<sockaddr_in6*>(&ms);
                m6->sin6_family = AF_INET6;
                for (int i = 0; i < 16; i++) {
                    int b = std::clamp<int>(u->OnLinkPrefixLength - i * 8, 0, 8);
                    m6->sin6_addr.s6_addr[i] = (u8)(0xff00 >> b);
                }
                to_guest_sa((sockaddr*)&ms, sa + 32, 32);
                d->netmask = gaddr_of(sa + 32);
            }
            (void)mask;
            *tail = d;
            tail = reinterpret_cast<DIfAddrs**>(&d->next);
        }
    }
    *out = head;
    return 0;
}
HLE_FUNC(freeifaddrs, void, (DIfAddrs* a)) {
    while (a) {
        DIfAddrs* n = gptr<DIfAddrs>(a->next);
        vmem::gfree(a);
        a = n;
    }
}
HLE_FUNC(if_nametoindex, u32, (const char* n)) {
    LOGD("net: %s", "if_nametoindex");
    if (!n) return 0;
    if (!strcmp(n, "lo0")) return 1;
    if (!strncmp(n, "en", 2)) return (u32)atoi(n + 2) + 2;
    return 0;
}
HLE_FUNC(if_indextoname, char*, (u32 i, char* buf)) {
    if (i == 1) strcpy(buf, "lo0");
    else snprintf(buf, 16, "en%u", i - 2);
    return buf;
}

// ---- kqueue (used by boost.asio): pretend it is unavailable ----
HLE_FUNC(kqueue, int, ()) { set_errno(D_ENOSYS); return -1; }
HLE_FUNC(kevent, int, (int, const void*, int, void*, int, const void*)) { set_errno(D_EBADF); return -1; }
