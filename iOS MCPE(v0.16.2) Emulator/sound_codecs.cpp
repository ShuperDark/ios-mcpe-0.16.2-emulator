// MP3 (minimp3) and Ogg Vorbis (stb_vorbis) decoding for the mods' sounds (emu_sound_load in hle_media.cpp).
// Kept in its own file: stb_vorbis defines short macro names.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#pragma warning(push)
#pragma warning(disable : 4244 4245 4267 4456 4457 4701 4996)
#define MINIMP3_IMPLEMENTATION
#include "../external/minimp3/minimp3_ex.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../external/stb_vorbis/stb_vorbis.c"
#pragma warning(pop)

// Decodes a whole MP3 or Ogg Vorbis file to interleaved 16-bit samples (1 or 2 channels).
bool decode_audio(const uint8_t* data, size_t len, std::vector<int16_t>& out, int& channels, int& rate) {
    out.clear();
    if (len >= 4 && memcmp(data, "OggS", 4) == 0) {
        int ch = 0, r = 0;
        short* pcm = nullptr;
        int frames = stb_vorbis_decode_memory(data, (int)len, &ch, &r, &pcm);
        if (frames <= 0 || !pcm || ch <= 0) {
            free(pcm);
            return false;
        }
        // down to stereo
        int keep = ch > 2 ? 2 : ch;
        out.resize((size_t)frames * keep);
        for (int f = 0; f < frames; f++)
            for (int c = 0; c < keep; c++) out[(size_t)f * keep + c] = pcm[(size_t)f * ch + c];
        free(pcm);
        channels = keep;
        rate = r;
        return true;
    }
    mp3dec_t dec;
    mp3dec_file_info_t info{};
    if (mp3dec_load_buf(&dec, data, len, &info, nullptr, nullptr) != 0 || !info.buffer || info.samples == 0 ||
        info.channels <= 0) {
        free(info.buffer);
        return false;
    }
    int ch = info.channels, keep = ch > 2 ? 2 : ch;
    size_t frames = info.samples / ch;
    out.resize(frames * keep);
    for (size_t f = 0; f < frames; f++)
        for (int c = 0; c < keep; c++) out[f * keep + c] = info.buffer[f * ch + c];
    free(info.buffer);
    channels = keep;
    rate = info.hz;
    return true;
}
