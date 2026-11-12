// Host media services for the guest frameworks: image decode/encode (WIC)
// and PCM audio output (waveOut).
#include "hle.h"
#include "remote.h"
#include "vmem.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <wincodec.h>

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "winmm.lib")

// ===================================================================== images
namespace {
IWICImagingFactory* wic() {
    static IWICImagingFactory* f = [] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IWICImagingFactory* p = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&p));
        return p;
    }();
    return f;
}
}  // namespace

HLE_FUNC(emu_image_decode, void*, (const void* data, size_t len, int* w, int* h)) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* f = wic();
    if (!f || !data || !len) return nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    void* out = nullptr;
    if (SUCCEEDED(f->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory((BYTE*)data, (DWORD)len)) &&
        SUCCEEDED(f->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(f->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT iw = 0, ih = 0;
        conv->GetSize(&iw, &ih);
        out = vmem::gmalloc(size_t(iw) * ih * 4);
        if (FAILED(conv->CopyPixels(nullptr, iw * 4, iw * ih * 4, (BYTE*)out))) {
            vmem::gfree(out);
            out = nullptr;
        } else {
            *w = (int)iw;
            *h = (int)ih;
        }
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (stream) stream->Release();
    return out;
}

HLE_FUNC(emu_image_encode_png, void*, (const void* rgba, int w, int h, size_t* out_len)) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* f = wic();
    *out_len = 0;
    if (!f || !rgba || w <= 0 || h <= 0) return nullptr;
    IStream* mem = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    void* out = nullptr;
    if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &mem)) && SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
        SUCCEEDED(enc->Initialize(mem, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) &&
        SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(w, h))) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppRGBA;
        frame->SetPixelFormat(&fmt);
        // the PNG encoder may only take BGRA (it then changes fmt): swap red and blue
        std::vector<u8> bgra;
        const BYTE* px = (const BYTE*)rgba;
        if (fmt == GUID_WICPixelFormat32bppBGRA) {
            bgra.assign(px, px + size_t(w) * h * 4);
            for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
            px = bgra.data();
        } else if (fmt != GUID_WICPixelFormat32bppRGBA) {
            LOGW("image: PNG encoder wants an unsupported pixel format");
        }
        if (SUCCEEDED(frame->WritePixels(h, w * 4, w * h * 4, (BYTE*)px)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit())) {
            STATSTG st{};
            mem->Stat(&st, STATFLAG_NONAME);
            size_t n = (size_t)st.cbSize.QuadPart;
            out = vmem::gmalloc(n);
            LARGE_INTEGER zero{};
            mem->Seek(zero, STREAM_SEEK_SET, nullptr);
            ULONG got = 0;
            mem->Read(out, (ULONG)n, &got);
            *out_len = got;
        }
    }
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (mem) mem->Release();
    return out;
}

// ===================================================================== audio
namespace {
std::mutex g_alock;
std::condition_variable g_acv;
HWAVEOUT g_wave;
constexpr int kBuffers = 6;
WAVEHDR g_hdr[kBuffers];
std::vector<int16_t> g_bufmem[kBuffers];
int g_rate = 48000;
bool g_audio_open = false;
bool g_audio_failed = false;

void CALLBACK wave_cb(HWAVEOUT, UINT msg, DWORD_PTR, DWORD_PTR, DWORD_PTR) {
    if (msg == WOM_DONE) g_acv.notify_all();
}
}  // namespace

