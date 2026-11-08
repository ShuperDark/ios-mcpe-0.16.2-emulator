// CommonCrypto (libSystem): digests, HMAC and AES used by the game, on top of Windows CNG.
#include "hle.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <cstring>

#pragma comment(lib, "bcrypt.lib")

namespace {

BCRYPT_ALG_HANDLE open_alg(const wchar_t* id, bool hmac) {
    BCRYPT_ALG_HANDLE h = nullptr;
    if (BCryptOpenAlgorithmProvider(&h, id, nullptr, hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0) < 0) return nullptr;
    return h;
}

// providers are thread safe and kept for the whole run
BCRYPT_ALG_HANDLE alg(const wchar_t* id, bool hmac) {
    static BCRYPT_ALG_HANDLE cache[2][6] = {};
    static const wchar_t* ids[6] = {BCRYPT_SHA1_ALGORITHM, BCRYPT_MD5_ALGORITHM, BCRYPT_SHA256_ALGORITHM,
                                    BCRYPT_SHA384_ALGORITHM, BCRYPT_SHA512_ALGORITHM, BCRYPT_SHA256_ALGORITHM};
    for (int i = 0; i < 6; i++)
        if (!wcscmp(ids[i], id)) {
            BCRYPT_ALG_HANDLE& c = cache[hmac][i];
            if (!c) c = open_alg(id, hmac);
            return c;
        }
    return open_alg(id, hmac);
}

void digest(const wchar_t* id, const void* data, size_t len, u8* out, ULONG outlen) {
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptCreateHash(alg(id, false), &h, nullptr, 0, nullptr, 0, 0) < 0) { memset(out, 0, outlen); return; }
    BCryptHashData(h, (PUCHAR)data, (ULONG)len, 0);
    BCryptFinishHash(h, out, outlen, 0);
    BCryptDestroyHash(h);
}

// Incremental contexts live in guest memory (CC_SHA256_CTX is 104 bytes, CCHmacContext
// 384); the CNG hash handle is kept in the first 8 bytes.
struct Incremental { BCRYPT_HASH_HANDLE h; u32 outlen; u32 magic; };
constexpr u32 kMagic = 0x43434d44;  // 'CCMD'

void inc_init(void* ctx, const wchar_t* id, bool hmac, const void* key, size_t keylen, u32 outlen) {
    auto* c = static_cast<Incremental*>(ctx);
    c->h = nullptr;
    c->outlen = outlen;
    c->magic = kMagic;
    BCryptCreateHash(alg(id, hmac), &c->h, nullptr, 0, (PUCHAR)key, (ULONG)keylen, BCRYPT_HASH_REUSABLE_FLAG);
}
void inc_update(void* ctx, const void* data, size_t len) {
    auto* c = static_cast<Incremental*>(ctx);
    if (c->magic == kMagic && c->h) BCryptHashData(c->h, (PUCHAR)data, (ULONG)len, 0);
}
void inc_final(void* ctx, u8* out) {
    auto* c = static_cast<Incremental*>(ctx);
    if (c->magic != kMagic || !c->h) return;
    BCryptFinishHash(c->h, out, c->outlen, 0);
    BCryptDestroyHash(c->h);
    c->h = nullptr;
    c->magic = 0;
}

}  // namespace

// ---- one-shot digests ----
HLE_FUNC(CC_MD5, u8*, (const void* d, u32 len, u8* md)) { digest(BCRYPT_MD5_ALGORITHM, d, len, md, 16); return md; }
HLE_FUNC(CC_SHA1, u8*, (const void* d, u32 len, u8* md)) { digest(BCRYPT_SHA1_ALGORITHM, d, len, md, 20); return md; }
HLE_FUNC(CC_SHA256, u8*, (const void* d, u32 len, u8* md)) { digest(BCRYPT_SHA256_ALGORITHM, d, len, md, 32); return md; }
HLE_FUNC(CC_SHA512, u8*, (const void* d, u32 len, u8* md)) { digest(BCRYPT_SHA512_ALGORITHM, d, len, md, 64); return md; }

// ---- incremental digests ----
HLE_FUNC(CC_SHA256_Init, int, (void* c)) { inc_init(c, BCRYPT_SHA256_ALGORITHM, false, nullptr, 0, 32); return 1; }
HLE_FUNC(CC_SHA256_Update, int, (void* c, const void* d, u32 len)) { inc_update(c, d, len); return 1; }
HLE_FUNC(CC_SHA256_Final, int, (u8* md, void* c)) { inc_final(c, md); return 1; }
HLE_FUNC(CC_SHA1_Init, int, (void* c)) { inc_init(c, BCRYPT_SHA1_ALGORITHM, false, nullptr, 0, 20); return 1; }
HLE_FUNC(CC_SHA1_Update, int, (void* c, const void* d, u32 len)) { inc_update(c, d, len); return 1; }
HLE_FUNC(CC_SHA1_Final, int, (u8* md, void* c)) { inc_final(c, md); return 1; }
HLE_FUNC(CC_MD5_Init, int, (void* c)) { inc_init(c, BCRYPT_MD5_ALGORITHM, false, nullptr, 0, 16); return 1; }
HLE_FUNC(CC_MD5_Update, int, (void* c, const void* d, u32 len)) { inc_update(c, d, len); return 1; }
HLE_FUNC(CC_MD5_Final, int, (u8* md, void* c)) { inc_final(c, md); return 1; }

