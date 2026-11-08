#include "gfmt.h"

#include <cctype>
#include <cstdlib>

namespace gfmt {

size_t u32len(const char32_t* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

std::string utf32_to_utf8(const char32_t* s, size_t n) {
    std::string r;
    if (!s) return r;
    if (n == (size_t)-1) n = u32len(s);
    for (size_t i = 0; i < n; i++) {
        char32_t c = s[i];
        if (c < 0x80) r += char(c);
        else if (c < 0x800) { r += char(0xc0 | (c >> 6)); r += char(0x80 | (c & 0x3f)); }
        else if (c < 0x10000) { r += char(0xe0 | (c >> 12)); r += char(0x80 | ((c >> 6) & 0x3f)); r += char(0x80 | (c & 0x3f)); }
        else { r += char(0xf0 | (c >> 18)); r += char(0x80 | ((c >> 12) & 0x3f)); r += char(0x80 | ((c >> 6) & 0x3f)); r += char(0x80 | (c & 0x3f)); }
    }
    return r;
}

std::u32string utf8_to_utf32(const char* s, size_t n) {
    std::u32string r;
    if (!s) return r;
    if (n == (size_t)-1) n = strlen(s);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    size_t i = 0;
    while (i < n) {
        unsigned c = p[i];
        char32_t cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; extra = 1; }
        else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; extra = 2; }
        else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; extra = 3; }
        else { cp = 0xfffd; extra = 0; }
        i++;
        for (int k = 0; k < extra && i < n; k++, i++) cp = (cp << 6) | (p[i] & 0x3f);
        r += cp;
    }
    return r;
}

namespace {

struct Spec {
    std::string flags;
    int width = -1, prec = -1;
    char len[3] = {0, 0, 0};
    char conv = 0;
};

// Parses one conversion spec starting after '%'. Returns pointer after it.
template <class CharT>
const CharT* parse_spec(const CharT* f, Spec& s, hle::VaReader& va) {
    while (*f == '-' || *f == '+' || *f == ' ' || *f == '#' || *f == '0' || *f == '\'') {
        if (*f != '\'') s.flags += char(*f);
        f++;
    }
    if (*f == '*') { s.width = int(va.next_s64()); if (s.width < 0) { s.flags += '-'; s.width = -s.width; } f++; }
    else if (*f >= '0' && *f <= '9') { s.width = 0; while (*f >= '0' && *f <= '9') s.width = s.width * 10 + (*f++ - '0'); }
    if (*f == '.') {
        f++;
        if (*f == '*') { s.prec = int(va.next_s64()); f++; }
        else { s.prec = 0; while (*f >= '0' && *f <= '9') s.prec = s.prec * 10 + (*f++ - '0'); }
    }
    int li = 0;
    while (*f == 'h' || *f == 'l' || *f == 'L' || *f == 'q' || *f == 'j' || *f == 'z' || *f == 't') {
        if (li < 2) s.len[li++] = char(*f);
        f++;
    }
    s.conv = char(*f);
    if (*f) f++;
    return f;
}

std::string host_spec(const Spec& s, const char* len, char conv) {
    std::string r = "%" + s.flags;
    if (s.width >= 0) r += std::to_string(s.width);
    if (s.prec >= 0) r += "." + std::to_string(s.prec);
    r += len;
    r += conv;
    return r;
}

template <class... A>
std::string sfmt(const std::string& spec, A... a) {
    char buf[512];
    int n = snprintf(buf, sizeof buf, spec.c_str(), a...);
    if (n < 0) return {};
    if (n < (int)sizeof buf) return std::string(buf, n);
    std::string r(n + 1, '\0');
    snprintf(r.data(), r.size(), spec.c_str(), a...);
    r.resize(n);
    return r;
}

bool is_64(const Spec& s) {
    return s.len[0] == 'l' || s.len[0] == 'q' || s.len[0] == 'j' || s.len[0] == 'z' || s.len[0] == 't' || s.len[0] == 'L';
}

std::string conv_one(const Spec& s, hle::VaReader& va, size_t out_so_far) {
    switch (s.conv) {
    case 'd': case 'i': {
        s64 v = va.next_s64();
        if (!is_64(s)) {
            if (s.len[0] == 'h' && s.len[1] == 'h') v = (signed char)v;
            else if (s.len[0] == 'h') v = (short)v;
            else v = (int)v;
        }
        return sfmt(host_spec(s, "ll", 'd'), (long long)v);
    }
    case 'u': case 'o': case 'x': case 'X': {
        u64 v = va.next_u64();
        if (!is_64(s)) {
            if (s.len[0] == 'h' && s.len[1] == 'h') v = (unsigned char)v;
            else if (s.len[0] == 'h') v = (unsigned short)v;
            else v = (unsigned)v;
        }
        return sfmt(host_spec(s, "ll", s.conv), (unsigned long long)v);
    }
    case 'D': case 'U': case 'O': {
        u64 v = va.next_u64();
        char c = s.conv == 'D' ? 'd' : s.conv == 'U' ? 'u' : 'o';
        return sfmt(host_spec(s, "ll", c), (unsigned long long)v);
    }
    case 'c': case 'C': {
        u64 v = va.next_u64();
        if (s.len[0] == 'l' || s.conv == 'C') {
            char32_t c = char32_t(v);
            std::string u = utf32_to_utf8(&c, 1);
            Spec t = s; t.prec = -1;
            return sfmt(host_spec(t, "", 's'), u.c_str());
        }
        return sfmt(host_spec(s, "", 'c'), (int)(char)v);
    }
    case 's': case 'S': {
        u64 p = va.next_u64();
        if (!p) return sfmt(host_spec(s, "", 's'), "(null)");
        if (s.len[0] == 'l' || s.conv == 'S') {
            const char32_t* w = gptr<const char32_t>(p);
            size_t n = 0;
            while (w[n] && (s.prec < 0 || (int)n < s.prec)) n++;
            std::string u = utf32_to_utf8(w, n);
            Spec t = s; t.prec = -1;
            return sfmt(host_spec(t, "", 's'), u.c_str());
        }
        return sfmt(host_spec(s, "", 's'), gptr<const char>(p));
    }
    case 'p': {
        u64 v = va.next_u64();
        Spec t = s; t.flags += '#';
        if (!v) return sfmt(host_spec(s, "", 's'), "0x0");
        return sfmt(host_spec(t, "ll", 'x'), (unsigned long long)v);
    }
    case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
        double v = va.next_double();
        return sfmt(host_spec(s, "", s.conv), v);
    }
    case 'n': {
        u64 p = va.next_u64();
        if (p) {
            if (is_64(s)) *gptr<s64>(p) = (s64)out_so_far;
            else if (s.len[0] == 'h') *gptr<short>(p) = (short)out_so_far;
            else *gptr<int>(p) = (int)out_so_far;
        }
        return {};
    }
    case '@': {
        // Objective-C object: should not reach libc printf; print pointer
        u64 v = va.next_u64();
        return sfmt("<obj %llx>", (unsigned long long)v);
    }
    case '%': return "%";
    default: return {};
    }
}

}  // namespace

