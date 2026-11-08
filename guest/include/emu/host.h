// Host services provided by the emulator (implemented on the Windows side as HLE functions).
#pragma once
#include <stddef.h>
#include <stdint.h>

#pragma pack(push, 1)
typedef struct emu_event {
    uint32_t type;
    int32_t id;
    float x, y;
    uint32_t chr;
    uint32_t mods;
    double time;
} emu_event;
#pragma pack(pop)

enum {
    EMU_EV_POINTER_DOWN = 1,
    EMU_EV_POINTER_MOVE = 2,
    EMU_EV_POINTER_UP = 3,
    EMU_EV_KEY_DOWN = 4,
    EMU_EV_KEY_UP = 5,
    EMU_EV_CHAR = 6,
    EMU_EV_RESIZE = 7,
    EMU_EV_CLOSE = 8,
    EMU_EV_FOCUS = 9,
    EMU_EV_BLUR = 10,
    EMU_EV_WHEEL = 11,
};

#ifdef __cplusplus
extern "C" {
#endif
int emu_window_create(int w, int h);
void emu_window_size(int* w, int* h);
int emu_poll_events(emu_event* out, int max);
void emu_wait_events(double seconds);
double emu_time(void);
// seconds per display-link frame (frameInterval 1); 0 = no limit (vsync paces)
double emu_frame_interval(void);
void emu_set_title(const char* t);
void emu_set_text_input(int on);
int emu_message_box(const char* title, const char* text, int buttons);
int emu_gl_storage_from_drawable(int w, int h);
int emu_gl_present(void);
// images: returns malloc'd RGBA8 (straight alpha) pixels or NULL
void* emu_image_decode(const void* data, size_t len, int* w, int* h);
void* emu_image_encode_png(const void* rgba, int w, int h, size_t* out_len);
// audio output (interleaved 16-bit stereo). write blocks when the device queue is full.
int emu_audio_open(int sample_rate, int channels);
void emu_audio_write(const int16_t* samples, int frames);
void emu_audio_close(void);
// image picker (custom skins): the connected phone's picker or a Windows file dialog.
// poll: 0 pending, 1 picked (*len bytes, copy them with take), -1 cancelled
void emu_pick_image_begin(void);
int emu_pick_image_poll(size_t* len);
void emu_pick_image_take(void* out);
#ifdef __cplusplus
}
#endif