// ---- HMAC (kCCHmacAlgSHA1 0, MD5 1, SHA256 2, SHA384 3, SHA512 4, SHA224 5) ----
static void hmac_alg(u32 a, const wchar_t** id, u32* len) {
    switch (a) {
    case 1: *id = BCRYPT_MD5_ALGORITHM; *len = 16; break;
    case 2: *id = BCRYPT_SHA256_ALGORITHM; *len = 32; break;
    case 3: *id = BCRYPT_SHA384_ALGORITHM; *len = 48; break;
    case 4: *id = BCRYPT_SHA512_ALGORITHM; *len = 64; break;
    default: *id = BCRYPT_SHA1_ALGORITHM; *len = 20; break;
    }
}
HLE_FUNC(CCHmacInit, void, (void* c, u32 a, const void* key, size_t keylen)) {
    const wchar_t* id;
    u32 len;
    hmac_alg(a, &id, &len);
    inc_init(c, id, true, key, keylen, len);
}
HLE_FUNC(CCHmacUpdate, void, (void* c, const void* d, size_t len)) { inc_update(c, d, len); }
HLE_FUNC(CCHmacFinal, void, (void* c, u8* mac)) { inc_final(c, mac); }
HLE_FUNC(CCHmac, void, (u32 a, const void* key, size_t keylen, const void* d, size_t len, u8* mac)) {
    u8 ctx[64];
    const wchar_t* id;
    u32 ol;
    hmac_alg(a, &id, &ol);
    inc_init(ctx, id, true, key, keylen, ol);
    inc_update(ctx, d, len);
    inc_final(ctx, mac);
}

// ---- CCCrypt: AES-128/192/256, CBC (default) or ECB, optional PKCS#7 padding ----
// CCOperation 0 encrypt / 1 decrypt; CCAlgorithm 0 = AES; options 1 = PKCS7, 2 = ECB
HLE_FUNC(CCCrypt, s32, (u32 op, u32 algo, u32 options, const void* key, size_t keylen, const void* iv, const void* in,
                        size_t inlen, void* out, size_t outavail, size_t* outmoved)) {
    if (outmoved) *outmoved = 0;
    if (algo != 0) return -4305;  // kCCUnimplemented
    static BCRYPT_ALG_HANDLE cbc = [] {
        BCRYPT_ALG_HANDLE h = open_alg(BCRYPT_AES_ALGORITHM, false);
        if (h) BCryptSetProperty(h, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
        return h;
    }();
    static BCRYPT_ALG_HANDLE ecb = [] {
        BCRYPT_ALG_HANDLE h = open_alg(BCRYPT_AES_ALGORITHM, false);
        if (h) BCryptSetProperty(h, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
        return h;
    }();
    BCRYPT_ALG_HANDLE a = (options & 2) ? ecb : cbc;
    BCRYPT_KEY_HANDLE k = nullptr;
    if (!a || BCryptGenerateSymmetricKey(a, &k, nullptr, 0, (PUCHAR)key, (ULONG)keylen, 0) < 0) return -4300;  // kCCParamError
    u8 ivbuf[16] = {};
    if (iv && !(options & 2)) memcpy(ivbuf, iv, 16);
    ULONG flags = (options & 1) ? BCRYPT_BLOCK_PADDING : 0;
    ULONG got = 0;
    NTSTATUS st = op == 0 ? BCryptEncrypt(k, (PUCHAR)in, (ULONG)inlen, nullptr, (options & 2) ? nullptr : ivbuf, (options & 2) ? 0 : 16,
                                          (PUCHAR)out, (ULONG)outavail, &got, flags)
                          : BCryptDecrypt(k, (PUCHAR)in, (ULONG)inlen, nullptr, (options & 2) ? nullptr : ivbuf, (options & 2) ? 0 : 16,
                                          (PUCHAR)out, (ULONG)outavail, &got, flags);
    BCryptDestroyKey(k);
    if (st < 0) return st == (NTSTATUS)0xC0000023L ? -4301 : -4304;  // buffer too small / decode error
    if (outmoved) *outmoved = got;
    return 0;
}

// ---- termios (unused by the game's real paths; report "not a tty") ----
HLE_FUNC(tcgetattr, int, (int, void*)) { return -1; }
HLE_FUNC(tcsetattr, int, (int, int, const void*)) { return -1; }
