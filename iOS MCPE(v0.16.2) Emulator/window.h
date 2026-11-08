// Host window, OpenGL context and input events.
#pragma once
#include "common.h"

namespace window {

// Event record shared with the guest (guest/include/emu/host.h mirrors it)
#pragma pack(push, 1)
struct Event {
    u32 type;     // EventType
    s32 id;       // pointer id / key code
    float x, y;   // pixels (top-left origin)
    u32 chr;      // unicode character for Char events
    u32 mods;
    double time;  // seconds
};
#pragma pack(pop)
static_assert(sizeof(Event) == 32);

enum EventType : u32 {
    EvNone = 0,
    EvPointerDown = 1,
    EvPointerMove = 2,
    EvPointerUp = 3,
    EvKeyDown = 4,
    EvKeyUp = 5,
    EvChar = 6,
    EvResize = 7,
    EvClose = 8,
    EvFocus = 9,
    EvBlur = 10,
    EvWheel = 11,
    // handled on the host side (game mouse input while the cursor is captured)
    EvGameMouseMove = 12,    // x, y = relative motion
    EvGameMouseButton = 13,  // id = 1 left / 2 right / 3 middle / 4 wheel; chr = 1 down, 0 up (wheel: +1/-1); x, y = position
    EvGameMousePos = 14,     // x, y = cursor position (screens)
};

bool create(int width, int height, const char* title);
bool exists();
void size(int* w, int* h);
int poll(Event* out, int max);
void wait(double seconds);
void present(u32 color_renderbuffer, int rb_w, int rb_h);
void set_title(const char* t);
void set_text_input(bool on);  // a text field is active (keys go to it, mouse is released)
bool text_input_active();
void set_gameplay(bool on);    // the game's in-world input mapping is active (captures the mouse)
double now();
u64 frames_presented();
int refresh_rate();  // of the monitor showing the window (Hz)
void toggle_fullscreen();
void* gl_proc(const char* name);
int message_box(const char* title, const char* text, int buttons);

// thread-safe (used by remote.cpp)
void post_event(Event e);
void set_game_size(int w, int h);    // render at w x h instead of the window size (0, 0 = window size)
void set_title_suffix(const char* t);
bool phone_connected();              // set_game_size is in effect (window thread)
void* native_handle();               // HWND

}  // namespace window