std::string format(const char* fmt, hle::VaReader& va) {
    std::string out;
    if (!fmt) return out;
    const char* f = fmt;
    while (*f) {
        if (*f != '%') {
            const char* e = strchr(f, '%');
            if (!e) { out.append(f); break; }
            out.append(f, e - f);
            f = e;
            continue;
        }
        f++;
        if (*f == '%') { out += '%'; f++; continue; }
        Spec s;
        f = parse_spec(f, s, va);
        out += conv_one(s, va, out.size());
    }
    return out;
}

std::u32string wformat(const char32_t* fmt, hle::VaReader& va) {
    // Convert the format to UTF-8, format, convert back (good enough for libc++'s needs)
    std::u32string out;
    const char32_t* f = fmt;
    while (*f) {
        if (*f != U'%') { out += *f++; continue; }
        f++;
        if (*f == U'%') { out += U'%'; f++; continue; }
        Spec s;
        f = parse_spec(f, s, va);
        std::string r = conv_one(s, va, out.size());
        out += utf8_to_utf32(r.c_str(), r.size());
    }
    return out;
}

int scan(const char* in, const char* fmt, hle::VaReader& va, size_t* consumed) {
    // A small scanf implementation supporting the conversions used in practice.
    const char* s = in;
    const char* f = fmt;
    int assigned = 0;
    bool any_input = false;
    while (*f) {
        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*f)) f++;
            while (isspace((unsigned char)*s)) s++;
            continue;
        }
        if (*f != '%') {
            if (*s != *f) break;
            s++; f++;
            continue;
        }
        f++;
        if (*f == '%') {
            while (isspace((unsigned char)*s)) s++;
            if (*s != '%') break;
            s++; f++;
            continue;
        }
        bool suppress = false;
        if (*f == '*') { suppress = true; f++; }
        int width = 0;
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        char len[3] = {0, 0, 0};
        int li = 0;
        while (*f == 'h' || *f == 'l' || *f == 'L' || *f == 'q' || *f == 'j' || *f == 'z' || *f == 't') {
            if (li < 2) len[li++] = *f;
            f++;
        }
        char conv = *f++;
        if (conv != 'c' && conv != '[' && conv != 'n') while (isspace((unsigned char)*s)) s++;
        if (!*s && conv != 'n') { if (!any_input) assigned = -1; break; }
        auto store_int = [&](u64 v) {
            if (suppress) return;
            u64 p = va.next_u64();
            if (len[0] == 'h' && len[1] == 'h') *gptr<u8>(p) = (u8)v;
            else if (len[0] == 'h') *gptr<u16>(p) = (u16)v;
            else if (len[0] == 'l' || len[0] == 'q' || len[0] == 'j' || len[0] == 'z' || len[0] == 't' || len[0] == 'L') *gptr<u64>(p) = v;
            else *gptr<u32>(p) = (u32)v;
            assigned++;
        };
        std::string tok;
        auto take = [&](auto pred) {
            int n = 0;
            while (*s && pred(*s) && (!width || n < width)) { tok += *s++; n++; }
        };
        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
            int base = conv == 'x' || conv == 'X' ? 16 : conv == 'o' ? 8 : conv == 'i' ? 0 : 10;
            int n = 0;
            if ((*s == '-' || *s == '+') && (!width || n < width)) { tok += *s++; n++; }
            if ((base == 16 || base == 0) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { tok += *s++; tok += *s++; n += 2; if (!base) base = 16; }
            while (*s && (!width || n < width) && (isxdigit((unsigned char)*s))) {
                int d = isdigit((unsigned char)*s) ? *s - '0' : (tolower(*s) - 'a' + 10);
                int b = base ? base : (tok.size() && tok.back() == '0' && tok.size() == 1 ? 8 : 10);
                if (d >= b) break;
                tok += *s++; n++;
            }
            if (tok.empty() || tok == "-" || tok == "+") goto done;
            u64 v = (conv == 'd' || conv == 'i') ? (u64)strtoll(tok.c_str(), nullptr, base) : strtoull(tok.c_str(), nullptr, base);
            store_int(v);
            any_input = true;
        } break;
        case 'f': case 'e': case 'g': case 'E': case 'G': case 'a': {
            char* end = nullptr;
            double v = strtod(s, &end);
            if (end == s) goto done;
            if (width && end - s > width) { std::string t(s, width); v = strtod(t.c_str(), nullptr); end = (char*)s + width; }
            s = end;
            if (!suppress) {
                u64 p = va.next_u64();
                if (len[0] == 'l' || len[0] == 'L') *gptr<double>(p) = v;
                else *gptr<float>(p) = (float)v;
                assigned++;
            }
            any_input = true;
        } break;
        case 's': {
            take([](char c) { return !isspace((unsigned char)c); });
            if (tok.empty()) goto done;
            if (!suppress) { char* p = va.next_ptr<char>(); memcpy(p, tok.c_str(), tok.size() + 1); assigned++; }
            any_input = true;
        } break;
        case 'c': {
            int n = width ? width : 1;
            if ((int)strlen(s) < n) goto done;
            if (!suppress) { char* p = va.next_ptr<char>(); memcpy(p, s, n); assigned++; }
            s += n;
            any_input = true;
        } break;
        case '[': {
            bool neg = false;
            if (*f == '^') { neg = true; f++; }
            std::string set;
            if (*f == ']') set += *f++;
            while (*f && *f != ']') set += *f++;
            if (*f == ']') f++;
            auto in_set = [&](char c) {
                for (size_t i = 0; i < set.size(); i++) {
                    if (i + 2 < set.size() && set[i + 1] == '-') {
                        if (c >= set[i] && c <= set[i + 2]) return true;
                        i += 2;
                    } else if (set[i] == c) return true;
                }
                return false;
            };
            take([&](char c) { return in_set(c) != neg; });
            if (tok.empty()) goto done;
            if (!suppress) { char* p = va.next_ptr<char>(); memcpy(p, tok.c_str(), tok.size() + 1); assigned++; }
            any_input = true;
        } break;
        case 'n':
            if (!suppress) { u64 p = va.next_u64(); *gptr<int>(p) = int(s - in); }
            break;
        default: goto done;
        }
    }
done:
    if (consumed) *consumed = size_t(s - in);
    return assigned;
}

}  // namespace gfmt
