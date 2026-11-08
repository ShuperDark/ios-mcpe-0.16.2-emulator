// printf / scanf for guest code (Darwin arm64 semantics: long == 64 bit,
// wchar_t == 32 bit, variadic args in 8-byte stack slots).
#pragma once
#include "common.h"
#include "hle.h"

namespace gfmt {
std::string format(const char* fmt, hle::VaReader& va);
// Wide (32-bit wchar_t) variant: returns UTF-32 string
std::u32string wformat(const char32_t* fmt, hle::VaReader& va);
// sscanf-style parsing; returns number of assigned fields or -1 (EOF)
int scan(const char* input, const char* fmt, hle::VaReader& va, size_t* consumed = nullptr);

std::string utf32_to_utf8(const char32_t* s, size_t n = (size_t)-1);
std::u32string utf8_to_utf32(const char* s, size_t n = (size_t)-1);
size_t u32len(const char32_t* s);
}  // namespace gfmt