HLE_FUNC(emu_audio_open, int, (int rate, int channels)) {
    (void)channels;
    std::lock_guard lk(g_alock);
    if (g_audio_open) return 1;
    if (g_audio_failed) return 0;
    if (getenv("EMU_NO_AUDIO")) { g_audio_failed = true; return 0; }
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = rate > 0 ? rate : 48000;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * 4;
    if (waveOutOpen(&g_wave, WAVE_MAPPER, &wf, (DWORD_PTR)wave_cb, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        LOGW("audio: waveOutOpen failed");
        g_audio_failed = true;
        return 0;
    }
    g_rate = (int)wf.nSamplesPerSec;
    for (auto& h : g_hdr) h.dwFlags = WHDR_DONE;
    g_audio_open = true;
    LOGI("audio: %d Hz stereo output opened", g_rate);
    return 1;
}

// ===================================================================== sounds of mods
// emu_sound_load decodes an MP3 / Ogg Vorbis file once; emu_sound_play mixes it into the
// game's audio output (emu_audio_write), so it also reaches a connected phone.
bool decode_audio(const uint8_t* data, size_t len, std::vector<int16_t>& out, int& channels, int& rate);
namespace {
struct ModSound {
    std::vector<int16_t> pcm;  // interleaved stereo
    int rate = 44100;
};
struct ModVoice {
    int id;
    std::shared_ptr<ModSound> sound;
    double pos = 0;  // in source frames
    float volume = 1;
};
std::mutex g_slock;
std::map<int, std::shared_ptr<ModSound>> g_sounds;
std::vector<ModVoice> g_voices;
int g_next_sound = 1, g_next_voice = 1;

// adds the playing sounds to `buf` (stereo, `rate`); false if none plays
bool mix_mod_sounds(int16_t* buf, int frames, int rate) {
    std::lock_guard lk(g_slock);
    if (g_voices.empty()) return false;
    for (auto& v : g_voices) {
        const auto& pcm = v.sound->pcm;
        size_t n = pcm.size() / 2;
        double step = (double)v.sound->rate / rate;
        for (int f = 0; f < frames; f++) {
            size_t i = (size_t)v.pos;
            if (i >= n) break;
            double t = v.pos - i;
            for (int c = 0; c < 2; c++) {
                float a = pcm[i * 2 + c], b = i + 1 < n ? pcm[(i + 1) * 2 + c] : a;
                float s = buf[f * 2 + c] + (float)(a + (b - a) * t) * v.volume;
                buf[f * 2 + c] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
            }
            v.pos += step;
        }
    }
    std::erase_if(g_voices, [](const ModVoice& v) { return (size_t)v.pos >= v.sound->pcm.size() / 2; });
    return true;
}
}  // namespace

HLE_FUNC(emu_sound_load, int, (const void* data, size_t len)) {
    std::vector<int16_t> pcm;
    int ch = 0, rate = 0;
    if (!data || !len || !decode_audio((const uint8_t*)data, len, pcm, ch, rate) || rate <= 0) {
        LOGW("sound: cannot decode a sound of %zu bytes (MP3 / Ogg Vorbis only)", len);
        return 0;
    }
    auto s = std::make_shared<ModSound>();
    s->rate = rate;
    if (ch == 1) {
        s->pcm.resize(pcm.size() * 2);
        for (size_t i = 0; i < pcm.size(); i++) s->pcm[i * 2] = s->pcm[i * 2 + 1] = pcm[i];
    } else {
        s->pcm = std::move(pcm);
    }
    std::lock_guard lk(g_slock);
    int id = g_next_sound++;
    g_sounds[id] = s;
    LOGI("sound: loaded #%d (%.2f s, %d Hz, %d channel(s))", id, s->pcm.size() / 2.0 / rate, rate, ch);
    return id;
}
HLE_FUNC(emu_sound_free, void, (int sound)) {
    std::lock_guard lk(g_slock);
    g_sounds.erase(sound);  // a voice that still plays it keeps it
}
HLE_FUNC(emu_sound_play, int, (int sound, float volume)) {
    std::lock_guard lk(g_slock);
    auto it = g_sounds.find(sound);
    if (it == g_sounds.end()) return 0;
    ModVoice v{g_next_voice++, it->second};
    v.volume = volume < 0 ? 0 : volume > 4 ? 4 : volume;
    g_voices.push_back(v);
    return v.id;
}
HLE_FUNC(emu_sound_stop, void, (int voice)) {
    std::lock_guard lk(g_slock);
    std::erase_if(g_voices, [voice](const ModVoice& v) { return voice == 0 || v.id == voice; });
}
HLE_FUNC(emu_sound_playing, int, (int voice)) {
    std::lock_guard lk(g_slock);
    for (auto& v : g_voices)
        if (v.id == voice) return 1;
    return 0;
}

HLE_FUNC(emu_audio_write, void, (const int16_t* samples, int frames)) {
    static std::vector<int16_t> mixed;
    mixed.assign(samples, samples + size_t(frames) * 2);
    if (mix_mod_sounds(mixed.data(), frames, g_rate)) samples = mixed.data();
    // a connected phone gets a copy; the PC device keeps pacing the render thread, silent if asked
    static std::vector<int16_t> silence;
    if (remote::audio(samples, frames, g_rate)) {
        silence.assign(size_t(frames) * 2, 0);
        samples = silence.data();
    }
    if (!g_audio_open) {
        // keep the guest render thread paced even without a device
        Sleep((DWORD)(frames * 1000 / 48000));
        return;
    }
    std::unique_lock lk(g_alock);
    int slot = -1;
    for (;;) {
        for (int i = 0; i < kBuffers; i++)
            if (g_hdr[i].dwFlags & WHDR_DONE) { slot = i; break; }
        if (slot >= 0) break;
        g_acv.wait_for(lk, std::chrono::milliseconds(5));
    }
    WAVEHDR& h = g_hdr[slot];
    if (h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_wave, &h, sizeof h);
    g_bufmem[slot].assign(samples, samples + size_t(frames) * 2);
    h = WAVEHDR{};
    h.lpData = (LPSTR)g_bufmem[slot].data();
    h.dwBufferLength = (DWORD)(frames * 4);
    waveOutPrepareHeader(g_wave, &h, sizeof h);
    waveOutWrite(g_wave, &h, sizeof h);
}

HLE_FUNC(emu_audio_close, void, ()) {
    std::lock_guard lk(g_alock);
    if (!g_audio_open) return;
    waveOutReset(g_wave);
    for (auto& h : g_hdr)
        if (h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_wave, &h, sizeof h);
    waveOutClose(g_wave);
    g_audio_open = false;
}

// ===================================================================== image picker (custom skins)
HLE_FUNC(emu_pick_image_begin, void, ()) { remote::pick_image_begin(); }
// for mods: any picture (PNG / JPEG / BMP / GIF), answered through emu_pick_image_poll / take
HLE_FUNC(emu_pick_media_begin, void, ()) { remote::pick_image_begin(remote::kPickMedia); }
// for mods: an MMD model (a ZIP archive of its folder, or a lone .pmx / .pmd from a phone)
HLE_FUNC(emu_pick_model_begin, void, ()) { remote::pick_image_begin(remote::kPickModel); }
// for mods: a sound file (.mp3 / .ogg)
HLE_FUNC(emu_pick_sound_begin, void, ()) { remote::pick_image_begin(remote::kPickSound); }
HLE_FUNC(emu_pick_image_cancel, void, ()) { remote::pick_image_cancel(); }
HLE_FUNC(emu_pick_image_poll, int, (size_t* len)) { return remote::pick_image_poll(len); }
HLE_FUNC(emu_pick_image_take, void, (void* out)) { remote::pick_image_take(out); }
